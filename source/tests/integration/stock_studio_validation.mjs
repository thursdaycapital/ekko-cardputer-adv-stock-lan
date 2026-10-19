#!/usr/bin/env node
/** Live contract checks against an isolated, unmodified official Studio.
 * Credentials and raw metadata are never copied to the public result. This is
 * host protocol coverage, not a substitute for M5 microphone/speaker testing.
 */
import assert from 'node:assert/strict';
import { readFile, writeFile, chmod, mkdtemp, rm } from 'node:fs/promises';
import { createRequire } from 'node:module';
import { resolve, join } from 'node:path';
import { tmpdir } from 'node:os';
import { randomUUID } from 'node:crypto';
import { spawnSync } from 'node:child_process';

const options = new Map();
for (let index = 2; index < process.argv.length; index += 2) {
  if (!process.argv[index].startsWith('--') || !process.argv[index + 1]) throw Error('Arguments require --name value');
  options.set(process.argv[index].slice(2), process.argv[index + 1]);
}
for (const name of ['manifest', 'modules', 'report']) assert(options.has(name), `Missing --${name}`);
const manifest = JSON.parse(await readFile(options.get('manifest'), 'utf8'));
assert(['127.0.0.1', 'localhost'].includes(new URL(manifest.baseUrl).hostname), 'Run live tests on an isolated loopback service only');
const require = createRequire(resolve(options.get('modules'), 'package.json'));
const { Manager } = require('socket.io-client');
const profile = manifest.profile || 'default';
const query = `?profile=${encodeURIComponent(profile)}`;
let jwt = manifest.userJwt;
const report = { testedAt: new Date().toISOString(), version: manifest.version, transport: 'official LAN /chat-run + /global-agent',
  verifiedPackageFiles: manifest.verifiedPackageFiles, serverSha256: manifest.serverSha256,
  physicalDeviceTest: false, remoteTest: false, tests: [], runs: [], findings: [] };
const manager = new Manager(manifest.baseUrl, { transports: ['websocket'], autoConnect: false, reconnection: false, timeout: 15000 });
let chat;
let status;
let registeredDevice;
let installed;
let available;
const privateErrors = [];

function failureCategory(value) {
  const message = typeof value === 'string' ? value : JSON.stringify(value || '');
  if (/unknown (argument|option)|unrecognized (argument|option)|unexpected argument|invalid.*flag/i.test(message)) return 'cli_argument_incompatible';
  if (/not installed|not found|ENOENT|找不到|未安装/i.test(message)) return 'runtime_or_file_missing';
  if (/unsupported.*provider|provider.*not supported|not compatible/i.test(message)) return 'provider_incompatible';
  if (/401|403|api.?key|auth|log.?in|token|credential|未登录|密钥|授权/i.test(message)) return 'authentication_or_authorization_required';
  if (/429|rate.?limit|quota|余额|限流/i.test(message)) return 'provider_quota_or_rate_limit';
  if (/timeout|timed out|超时/i.test(message)) return 'timeout';
  if (/network|fetch failed|ECONN|ENOTFOUND|TLS|certificate/i.test(message)) return 'network_or_transport_error';
  if (/model|模型/i.test(message)) return 'model_unavailable_or_incompatible';
  if (/exit|退出/i.test(message)) return 'runtime_exit';
  return 'runtime_reported_error';
}

async function request(path, { method = 'GET', body, timeout = 30000, json = true } = {}) {
  const headers = { Authorization: `Bearer ${jwt}`, 'x-hermes-profile': profile };
  if (body !== undefined && !(body instanceof FormData)) headers['Content-Type'] = 'application/json';
  const response = await fetch(manifest.baseUrl + path, { method, headers,
    body: body === undefined ? undefined : body instanceof FormData ? body : JSON.stringify(body), signal: AbortSignal.timeout(timeout) });
  const value = json ? await response.json() : Buffer.from(await response.arrayBuffer());
  return { response, value };
}
async function test(name, callback) {
  const started = Date.now();
  try {
    const detail = await callback();
    report.tests.push({ name, passed: true, elapsedMs: Date.now() - started, ...detail });
    console.log(`PASS ${name}`);
  } catch (error) {
    // Only locally generated assertion messages go public. Provider responses,
    // socket errors and request bodies may contain sensitive configuration.
    report.tests.push({ name, passed: false, elapsedMs: Date.now() - started, failureType: error.name });
    console.log(`FAIL ${name} (${error.name})`);
  }
}
function connect(socket) {
  return new Promise((resolvePromise, reject) => {
    const timer = setTimeout(() => finish(Error('Namespace connection timed out')), 20000);
    const onConnect = () => finish();
    const onError = () => finish(Error('Namespace connection rejected'));
    function finish(error) {
      clearTimeout(timer); socket.off('connect', onConnect); socket.off('connect_error', onError);
      error ? reject(error) : resolvePromise();
    }
    socket.once('connect', onConnect); socket.once('connect_error', onError); socket.connect();
  });
}

