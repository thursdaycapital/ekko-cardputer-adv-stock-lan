/** Explicit daily-Studio supplement. No login, device registration or settings
 * mutations. Credentials enter through stdin only; public results contain no
 * server strings, chat text, session IDs, model identities or authentication.
 * Socket run/abort/resume are the only writes, confined to fresh owned sessions.
 */
import { request as httpRequest } from 'node:http';
import { request as httpsRequest } from 'node:https';
import { readFile, open, realpath } from 'node:fs/promises';
import { createRequire } from 'node:module';
import { resolve, relative, join, isAbsolute } from 'node:path';
import { randomUUID, createHash } from 'node:crypto';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';

export const INPUT = '请不要调用工具，只回复：连接成功';
export const DAILY_HELPER_VERSION = 6;
const own = (value, key) => Object.hasOwn(value, key);
const object = value => value !== null && typeof value === 'object' && !Array.isArray(value);
const text = (value, maximum = 512) => typeof value === 'string' && value.trim().length > 0 &&
  Buffer.byteLength(value) <= maximum && !/[\u0000-\u001f\u007f]/u.test(value);
const fail = () => { throw new Error('DAILY_VALIDATION_FAILED'); };
const apiModes = new Set(['chat_completions', 'anthropic_messages', 'codex_responses']);
const nativeStatuses = new Set(['ready', 'empty', 'unsupported', 'not_installed', 'auth_required', 'timeout', 'error']);
const nativeProbeStatuses = new Set(['ready', 'empty', 'unsupported', 'auth_required', 'error']);
const oauthProviders = new Set(['openai-codex', 'copilot', 'xai-oauth', 'qwen-oauth', 'nous', 'claude-oauth', 'minimax-oauth']);
const blocked = reason => ({ available: false, reason });
const execute = promisify(execFile);

export async function hostIntegrity(options, verifySignature = true) {
  const entry = options.get('server-entry'), expected = options.get('expected-server-sha'), bundle = options.get('app-bundle');
  if (!entry) {
    if (expected || bundle) fail();
    return { checked: false, unmodifiedRuntimeVerified: false };
  }
  if ((!expected && !bundle) || (expected && !/^[a-f0-9]{64}$/.test(expected))) fail();
  const actualSha256 = createHash('sha256').update(await readFile(entry)).digest('hex');
  const result = { checked: true, actualSha256, expectedSource: bundle ? 'app_CodeResources' : 'operator_supplied',
    unmodifiedRuntimeVerified: false };
  let sealedSha;
  if (bundle) {
    const contents = await realpath(join(bundle, 'Contents')), actualEntry = await realpath(entry);
    const resource = relative(contents, actualEntry);
    if (!resource || resource.startsWith('..') || isAbsolute(resource)) fail();
    const code = "import json,plistlib,sys\nwith open(sys.argv[1],'rb') as f: p=plistlib.load(f)\nv=p.get('files2',{}).get(sys.argv[2],{})\nh=v.get('hash2') if isinstance(v,dict) else None\nprint(json.dumps({'sha256':h.hex() if isinstance(h,bytes) and len(h)==32 else None}))";
    const { stdout } = await execute('/usr/bin/python3', ['-B', '-c', code,
      join(contents, '_CodeSignature/CodeResources'), resource.split('\\').join('/')], { maxBuffer: 4096, timeout: 10000 });
    sealedSha = JSON.parse(stdout).sha256;
    if (typeof sealedSha !== 'string' || !/^[a-f0-9]{64}$/.test(sealedSha)) fail();
    result.sealedResourceSha256 = sealedSha;
    if (verifySignature) {
      try { await execute('/usr/bin/codesign', ['--verify', '--ignore-resources', '--strict', bundle], { maxBuffer: 16384, timeout: 15000 }); result.codeSignatureValid = true; }
      catch { result.codeSignatureValid = false; }
      try { await execute('/usr/bin/codesign', ['--verify', '--deep', '--strict', bundle], { maxBuffer: 16384, timeout: 15000 }); result.codesignStrictValid = true; }
      catch { result.codesignStrictValid = false; }
    }
  }
  result.expectedSha256 = expected || sealedSha;
  result.matchesExpected = actualSha256 === result.expectedSha256 && (!sealedSha || actualSha256 === sealedSha) &&
    result.codeSignatureValid !== false;
  return result;
}

export function validateAuth(raw) {
  if (!object(raw) || Object.keys(raw).some(key => !['baseUrl', 'userJwt', 'profile', 'expectedUserId'].includes(key))) fail();
  if (!text(raw.baseUrl, 256) || !text(raw.userJwt, 8192) || !text(raw.profile, 96)) fail();
  if (!/^[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+$/.test(raw.userJwt)) fail();
  const url = new URL(raw.baseUrl);
  const host = url.hostname;
  const octets = /^\d+\.\d+\.\d+\.\d+$/.test(host) ? host.split('.').map(Number) : [];
  const privateV4 = octets.length === 4 && octets.every(n => n >= 0 && n <= 255) &&
    (octets[0] === 10 || octets[0] === 127 || (octets[0] === 192 && octets[1] === 168) ||
     (octets[0] === 172 && octets[1] >= 16 && octets[1] <= 31));
  if (!['http:', 'https:'].includes(url.protocol) || (!privateV4 && host !== 'localhost') ||
      url.username || url.password || url.search || url.hash || url.pathname !== '/') fail();
  if (raw.expectedUserId !== undefined && (!Number.isSafeInteger(raw.expectedUserId) || raw.expectedUserId <= 0)) fail();
  return { baseUrl: url.origin, userJwt: raw.userJwt, profile: raw.profile, expectedUserId: raw.expectedUserId };
}

export function parsePlan(spec, catalog) {
  if (!text(spec, 4096) || !Array.isArray(catalog) || catalog.length !== 16) fail();
  const routes = new Map(catalog.map(agent => [agent.id, agent]));
  if (routes.size !== 16) fail();
  const seen = new Set();
  return spec.split(',').map(pair => {
    const parts = pair.split(':');
    const [id, mode] = parts;
    const agent = routes.get(id);
    if (parts.length !== 2 || !agent || seen.has(id) || !['studio', 'native'].includes(mode) ||
        !agent.modes?.includes(mode === 'studio' ? 'scoped' : 'global')) fail();
    seen.add(id);
    return { id, mode, source: id === 'ekko-agent' ? 'builtin_agent' : id === 'hermes' ? 'cli' : 'coding_agent' };
  });
}

export function selectProfileModel(raw, profile, route) {
  if (!object(raw) || !Array.isArray(raw.profiles) || !raw.profiles.every(p => object(p) && text(p.profile, 96))) return blocked('model_metadata_invalid');
  const matches = raw.profiles.filter(p => object(p) && p.profile === profile);
  if (matches.length !== 1) return blocked('profile_unavailable');
  const p = matches[0];
  const optionalDefault = (value, maximum) => typeof value === 'string' && Buffer.byteLength(value) <= maximum &&
    !/[\u0000-\u001f\u007f]/u.test(value);
  if (!optionalDefault(p.default_provider, 256) || !optionalDefault(p.default, 512) || !Array.isArray(p.groups))
    return blocked('default_model_incomplete');
  const compatible = provider => {
    const normalized = provider.trim().toLowerCase();
    return !(route.source === 'coding_agent' && oauthProviders.has(normalized)) &&
      !(route.id !== 'hermes' && normalized === 'moa');
  };
  const selectable = (group, model) => {
    const meta = own(group, 'model_meta') && own(group.model_meta, model) ? group.model_meta[model] : undefined;
    return !meta?.disabled && !meta?.hidden;
  };
  let preferred, first;
  for (const group of p.groups) {
    if (!object(group) || !text(group.provider, 256) || !Array.isArray(group.models) ||
        !group.models.every(m => text(m, 512))) return blocked('model_metadata_invalid');
    if (own(group, 'model_meta') && (!object(group.model_meta) || !Object.entries(group.model_meta).every(([key, meta]) =>
      text(key, 512) && object(meta) && ['disabled', 'hidden'].every(k => !own(meta, k) || typeof meta[k] === 'boolean'))))
      return blocked('model_metadata_invalid');
    if (own(group, 'api_mode_by_model') && (!object(group.api_mode_by_model) || !Object.entries(group.api_mode_by_model).every(([key, value]) =>
      text(key, 512) && (value === null || text(value, 23))))) return blocked('model_metadata_invalid');
    if (own(group, 'api_mode') && group.api_mode !== null && !optionalDefault(group.api_mode, 23))
      return blocked('model_metadata_invalid');
    if (!compatible(group.provider)) continue;
    if (group.provider === p.default_provider) {
      if (preferred) return blocked('model_metadata_invalid');
      preferred = group;
    }
    if (!first && group.models.some(m => selectable(group, m))) first = group;
  }
  // Match the official fresh New Chat flow and production selector. A default
  // provider omitted from profile.groups is not proof that all models are
  // missing (e.g. an OAuth default appears only in allProviders). Choose only
  // this Profile's first compatible group, in the server's existing order.
  // A still-present default group with no visible models remains unavailable.
  const group = preferred || first;
  if (!group) return blocked('default_model_unavailable');
  const useDefault = group.models.includes(p.default) && selectable(group, p.default);
  const model = useDefault ? p.default : group.models.find(m => selectable(group, m));
  if (!model) return blocked('default_model_unavailable');
  const apiMode = own(group, 'api_mode_by_model') ?
    (own(group.api_mode_by_model, model) ? group.api_mode_by_model[model] : undefined) : group.api_mode;
  if (!apiModes.has(apiMode)) return blocked('default_api_mode_unavailable');
  // Deliberate field projection: api_key, base_url and all server metadata die here.
  return { available: true, provider: group.provider, model, apiMode,
    selectionOrigin: preferred && useDefault ? 'profile_default' : 'compatible_default' };
}

export function installedAgents(raw) {
  if (!object(raw) || !Array.isArray(raw.tools)) fail();
  const found = new Map();
  for (const tool of raw.tools) {
    if (!object(tool) || !text(tool.id, 64) || typeof tool.installed !== 'boolean' || found.has(tool.id)) fail();
    found.set(tool.id, tool.installed);
  }
  return found;
}

export function parseAttemptedStatuses(spec = 'ready,empty') {
  if (!text(spec, 128)) fail();
  const values = spec.split(',');
  if (new Set(values).size !== values.length || values.some(status => !nativeProbeStatuses.has(status))) fail();
  return new Set(values);
}

export function inspectNative(raw, id, installed, attempted = new Set(['ready', 'empty'])) {
  if (!installed) return { status: 'not_installed', available: false, firmwarePreflightAccepts: false, modelCount: 0 };
  // Mirror alpha5's complete parsed catalog validation, including unselected
  // rows. The helper sends only Explicit Native with the CLI default model;
  // it does not predict admission for Automatic or an explicit model identity.
  if (!object(raw) || !Array.isArray(raw.agents) || !raw.agents.every(a => object(a) && text(a.agentId, 64) &&
      nativeStatuses.has(a.status) && Array.isArray(a.models) &&
      a.models.every(m => object(m) && text(m.id) && (!own(m, 'hidden') || typeof m.hidden === 'boolean'))))
    return { status: 'invalid', available: false, firmwarePreflightAccepts: false, modelCount: 0 };
  const matches = raw.agents.filter(a => a.agentId === id);
  if (matches.length !== 1)
    return { status: 'invalid', available: false, firmwarePreflightAccepts: false, modelCount: 0 };
  const entry = matches[0];
  return { status: entry.status, available: nativeProbeStatuses.has(entry.status) && attempted.has(entry.status),
    firmwarePreflightAccepts: ['ready', 'empty', 'unsupported', 'auth_required', 'error'].includes(entry.status), modelCount: entry.models.length };
}

export function buildPayload(route, profile, model, sessionId, queueId) {
  const payload = { source: route.source, input: INPUT, profile, session_id: sessionId, queue_id: queueId };
  if (route.id !== 'hermes') { payload.coding_agent_id = route.id; payload.mode = route.mode === 'studio' ? 'scoped' : 'global'; }
  if (route.id === 'ekko-agent') payload.agent_id = route.id;
  if (route.mode === 'studio') {
    if (!model?.available) fail();
    Object.assign(payload, { provider: model.provider, model: model.model, apiMode: model.apiMode });
  }
  return payload;
}

export function failureCategory(raw) {
  const s = typeof raw === 'string' ? raw : '';
  if (/PLUGIN_SCHEMA/.test(s)) return 'host_plugin_schema';
  if (/429|rate.?limit|quota|余额|限流/i.test(s)) return 'quota_or_rate_limit';
  if (/401|403|api.?key|auth|log.?in|token|credential|未登录|密钥|授权/i.test(s)) return 'authentication_required';
  if (/not installed|not found|ENOENT|未安装/i.test(s)) return 'runtime_missing';
  if (/unknown (argument|option)|unrecognized|unexpected argument|invalid.*flag/i.test(s)) return 'cli_argument_incompatible';
  if (/unsupported.*provider|provider.*not supported|not compatible/i.test(s)) return 'provider_incompatible';
  if (/timeout|timed out|超时/i.test(s)) return 'timeout';
  if (/network|fetch failed|ECONN|ENOTFOUND|TLS|certificate/i.test(s)) return 'network_error';
  if (/model|模型/i.test(s)) return 'model_unavailable';
  return 'runtime_error';
}

export function historyProof(body, route, profile, sessionId) {
  if (!object(body) || !object(body.session) || body.session.id !== sessionId || body.session.profile !== profile ||
      body.session.source !== route.source ||
      (body.session.agent !== route.id && !(route.id === 'hermes' && body.session.agent === '') &&
        // Official Studio canonicalizes the Claude Code CLI route when storing
        // sessions. This one documented mapping does not relax any scope field.
        !(route.id === 'claude-code' && body.session.agent === 'claude')) ||
      (route.id !== 'hermes' && body.session.agent_mode !== (route.mode === 'native' ? 'global' : 'scoped')) ||
      !Array.isArray(body.messages)) return { routeMatched: false, inputMatched: false, answerMatched: false };
  const rows = body.messages.filter(object);
  const content = row => row.display_content ?? row.content;
  return { routeMatched: true,
    inputMatched: rows.some(m => (m.display_role || m.role) === 'user' && typeof content(m) === 'string' && content(m).trim() === INPUT),
    answerMatched: rows.some(m => (m.display_role || m.role) === 'assistant' && typeof content(m) === 'string' && content(m).includes('连接成功')) };
}

function historyPath(sessionId, profile) {
  return `/api/studio/sessions/conversations/${encodeURIComponent(sessionId)}/messages/paginated?profile=${encodeURIComponent(profile)}&offset=0&limit=12`;
}
function sessionPath(sessionId, profile) {
  return `/api/studio/sessions/${encodeURIComponent(sessionId)}?profile=${encodeURIComponent(profile)}`;
}
export function idleResume(value, sessionId) {
  return object(value) && value.session_id === sessionId && value.isWorking === false && value.isAborting === false &&
    value.queueLength === 0 && Array.isArray(value.queueMessages) && value.queueMessages.length === 0 &&
    value.queueInsertion === null && (!own(value, 'backgroundPending') || value.backgroundPending === 0);
}
export function allowedReadPath(path) {
  return path === '/health' || path === '/api/auth/me' ||
    /^\/api\/(?:coding-agents|hermes\/available-models)\?profile=[A-Za-z0-9%_.~-]+$/.test(path) ||
    /^\/api\/coding-agents\/models\?agent=[a-z0-9-]+&profile=[A-Za-z0-9%_.~-]+$/.test(path) ||
    /^\/api\/studio\/sessions\/[A-Za-z0-9-]+\?profile=[A-Za-z0-9%_.~-]+$/.test(path) ||
    /^\/api\/studio\/sessions\/conversations\/[A-Za-z0-9-]+\/messages\/paginated\?profile=[A-Za-z0-9%_.~-]+&offset=0&limit=12$/.test(path);
}

export function directReader(auth, timeoutMs = 30000) {
  return path => new Promise((resolvePromise, reject) => {
    if (!allowedReadPath(path)) { reject(new Error('GET_PATH_REJECTED')); return; }
    const url = new URL(path, auth.baseUrl);
    const call = url.protocol === 'https:' ? httpsRequest : httpRequest;
    let bytes = 0; const chunks = [];
    const request = call(url, { method: 'GET', headers: { Authorization: `Bearer ${auth.userJwt}`,
      'x-hermes-profile': auth.profile, 'Cache-Control': 'no-cache' } }, response => {
      if (response.headers['content-encoding'] && response.headers['content-encoding'] !== 'identity') { request.destroy(new Error('ENCODING_REJECTED')); return; }
      response.on('data', chunk => {
        bytes += chunk.length;
        if (bytes > 2 * 1024 * 1024) request.destroy(new Error('RESPONSE_TOO_LARGE'));
        else chunks.push(chunk);
      });
      response.on('error', () => request.destroy(new Error('RESPONSE_FAILED')));
      response.on('end', () => {
        clearTimeout(timer);
        const status = response.statusCode;
        // No redirects, cookies, error text or raw body escape the transport.
        if (status !== 200 && status !== 404) { resolvePromise({ status }); return; }
        try { resolvePromise({ status, body: status === 200 ? JSON.parse(Buffer.concat(chunks).toString('utf8')) : undefined }); }
        catch { reject(new Error('RESPONSE_INVALID')); }
      });
    });
    const timer = setTimeout(() => request.destroy(new Error('REQUEST_TIMEOUT')), timeoutMs);
    request.on('error', () => { clearTimeout(timer); reject(new Error('TRANSPORT_FAILED')); });
    request.end();
  });
}

async function connection(socket, timeoutMs = 15000) {
  await new Promise((resolvePromise, reject) => {
    const timer = setTimeout(() => done(false), timeoutMs);
    const ok = () => done(true), error = () => done(false);
    function done(connected) { clearTimeout(timer); socket.off('connect', ok); socket.off('connect_error', error); connected ? resolvePromise() : reject(new Error('CONNECT_FAILED')); }
    socket.once('connect', ok); socket.once('connect_error', error);
    try { socket.connect(); } catch { done(false); }
  });
}

export async function ownedRun({ socket, request, route, profile, model, sessionId, queueId, timeoutMs = 60000, settleMs = 10000, signal, sleep = ms => new Promise(r => setTimeout(r, ms)) }) {
  if (signal?.aborted) return { state: 'not_sent', reason: 'interrupted_before_send', executionVerified: false, safeToContinue: true };
  let before;
  // The official local detail endpoint gives a definite 404 for a nonexistent
  // session. Paginated history can query the external Hermes bridge instead
  // and fail with 500; neither 500 nor any other failed check authorizes a run.
  try { before = await request(sessionPath(sessionId, profile)); }
  catch { return { state: 'not_sent', reason: 'fresh_session_check_failed', executionVerified: false, safeToContinue: true }; }
  if (signal?.aborted) return { state: 'not_sent', reason: 'interrupted_before_send', executionVerified: false, safeToContinue: true };
  if (before.status !== 404) return { state: 'not_sent', reason: before.status === 200 ? 'session_not_fresh' : 'fresh_session_check_failed',
    freshSessionStatus: before.status, executionVerified: false, safeToContinue: true };
  const payload = buildPayload(route, profile, model, sessionId, queueId);
  const result = await new Promise(resolvePromise => {
    let runId = '', queueSeen = false, outputCharacters = 0, answerMatched = false, tail = '', finished = false;
    const timer = setTimeout(() => done('timeout', 'timeout'), timeoutMs);
    const disconnected = () => done('uncertain', 'connection_lost');
    const interrupted = () => done('interrupted', 'signal_received');
    function done(state, reason) {
      if (finished) return; finished = true; clearTimeout(timer); socket.offAny(event); socket.off('disconnect', disconnected); signal?.removeEventListener('abort', interrupted);
      resolvePromise({ state, reason, outputCharacters, streamingAnswerMatched: answerMatched, queueCorrelated: queueSeen, started: Boolean(runId) });
    }
    function event(name, value) {
      if (!object(value) || value.session_id !== sessionId ||
          (own(value, 'queue_id') && value.queue_id !== undefined && value.queue_id !== null && value.queue_id !== queueId)) return;
      if (typeof value.run_id === 'string' && runId && value.run_id !== runId) return;
      if (name === 'run.started') {
        if (!text(value.run_id, 128)) return;
        runId = value.run_id; queueSeen ||= value.queue_id === queueId; return;
      }
      const correlated = value.queue_id === queueId || (runId && value.run_id === runId);
      if (name === 'message.delta' && runId) {
        const chunk = value.content ?? value.delta ?? value.text;
        if (typeof chunk === 'string') { outputCharacters += [...chunk].length; tail = (tail + chunk).slice(-128); answerMatched ||= tail.includes('连接成功'); }
      }
      if (name === 'run.completed' && correlated) done('completed');
      if (name === 'run.failed') done(correlated ? 'failed' : 'uncertain', failureCategory(value.error));
    }
    socket.onAny(event); socket.once('disconnect', disconnected); signal?.addEventListener('abort', interrupted, { once: true });
    try { socket.emit('run', payload); } catch { done('uncertain', 'emit_failed'); }
  });
  let abortSent = false, stoppedAfterAbort = false;
  if (['timeout', 'uncertain', 'interrupted'].includes(result.state)) {
    if (socket.connected) {
      stoppedAfterAbort = await new Promise(resolvePromise => {
        const timer = setTimeout(() => finish(false), settleMs);
        let retry;
        const handler = value => { if (idleResume(value, sessionId)) finish(true); };
        const disconnected = () => finish(false);
        function finish(stopped) { clearTimeout(timer); clearInterval(retry); socket.off('resumed', handler); socket.off('disconnect', disconnected); resolvePromise(stopped); }
        socket.on('resumed', handler); socket.once('disconnect', disconnected); abortSent = true;
        retry = setInterval(() => {
          if (socket.connected) { try { socket.emit('resume', { session_id: sessionId }); } catch { finish(false); } }
        }, 250);
        try { socket.emit('abort', { session_id: sessionId }); socket.emit('resume', { session_id: sessionId }); }
        catch { finish(false); }
      });
    }
  }
  let historyStatus = 0, proof = { routeMatched: false, inputMatched: false, answerMatched: false };
  for (let attempt = 0; attempt < 5; ++attempt) {
    if (signal?.aborted) break;
    let history;
    try { history = await request(historyPath(sessionId, profile)); }
    catch { historyStatus = 0; break; }
    historyStatus = history.status;
    if (history.status === 200) proof = historyProof(history.body, route, profile, sessionId);
    if (proof.routeMatched && proof.inputMatched && proof.answerMatched) break;
    if (attempt < 4) await sleep(200);
  }
  return { ...result, abortSent, stoppedAfterAbort, historyStatus, history: proof,
    executionVerified: result.state === 'completed' && proof.routeMatched && proof.inputMatched && proof.answerMatched,
    safeToContinue: !['timeout', 'uncertain', 'interrupted'].includes(result.state) || stoppedAfterAbort };
}

export async function dailyAudit({ auth, plan, request, socketFactory, runTimeout = 60000, attempted = new Set(['ready', 'empty']), id = randomUUID, sleep, signal, verifyHost }) {
  const report = { schemaVersion: 1, helperVersion: DAILY_HELPER_VERSION, mode: 'daily-supplement', transport: 'official /chat-run',
    physicalDeviceTest: false, remoteTest: false, lowPrivilegeAccountTest: false,
    newLoginPerformed: false, deviceRegistered: false, settingsChanged: false,
    testedAt: new Date().toISOString(), runs: [], allRequestedExecuted: false };
  const summarize = () => {
    report.interrupted = signal?.aborted === true;
    report.allRequestedExecuted = report.runs.length === plan.length && report.runs.every(r => r.executionVerified);
    report.requestedCount = plan.length; report.executedCount = report.runs.filter(r => r.executionAttempted).length;
    report.verifiedCount = report.runs.filter(r => r.executionVerified).length;
    return report;
  };
  report.hostIntegrity = verifyHost ? await verifyHost(true) : { checked: false, unmodifiedRuntimeVerified: false };
  if ((report.hostIntegrity.checked && !report.hostIntegrity.matchesExpected) || signal?.aborted) {
    report.stoppedBecauseHostIntegrity = report.hostIntegrity.checked && !report.hostIntegrity.matchesExpected;
    return summarize();
  }
  const health = await request('/health');
  if (health.status !== 200 || !text(health.body?.webui_version, 32) ||
      !/^\d+\.\d+\.\d+(?:[-+][A-Za-z0-9.-]+)?$/.test(health.body.webui_version)) fail();
  report.studioVersion = health.body.webui_version;
  const me = await request('/api/auth/me');
  if (me.status !== 200 || !Number.isSafeInteger(me.body?.user?.id) || me.body.user.id <= 0 ||
      (auth.expectedUserId !== undefined && auth.expectedUserId !== me.body.user.id)) fail();
  report.authenticatedRole = ['super_admin', 'admin', 'user'].includes(me.body.user.role) ? me.body.user.role : 'other';
  const query = `?profile=${encodeURIComponent(auth.profile)}`;
  const tools = await request('/api/coding-agents' + query);
  if (tools.status !== 200) fail();
  const installed = installedAgents(tools.body);
  let models;
  if (plan.some(route => route.mode === 'studio')) {
    const response = await request('/api/hermes/available-models' + query);
    if (response.status !== 200) fail(); models = response.body;
  }
  const socket = socketFactory();
  try {
    await connection(socket);
    for (const route of plan) {
      if (signal?.aborted) break;
      const record = { agentId: route.id, mode: route.mode, installed: route.source !== 'coding_agent' || installed.get(route.id) === true,
        executionAttempted: false, executionVerified: false };
      report.runs.push(record);
      if (!record.installed) { record.preflight = 'not_installed'; continue; }
      let model;
      if (route.mode === 'studio') {
        model = selectProfileModel(models, auth.profile, route);
        record.preflight = model.available ? 'ready' : model.reason;
        if (model.available) record.modelSelectionOrigin = model.selectionOrigin;
        if (!model.available) continue;
      } else {
        let response;
        try { response = await request(`/api/coding-agents/models?agent=${route.id}&profile=${encodeURIComponent(auth.profile)}`); }
        catch { record.preflight = 'transport_error'; continue; }
        const catalog = inspectNative(response.body, route.id, true, attempted);
        record.preflight = response.status === 200 ? catalog.status : 'http_error';
        record.firmwarePreflightAccepts = response.status === 200 && catalog.firmwarePreflightAccepts;
        record.nativeModelCount = catalog.modelCount;
        if (response.status !== 200 || !catalog.available) continue;
        record.operatorRequestedProbeBeyondFirmwarePreflight = !catalog.firmwarePreflightAccepts;
        record.operatorRequestedProbeBeyondDefaultToolPolicy = !['ready', 'empty'].includes(catalog.status);
      }
      if (signal?.aborted) break;
      if (verifyHost) {
        const current = await verifyHost(true);
        if (current.checked && !current.matchesExpected) { report.hostIntegrity = current; report.stoppedBecauseHostIntegrity = true; break; }
      }
      const sessionId = `cardputer-daily-${id()}`, queueId = `daily-${id()}`;
      const result = await ownedRun({ socket, request, route, profile: auth.profile, model, sessionId, queueId, timeoutMs: runTimeout, sleep, signal });
      record.executionAttempted = result.state !== 'not_sent'; Object.assign(record, result);
      if (!result.safeToContinue) { report.stoppedBecauseRunUncertain = true; break; }
    }
  } finally { socket.disconnect(); }
  return summarize();
}

export async function readAuthStream(stream) {
  const chunks = []; let bytes = 0;
  for await (const chunk of stream) { const data = Buffer.from(chunk); bytes += data.length; if (bytes > 32768) fail(); chunks.push(data); }
  try { return validateAuth(JSON.parse(Buffer.concat(chunks).toString('utf8'))); } catch { fail(); }
}

export async function dailyMain(options) {
  const allowed = new Set(['mode', 'modules', 'report', 'agent-modes', 'native-statuses', 'run-timeout',
    'server-entry', 'expected-server-sha', 'app-bundle']);
  if ([...options.keys()].some(key => !allowed.has(key)) || ['modules', 'report', 'agent-modes'].some(key => !options.get(key))) fail();
  const timeout = Number(options.get('run-timeout') || 60000);
  if (!Number.isSafeInteger(timeout) || timeout < 1000 || timeout > 180000) fail();
  // Metadata auth/error can be explicitly probed like the official Native UI.
  // This never bypasses Studio JWT/HTTP authorization or changes the original
  // catalog status. Firmware admission and this paid-probe policy are separate.
  const attempted = parseAttemptedStatuses(options.get('native-statuses'));
  const catalog = JSON.parse(await readFile(new URL('../../config/agents.json', import.meta.url), 'utf8')).agents;
  const plan = parsePlan(options.get('agent-modes'), catalog);
  const auth = await readAuthStream(process.stdin);
  // Reserve the report BEFORE any network/model call. Reusing an existing
  // report must never execute the same paid test again and only then fail.
  const reportFile = await open(options.get('report'), 'wx', 0o600);
  const controller = new AbortController(), interrupt = () => controller.abort();
  process.on('SIGINT', interrupt); process.on('SIGTERM', interrupt);
  let manager, saved = false;
  try {
    const require = createRequire(resolve(options.get('modules'), 'package.json'));
    const { Manager } = require('socket.io-client');
    const report = await dailyAudit({ auth, plan, request: directReader(auth), runTimeout: timeout, attempted,
      signal: controller.signal, verifyHost: checkSignature => hostIntegrity(options, checkSignature),
      socketFactory: () => {
        manager = new Manager(auth.baseUrl, { transports: ['websocket'], autoConnect: false, reconnection: false,
          timeout: 15000, query: { profile: auth.profile } });
        return manager.socket('/chat-run', { auth: { token: auth.userJwt } });
      } });
    await reportFile.writeFile(JSON.stringify(report, null, 2) + '\n'); saved = true;
    console.log(`Daily supplement: ${report.verifiedCount}/${report.requestedCount} requests verified; physicalDeviceTest=false.`);
    if (!report.allRequestedExecuted) process.exitCode = 1;
  } finally {
    process.off('SIGINT', interrupt); process.off('SIGTERM', interrupt);
    manager?._close(); auth.userJwt = '';
    if (!saved) await reportFile.writeFile(JSON.stringify({ schemaVersion: 1, mode: 'daily-supplement',
      state: 'stopped_before_report', interrupted: controller.signal.aborted,
      executionVerified: false, physicalDeviceTest: false, remoteTest: false }) + '\n');
    await reportFile.close();
  }
}