async function run(agentId) {
  const mode = ['cursor', 'qoder'].includes(agentId) ? 'global' : 'scoped';
  const sessionId = `cardputer-stock-${randomUUID()}`;
  const payload = { source: agentId === 'ekko-agent' ? 'builtin_agent' : agentId === 'hermes' ? 'cli' : 'coding_agent',
    input: '请不要调用工具，只回复：连接成功', session_id: sessionId, queue_id: randomUUID(), profile,
    workspace: manifest.workspace, background_delegation_enabled: false, context_compression_enabled: false, mcpServers: {} };
  if (agentId !== 'hermes') { payload.coding_agent_id = agentId; payload.mode = mode; }
  if (agentId === 'ekko-agent') payload.agent_id = agentId;
  if (mode === 'scoped') {
    const group = available.groups.find(group => group.provider === available.default_provider);
    assert(group?.api_mode && available.default_provider && available.default, 'Official selected identity must be complete');
    Object.assign(payload, { provider: available.default_provider, model: available.default, apiMode: group.api_mode });
  }
  const started = Date.now();
  const events = [];
  let output = '';
  let failure;
  const result = await new Promise(resolvePromise => {
    const timer = setTimeout(() => { chat.emit('abort', { session_id: sessionId }); finish('timeout'); }, Number(options.get('run-timeout') || 90000));
    function handler(event, value) {
      if (value?.session_id && value.session_id !== sessionId) return;
      events.push({ event, ms: Date.now() - started });
      if (event === 'message.delta') output += String(value?.content || value?.delta || value?.text || '');
      if (event === 'run.completed') finish('completed');
      if (event === 'run.failed') {
        failure = failureCategory(value?.error);
        privateErrors.push({ agentId, mode, sessionId, error: value?.error });
        finish('failed');
      }
    }
    function finish(state) {
      clearTimeout(timer); chat.offAny(handler);
      resolvePromise({ state, failureCategory: failure || (state === 'timeout' ? 'timeout_after_abort_request' : undefined),
        elapsedMs: Date.now() - started, events, outputCharacters: [...output].length,
        started: events.some(value => value.event === 'run.started'), toolEvents: events.filter(value => /tool\./.test(value.event)).length });
    }
    chat.onAny(handler); chat.emit('run', payload);
  });
  const history = await request(`/api/studio/sessions/conversations/${encodeURIComponent(sessionId)}/messages/paginated${query}&offset=0&limit=12`);
  const assistantAnswerInHistory = Array.isArray(history.value.messages) && history.value.messages.some(message =>
    (message.display_role || message.role) === 'assistant' &&
    String(message.display_content ?? message.content ?? '').includes('连接成功'));
  const resultRecord = { agentId, mode, ...result, historyStatus: history.response.status,
    streamingAnswerMatched: output.includes('连接成功'), historyContainsAnswer: assistantAnswerInHistory,
    storedRoute: history.value.session ? { source: history.value.session.source, agent: history.value.session.agent,
      mode: history.value.session.agent_mode } : undefined, sessionId };
  // Session IDs are not credentials, but keep per-test identifiers out of the
  // public summary: the authenticated history is still private user data.
  delete resultRecord.sessionId;
  report.runs.push(resultRecord);
  assert.equal(result.state, 'completed', 'Official run did not complete');
  assert.equal(history.response.status, 200, 'Official history request failed');
  assert(output.includes('连接成功'), 'Expected actual streaming answer absent');
  assert(assistantAnswerInHistory, 'Expected assistant answer absent from history');
  return { agentId, elapsedMs: result.elapsedMs, toolEvents: result.toolEvents, historyStatus: history.response.status };
}

try {
  await test('MCU LAN login yields ordinary user JWT', async () => {
    const login = await request('/api/auth/mcu-login', { method: 'POST', body: {
      token: randomUUID(), id: manifest.instanceId, account: manifest.username, password: manifest.password,
    } });
    assert.equal(login.response.status, 200); assert.equal(login.value.relay.connected, false);
    jwt = login.value.token;
    const me = await request('/api/auth/me'); assert.equal(me.response.status, 200);
    const saved = { ...manifest, userJwt: jwt };
    await writeFile(options.get('manifest'), JSON.stringify(saved, null, 2), { mode: 0o600 });
    await chmod(options.get('manifest'), 0o600);
    return { loginStatus: 200, authMeStatus: 200, authenticatedRole: me.value.user?.role,
      relayConnected: false, profiles: login.value.profiles };
  });
  if (options.has('voice-seed')) await test('Import selected voice settings via official APIs', async () => {
    const seed = JSON.parse(await readFile(options.get('voice-seed'), 'utf8'));
    for (const kind of ['tts', 'stt']) {
      const value = seed[kind];
      const result = await request(`/api/studio/${kind}/settings/${encodeURIComponent(value.provider)}${query}`, { method: 'PUT', body: value });
      assert.equal(result.response.status, 200); assert.equal(result.value.activeProvider, value.activeProvider);
    }
    return { productionDatabaseCopied: false, providers: ['tts', 'stt'] };
  });
  await test('Official catalog and Studio selected model identity', async () => {
    const tools = await request('/api/coding-agents' + query, { timeout: 60000 });
    assert.equal(tools.response.status, 200); installed = tools.value.tools;
    const models = await request('/api/hermes/available-models' + query); assert.equal(models.response.status, 200); available = models.value;
    assert(Array.isArray(installed)); assert(Array.isArray(available.groups));
    const canonical = ['ekko-agent', 'hermes', 'claude-code', 'codex', 'pi', 'grok', 'opencode', 'dsh', 'cursor', 'antigravity', 'qwen', 'kimi', 'codebuddy', 'qoder', 'copilot', 'zcode'];
    const coding = canonical.slice(2);
    assert(coding.every(id => installed.some(tool => tool.id === id)), 'Official catalog missing canonical coding route');
    const secretReturned = available.groups.some(group => typeof group.api_key === 'string' && group.api_key.length > 0);
    if (secretReturned) report.findings.push({ severity: 'P1', confidence: 'high', issue: 'Official authorized model metadata returns provider secrets; firmware must discard, never persist or log them.' });
    report.catalog = canonical.map(id => ({ agentId: id, installed: id === 'ekko-agent' ? true : id === 'hermes' ? null : installed.find(tool => tool.id === id)?.installed === true,
      executionVerified: false }));
    return { canonicalRoutes: canonical.length, nativeCodingCatalog: coding.length,
      installedCodingAgents: installed.filter(tool => tool.installed).map(tool => tool.id), modelApiReturnsSecrets: secretReturned };
  });
  await test('Register isolated test device through native API', async () => {
    const result = await request('/api/mcu-devices', { method: 'POST', body: { name: 'Cardputer stock integration test', device_code: manifest.deviceCode }, timeout: 60000 });
    assert([201, 409].includes(result.response.status));
    const devices = await request('/api/mcu-devices');
    registeredDevice = devices.value.devices.find(device => device.device_code === manifest.deviceCode);
    assert(registeredDevice); assert.equal(registeredDevice.lan_connected, false);
    return { nativeRegistrationStatus: result.response.status, initiallyOnline: false };
  });
  await test('Two namespaces share one Engine.IO connection and report LAN online', async () => {
    chat = manager.socket('/chat-run', { auth: { token: jwt }, query: { profile } });
    // Namespace query is shared by Manager's Engine.IO transport, as it is on
    // the actual firmware. Auth.profile additionally scopes global-agent.
    manager.opts.query = { profile };
    status = manager.socket('/global-agent', { auth: { token: jwt, role: 'hermes-studio', profile,
      instanceId: manifest.instanceId, deviceCode: manifest.deviceCode } });
    await connect(chat); await connect(status);
    assert.equal(chat.io.engine, status.io.engine);
    if (registeredDevice) {
      const devices = await request('/api/mcu-devices');
      assert.equal(devices.value.devices.find(device => device.device_code === manifest.deviceCode)?.lan_connected, true);
    }
    return { chatConnected: chat.connected, managementConnected: status.connected, sharedEngine: true,
      managementOnlineVerified: Boolean(registeredDevice), mcuExtensionEventsSent: 0 };
  });
  for (const agentId of (options.get('run-agents') || 'ekko-agent').split(',').filter(Boolean)) {
    await test(`Actual Agent run: ${agentId}`, async () => {
      assert(chat?.connected && available, 'Chat prerequisites unavailable');
      if (agentId !== 'ekko-agent' && agentId !== 'hermes') assert(installed.some(tool => tool.id === agentId && tool.installed), 'CLI not installed');
      const result = await run(agentId);
      const catalog = report.catalog?.find(item => item.agentId === agentId); if (catalog) catalog.executionVerified = true;
      return result;
    });
  }
  if (options.get('voice') === 'yes') await test('Official TTS to STT roundtrip (host audio only)', async () => {
    const temporary = await mkdtemp(join(tmpdir(), 'cardputer-stock-voice-'));
    try {
      const audio = await request('/api/studio/tts/synthesize' + query, { method: 'POST', body: { text: '你好，连接成功。' }, timeout: 60000, json: false });
      assert.equal(audio.response.status, 200); assert(audio.value.length > 128);
      const input = join(temporary, 'tts.audio'); const output = join(temporary, 'stt.wav');
      await writeFile(input, audio.value, { mode: 0o600 });
      const conversion = spawnSync(options.get('ffmpeg') || 'ffmpeg', ['-v', 'error', '-y', '-i', input, '-ar', '16000', '-ac', '1', '-c:a', 'pcm_s16le', output], { encoding: 'utf8', timeout: 30000 });
      assert.equal(conversion.status, 0, 'Host audio conversion failed');
      const form = new FormData();
      form.append('response_format', 'text');
      form.append('file', new Blob([await readFile(output)], { type: 'audio/wav' }), 'test.wav');
      // The ordinary /stt/transcribe route requires an explicit provider. The
      // official voice proxy resolves the Profile's active provider itself;
      // this is also the real firmware's endpoint and multipart contract.
      const transcript = await request(`/api/studio/voice/proxy/${encodeURIComponent(profile)}/v1/audio/transcriptions${query}`,
        { method: 'POST', body: form, timeout: 60000, json: false });
      assert.equal(transcript.response.status, 200); assert(transcript.value.toString('utf8').includes('连接成功'));
      return { ttsStatus: 200, contentType: audio.response.headers.get('content-type'), audioBytes: audio.value.length,
        sttStatus: 200, expectedPhraseRecognized: true, physicalAudio: false, conversion: 'host ffmpeg; target decoder tested separately' };
    } finally { await rm(temporary, { recursive: true, force: true }); }
  });
  await test('Management disconnect clears LAN online', async () => {
    assert(status); status.disconnect();
    if (registeredDevice) {
      await new Promise(resolvePromise => setTimeout(resolvePromise, 150));
      const devices = await request('/api/mcu-devices');
      assert.equal(devices.value.devices.find(device => device.device_code === manifest.deviceCode)?.lan_connected, false);
    }
    return { managementOfflineVerified: Boolean(registeredDevice), chatStillConnected: chat?.connected === true };
  });
} finally {
  chat?.disconnect(); status?.disconnect(); manager._close();
  report.passed = report.tests.every(test => test.passed);
  await writeFile(options.get('report'), JSON.stringify(report, null, 2) + '\n', { mode: 0o600 });
  await chmod(options.get('report'), 0o600);
  if (options.has('private-errors')) {
    await writeFile(options.get('private-errors'), JSON.stringify(privateErrors, null, 2) + '\n', { mode: 0o600 });
    await chmod(options.get('private-errors'), 0o600);
  }
}
if (!report.passed) process.exitCode = 1;
