import test from 'node:test';
import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';
import { Readable } from 'node:stream';
import { createServer } from 'node:http';
import { readFile, writeFile, mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { spawnSync, fork } from 'node:child_process';
import { createHash } from 'node:crypto';
import { once } from 'node:events';
import { INPUT, validateAuth, parsePlan, selectProfileModel, installedAgents, inspectNative,
  buildPayload, historyProof, allowedReadPath, directReader, ownedRun, dailyAudit,
  readAuthStream, failureCategory, idleResume, hostIntegrity, parseAttemptedStatuses } from './stock_studio_daily.mjs';

const catalog = JSON.parse(await readFile(new URL('../../config/agents.json', import.meta.url))).agents;
const auth = { baseUrl: 'http://127.0.0.1:8748', userJwt: 'fixture.jwt.signature', profile: 'default', expectedUserId: 17 };
const route = id => ({ id, mode: 'native', source: 'coding_agent' });
const studio = { id: 'ekko-agent', mode: 'studio', source: 'builtin_agent' };
const modelBody = () => ({ profiles: [{ profile: 'default', default_provider: 'custom:test', default: 'exact-model',
  groups: [{ provider: 'custom:test', models: ['exact-model'], api_mode: 'chat_completions',
    api_key: 'PRIVATE_KEY_SENTINEL', base_url: 'https://private.invalid' }] }] });
function history(r, session = 'owned-session') {
  return { session: { id: session, profile: 'default', source: r.source, agent: r.id, agent_mode: r.mode === 'native' ? 'global' : 'scoped' },
    messages: [{ role: 'user', content: INPUT }, { role: 'assistant', content: '连接成功' }] };
}
class Socket extends EventEmitter {
  connected = true; listenersAny = new Set(); sent = []; behavior;
  constructor(behavior) { super(); this.behavior = behavior; }
  onAny(fn) { this.listenersAny.add(fn); }
  offAny(fn) { this.listenersAny.delete(fn); }
  deliver(name, body) { for (const listener of [...this.listenersAny]) listener(name, body); super.emit(name, body); }
  emit(name, body) { this.sent.push({ name, body }); this.behavior?.(name, body, this); return true; }
  connect() { this.connected = true; queueMicrotask(() => this.deliver('connect')); }
  disconnect() { this.connected = false; this.deliver('disconnect'); }
}
const complete = (name, body, socket) => {
  if (name !== 'run') return;
  queueMicrotask(() => {
    const fields = { session_id: body.session_id, queue_id: body.queue_id, run_id: 'owned-run' };
    socket.deliver('run.started', fields);
    socket.deliver('message.delta', { ...fields, content: '连接成' });
    socket.deliver('message.delta', { ...fields, content: '功' });
    socket.deliver('run.completed', fields);
  });
};
function requestFor(r, session = 'owned-session') {
  let calls = 0;
  return async () => ++calls === 1 ? { status: 404 } : { status: 200, body: history(r, session) };
}

test('stdin authentication is bounded, explicit and rejects untrusted origins/fields', async () => {
  assert.equal(validateAuth(auth).baseUrl, auth.baseUrl);
  for (const baseUrl of ['https://example.com', 'http://169.254.169.254', 'http://127.0.0.1/path',
    'http://user:pass@127.0.0.1', 'http://127.0.0.1/?x=1', 'http://127.0.0.1/#x'])
    assert.throws(() => validateAuth({ ...auth, baseUrl }));
  for (const expectedUserId of [0, -1, '17', NaN]) assert.throws(() => validateAuth({ ...auth, expectedUserId }));
  assert.throws(() => validateAuth({ ...auth, password: 'PRIVATE_PASSWORD' }));
  assert.throws(() => validateAuth({ ...auth, userJwt: 'bad token' }));
  assert.equal((await readAuthStream(Readable.from([JSON.stringify(auth)]))).profile, 'default');
  await assert.rejects(readAuthStream(Readable.from([Buffer.alloc(32769, 65)])));
  await assert.rejects(readAuthStream(Readable.from(['{truncated'])));
});

test('all canonical routes require an explicit supported mode; duplicate and Auto are rejected', () => {
  const spec = catalog.map(a => `${a.id}:${a.modes.includes('global') ? 'native' : 'studio'}`).join(',');
  const plan = parsePlan(spec, catalog); assert.equal(plan.length, 16);
  assert.equal(plan.find(r => r.id === 'hermes').source, 'cli');
  for (const spec of ['codex:auto', 'codex:native,codex:studio', 'cursor:studio', 'ekko-agent:native', 'other:native'])
    assert.throws(() => parsePlan(spec, catalog));
});

test('exact Profile model parsing retains scalar legacy shape and projects away secrets', () => {
  const model = selectProfileModel(modelBody(), 'default', studio);
  assert.deepEqual(model, { available: true, provider: 'custom:test', model: 'exact-model', apiMode: 'chat_completions', selectionOrigin: 'profile_default' });
  assert(!JSON.stringify(model).includes('PRIVATE') && !JSON.stringify(model).includes('private.invalid'));
  const raw = modelBody(); raw.groups = raw.profiles[0].groups; raw.default_provider = 'wrong';
  assert.equal(selectProfileModel(raw, 'absent', studio).reason, 'profile_unavailable');
  raw.profiles.push(structuredClone(raw.profiles[0])); assert.equal(selectProfileModel(raw, 'default', studio).available, false);
});

test('actual signed API shape may omit the OAuth default group; official compatible default remains selectable', () => {
  const raw = { default_provider: 'ignored-top-level', groups: [], allProviders: [{ provider: 'openai-codex' }],
    profiles: [{ profile: 'default', default_provider: 'openai-codex', default: 'shared-model', groups: [
      { provider: 'custom:first', models: ['shared-model', 'other-model'], api_mode: 'codex_responses',
        credential_configured: true, api_key: 'PRIVATE_KEY_SENTINEL', base_url: 'https://private.invalid',
        model_meta: { 'shared-model': { reasoning: true, reasoning_efforts: ['low', 'high'] } } },
      { provider: 'custom:later', models: ['other-model'], api_mode_by_model: { 'other-model': 'anthropic_messages' } },
    ] }] };
  for (const r of [studio, { id: 'hermes', mode: 'studio', source: 'cli' }, { ...route('claude-code'), mode: 'studio' }]) {
    const selected = selectProfileModel(raw, 'default', r);
    assert.equal(selected.available, true);
    assert.equal(selected.provider, 'custom:first'); assert.equal(selected.model, 'shared-model');
    assert.equal(selected.apiMode, 'codex_responses'); assert.equal(selected.selectionOrigin, 'compatible_default');
    assert(!JSON.stringify(selected).includes('PRIVATE') && !JSON.stringify(selected).includes('private.invalid'));
  }
});

test('freshness uses the official local session detail, avoiding the absent-session bridge-history 500', async () => {
  const r = route('codex'), socket = new Socket(complete), paths = [];
  const result = await ownedRun({ socket, route: r, profile: 'default', sessionId: 'owned-session', queueId: 'owned-queue',
    request: async path => {
      paths.push(path);
      if (path === '/api/studio/sessions/owned-session?profile=default') return { status: 404 };
      // A paginated lookup of a nonexistent session can enter the official
      // Hermes bridge and return 500. It is not an existence check.
      if (!socket.sent.some(e => e.name === 'run')) return { status: 500 };
      return { status: 200, body: history(r) };
    } });
  assert.equal(result.executionVerified, true);
  assert.equal(paths[0], '/api/studio/sessions/owned-session?profile=default');
  assert.equal(socket.sent.filter(e => e.name === 'run').length, 1);
});

test('per-model mode is authoritative, including missing/null/malformed map entries', () => {
  const raw = modelBody(), group = raw.profiles[0].groups[0];
  group.api_mode = null; group.api_mode_by_model = { 'exact-model': 'anthropic_messages' };
  assert.equal(selectProfileModel(raw, 'default', studio).apiMode, 'anthropic_messages');
  for (const map of [{}, null, [], { 'exact-model': null }, { 'exact-model': true }, { 'exact-model': 'guessed_mode' },
    { 'exact-model': 'anthropic_messages', ['x'.repeat(513)]: null }, { 'exact-model': 'chat_completions\0' }]) {
    group.api_mode = 'chat_completions'; group.api_mode_by_model = map;
    assert.equal(selectProfileModel(raw, 'default', studio).available, false);
  }
  delete group.api_mode_by_model;
  for (const model of [null, undefined, 'x\0y', 'x'.repeat(513)]) { raw.profiles[0].default = model; assert.equal(selectProfileModel(raw, 'default', studio).available, false); }
  raw.profiles[0].default = '';
  assert.equal(selectProfileModel(raw, 'default', studio).selectionOrigin, 'compatible_default');
});

test('official compatible defaults preserve order and never cross Profiles, enabled default groups or provider restrictions', () => {
  for (const mutation of [raw => { raw.profiles[0].default = 'missing'; }, raw => { raw.profiles[0].default_provider = 'missing'; }]) {
    const raw = modelBody(); mutation(raw);
    const selected = selectProfileModel(raw, 'default', studio);
    assert.equal(selected.available, true); assert.equal(selected.selectionOrigin, 'compatible_default');
    assert.equal(selected.provider, 'custom:test'); assert.equal(selected.model, 'exact-model');
  }
  for (const mutation of [raw => { raw.profiles[0].groups[0].model_meta = { 'exact-model': { disabled: true } }; },
    raw => { raw.profiles[0].groups[0].model_meta = { 'exact-model': { hidden: true } }; }]) {
    const raw = modelBody(); mutation(raw);
    raw.profiles[0].groups.push({ provider: 'custom:later', models: ['later'], api_mode: 'chat_completions' });
    assert.equal(selectProfileModel(raw, 'default', studio).available, false);
  }
  const raw = modelBody(); raw.profiles[0].default_provider = raw.profiles[0].groups[0].provider = 'openai-codex';
  assert.equal(selectProfileModel(raw, 'default', route('codex')).reason, 'default_model_unavailable');
  assert.equal(selectProfileModel(raw, 'default', studio).available, true);
  raw.profiles[0].groups.push({ provider: 'moa', models: ['exact-model'], api_mode: 'codex_responses' },
    { provider: 'custom:first', models: ['different', 'exact-model'], api_mode: 'anthropic_messages' },
    { provider: 'custom:later', models: ['exact-model'], api_mode: 'codex_responses' });
  const selected = selectProfileModel(raw, 'default', route('codex'));
  assert.equal(selected.provider, 'custom:first'); assert.equal(selected.model, 'exact-model');
  assert.equal(selected.selectionOrigin, 'compatible_default');
  assert.equal(selectProfileModel(raw, 'absent', studio).available, false);
  const other = structuredClone(raw.profiles[0]); other.profile = 'other';
  raw.profiles[0].groups = []; raw.profiles.push(other);
  assert.equal(selectProfileModel(raw, 'default', route('codex')).available, false);
});

test('compatible model metadata still rejects malformed identities, flags, maps and ambiguous defaults', () => {
  for (const mutate of [p => { delete p.default_provider; }, p => { p.default_provider = null; },
    p => { p.default_provider = 'x'.repeat(257); }, p => { p.default_provider = 'bad\0identity'; },
    p => { p.groups[0].models = [null]; }, p => { p.groups[0].model_meta = null; },
    p => { p.groups[0].model_meta = { model: { disabled: 'true' } }; },
    p => { p.groups[0].model_meta = { model: { hidden: null } }; },
    p => { p.groups[0].api_mode = false; }, p => { p.groups[0].api_mode_by_model = [] },
    p => { p.groups.push(structuredClone(p.groups[0])); }]) {
    const raw = modelBody(); mutate(raw.profiles[0]); assert.equal(selectProfileModel(raw, 'default', studio).available, false);
  }
  const raw = modelBody(); raw.profiles[0].groups[0].models.unshift('hidden', 'disabled');
  raw.profiles[0].default = 'absent';
  raw.profiles[0].groups[0].model_meta = { hidden: { hidden: true }, disabled: { disabled: true } };
  assert.equal(selectProfileModel(raw, 'default', studio).model, 'exact-model');
});

test('fresh session existence check fails closed on every non-404 status and cannot use history 404 as permission', async () => {
  for (const status of [200, 204, 302, 400, 401, 403, 500, 502]) {
    const socket = new Socket(complete);
    const result = await ownedRun({ socket, route: route('codex'), profile: 'default', sessionId: 'owned-session', queueId: 'owned-queue',
      request: async path => path.includes('/conversations/') ? { status: 404 } : { status } });
    assert.equal(result.state, 'not_sent'); assert.equal(result.executionVerified, false);
    assert.equal(result.freshSessionStatus, status); assert.equal(socket.sent.length, 0);
  }
  for (const path of ['/api/studio/sessions/owned-session?profile=default',
    '/api/studio/sessions/conversations/owned-session/messages/paginated?profile=default&offset=0&limit=12'])
    assert.equal(allowedReadPath(path), true);
  for (const path of ['/api/studio/sessions/../config?profile=default', '/api/studio/sessions/id?profile=default&write=true',
    '/api/studio/sessions/id/export?profile=default']) assert.equal(allowedReadPath(path), false);
});

test('Native distinguishes firmware preflight from configured execution policy', () => {
  const installed = installedAgents({ tools: [{ id: 'codex', installed: true, secret: 'PRIVATE_KEY' }] }); assert.equal(installed.get('codex'), true);
  assert.throws(() => installedAgents({ tools: [{ id: 'codex', installed: 'true' }] }));
  assert.throws(() => installedAgents({ tools: [{ id: 'codex', installed: true }, { id: 'codex', installed: true }] }));
  for (const status of ['ready', 'empty', 'unsupported', 'not_installed', 'auth_required', 'timeout', 'error']) {
    const result = inspectNative({ agents: [{ agentId: 'codex', status, models: [] }] }, 'codex', true);
    assert.equal(result.available, ['ready', 'empty'].includes(status));
    assert.equal(result.firmwarePreflightAccepts, ['ready', 'empty', 'unsupported', 'auth_required', 'error'].includes(status));
  }
  assert.equal(inspectNative({ agents: [{ agentId: 'codex', status: 'ready', models: [] }] }, 'codex', false).available, false);
  assert.equal(inspectNative({ agents: [{ agentId: 'other', status: 'ready', models: [] }] }, 'codex', true).status, 'invalid');
  const payload = buildPayload(route('codex'), 'default', { api_key: 'PRIVATE' }, 's', 'q');
  assert.deepEqual(Object.keys(payload).sort(), ['coding_agent_id', 'input', 'mode', 'profile', 'queue_id', 'session_id', 'source']);
});

test('Native metadata error/auth probes require explicit enum opt-in; timeout/uninstalled remain blocked', () => {
  assert.deepEqual([...parseAttemptedStatuses()], ['ready', 'empty']);
  const optedIn = parseAttemptedStatuses('ready,empty,auth_required,error');
  for (const status of ['auth_required', 'error']) {
    const raw = { agents: [{ agentId: 'qoder', status, models: [] }] };
    assert.equal(inspectNative(raw, 'qoder', true).available, false);
    const probe = inspectNative(raw, 'qoder', true, optedIn);
    assert.equal(probe.status, status); assert.equal(probe.available, true); assert.equal(probe.firmwarePreflightAccepts, true);
  }
  for (const spec of ['', 'ready,timeout', 'not_installed', 'READY', 'ready,error,error', 'ready,unknown', ' ready', 'auth_required,'])
    assert.throws(() => parseAttemptedStatuses(spec));
  for (const status of ['timeout', 'not_installed']) assert.equal(inspectNative({ agents: [
    { agentId: 'qoder', status, models: [] }] }, 'qoder', true, new Set([status])).available, false);
});

test('alpha5 explicit Native default admission is independent of the paid-probe policy and requires the whole catalog', () => {
  for (const status of ['auth_required', 'error']) {
    const raw = { agents: [{ agentId: 'cursor', status, models: [] }] };
    const defaultPolicy = inspectNative(raw, 'cursor', true);
    assert.equal(defaultPolicy.status, status);
    assert.equal(defaultPolicy.firmwarePreflightAccepts, true);
    assert.equal(defaultPolicy.available, false); // No implicit paid probe.
    const opted = inspectNative(raw, 'cursor', true, parseAttemptedStatuses('auth_required,error'));
    assert.equal(opted.firmwarePreflightAccepts, true); assert.equal(opted.available, true);
    assert.equal(inspectNative(raw, 'cursor', false).firmwarePreflightAccepts, false);
    for (const other of [{ agentId: 'other', status: 'ready', models: null },
      { agentId: 'other', status: 'unknown', models: [] },
      { agentId: 'other', status: 'ready', models: [{ id: 'm', hidden: null }] },
      { agentId: 'other\0bad', status: 'ready', models: [] },
      structuredClone(raw.agents[0])]) {
      const malformed = inspectNative({ agents: [...raw.agents, other] }, 'cursor', true,
        parseAttemptedStatuses('auth_required,error'));
      assert.equal(malformed.status, 'invalid'); assert.equal(malformed.available, false);
      assert.equal(malformed.firmwarePreflightAccepts, false);
    }
  }
});

test('explicit Native probe preserves catalog failure and does not bypass auth/me or models HTTP authorization', async () => {
  const r = route('qoder'), plan = [r];
  for (const metadata of ['auth_required', 'error', 401, 403]) {
    const socket = new Socket(complete); let historyCalls = 0;
    const request = async path => {
      if (path === '/health') return { status: 200, body: { webui_version: '0.8.24' } };
      if (path === '/api/auth/me') return { status: 200, body: { user: { id: 17, role: 'user' } } };
      if (path.startsWith('/api/coding-agents?')) return { status: 200, body: { tools: [{ id: 'qoder', installed: true }] } };
      if (path.includes('/models?')) return typeof metadata === 'number' ? { status: metadata } :
        { status: 200, body: { agents: [{ agentId: 'qoder', status: metadata, models: [] }] } };
      if (++historyCalls === 1) return { status: 404 };
      const sessionId = new URL(path, 'http://local').pathname.split('/')[path.includes('/conversations/') ? 5 : 4]; return { status: 200, body: history(r, sessionId) };
    };
    const report = await dailyAudit({ auth, plan, request, socketFactory: () => socket,
      attempted: parseAttemptedStatuses('ready,empty,auth_required,error') });
    if (typeof metadata === 'number') {
      assert.equal(report.runs[0].preflight, 'http_error'); assert.equal(report.runs[0].executionAttempted, false);
      assert.equal(socket.sent.length, 0);
    } else {
      assert.equal(report.runs[0].preflight, metadata); assert.equal(report.runs[0].firmwarePreflightAccepts, true);
      assert.equal(report.runs[0].operatorRequestedProbeBeyondFirmwarePreflight, false);
      assert.equal(report.runs[0].operatorRequestedProbeBeyondDefaultToolPolicy, true);
      assert.equal(report.runs[0].executionVerified, true);
      assert.equal(socket.sent.filter(e => e.name === 'run').length, 1);
    }
  }
  await assert.rejects(dailyAudit({ auth, plan, attempted: parseAttemptedStatuses('auth_required,error'),
    request: async path => path === '/health' ? { status: 200, body: { webui_version: '0.8.24' } } : { status: 401 },
    socketFactory: () => assert.fail('Unauthorized JWT must not create a socket') }));
});

test('history requires the exact session/Profile/Agent/mode and persisted input and assistant', () => {
  const r = route('codex'); assert.deepEqual(historyProof(history(r), r, 'default', 'owned-session'),
    { routeMatched: true, inputMatched: true, answerMatched: true });
  for (const key of ['id', 'profile', 'source', 'agent', 'agent_mode']) {
    const h = history(r); h.session[key] = 'foreign'; assert.equal(historyProof(h, r, 'default', 'owned-session').routeMatched, false);
  }
  const h = history(r); h.messages[0].content = 'other input'; assert.equal(historyProof(h, r, 'default', 'owned-session').inputMatched, false);
  const hermes = { id: 'hermes', mode: 'studio', source: 'cli' }, hh = history(hermes); hh.session.agent = '';
  assert.equal(historyProof(hh, hermes, 'default', 'owned-session').routeMatched, true);
});

test('only the canonical Claude route accepts the official stored claude alias; scope remains exact', () => {
  const r = { ...route('claude-code'), mode: 'studio' }, h = history(r);
  h.session.agent = 'claude';
  assert.deepEqual(historyProof(h, r, 'default', 'owned-session'),
    { routeMatched: true, inputMatched: true, answerMatched: true });
  for (const changed of [{ profile: 'other' }, { id: 'other' }, { source: 'cli' }, { agent_mode: 'global' },
    { agent: 'Claude' }, { agent: 'claude_code' }, { agent: 'claude-code-other' }]) {
    assert.equal(historyProof({ ...h, session: { ...h.session, ...changed } }, r, 'default', 'owned-session').routeMatched, false);
  }
  for (const id of ['codex', 'hermes', 'ekko-agent', 'cursor'])
    assert.equal(historyProof(h, { ...r, id }, 'default', 'owned-session').routeMatched, false);
  const native = { ...r, mode: 'native' }, nh = history(native); nh.session.agent = 'claude';
  assert.equal(historyProof(nh, native, 'default', 'owned-session').routeMatched, true);
  assert.equal(historyProof(nh, r, 'default', 'owned-session').routeMatched, false);
});

test('owned run ignores foreign/missing session and wrong queue; no false completion from stale events', async () => {
  const r = route('codex');
  const socket = new Socket((name, body, s) => {
    if (name !== 'run') return;
    s.deliver('run.completed', {});
    s.deliver('run.completed', { session_id: body.session_id, queue_id: 'foreign' });
    s.deliver('run.failed', { session_id: 'foreign', error: 'PRIVATE_KEY' });
    complete(name, body, s);
  });
  const result = await ownedRun({ socket, request: requestFor(r), route: r, profile: 'default', sessionId: 'owned-session', queueId: 'owned-queue' });
  assert.equal(result.executionVerified, true); assert.equal(result.queueCorrelated, true);
  assert.equal(socket.sent.filter(e => e.name === 'run').length, 1);
  assert.equal(socket.listenersAny.size, 0); assert.equal(socket.listenerCount('disconnect'), 0);
});

test('Native run-only events require a pinned run and exact durable history', async () => {
  const r = route('codex');
  const socket = new Socket((name, body, s) => {
    if (name !== 'run') return;
    queueMicrotask(() => { const value = { session_id: body.session_id, run_id: 'native-run' };
      s.deliver('run.started', value); s.deliver('run.completed', { ...value, run_id: 'other-run' }); s.deliver('run.completed', value); });
  });
  const result = await ownedRun({ socket, request: requestFor(r), route: r, profile: 'default', sessionId: 'owned-session', queueId: 'owned-queue' });
  assert.equal(result.executionVerified, true); assert.equal(result.queueCorrelated, false);
  assert.equal(result.streamingAnswerMatched, false); // Persistent proof, not invented streaming.
});

test('timeout aborts only the fresh owned session, then verifies idle without resending', async () => {
  const r = route('codex');
  const socket = new Socket((name, body, s) => {
    if (name === 'resume') queueMicrotask(() => {
      const idle = { session_id: body.session_id, isWorking: false, isAborting: false, queueLength: 0, queueMessages: [], queueInsertion: null };
      s.deliver('resumed', { ...idle, session_id: 'foreign' });
      s.deliver('resumed', { ...idle, queueLength: 1, queueMessages: [{ queue_id: 'owned-queue' }] });
      s.deliver('resumed', idle);
    });
  });
  const result = await ownedRun({ socket, request: requestFor(r), route: r, profile: 'default', sessionId: 'owned-session', queueId: 'owned-queue', timeoutMs: 5, settleMs: 30 });
  assert.equal(result.executionVerified, false); assert.equal(result.stoppedAfterAbort, true); assert.equal(result.safeToContinue, true);
  assert.equal(socket.sent.filter(e => e.name === 'run').length, 1);
  assert.deepEqual(socket.sent.filter(e => e.name === 'abort'), [{ name: 'abort', body: { session_id: 'owned-session' } }]);
  assert.equal(socket.listenerCount('resumed'), 0);
});

test('idle confirmation rejects working false with an abort, queued input, insertion or background pending', () => {
  const idle = { session_id: 'owned-session', isWorking: false, isAborting: false,
    queueLength: 0, queueMessages: [], queueInsertion: null, backgroundPending: 0 };
  assert.equal(idleResume(idle, 'owned-session'), true);
  for (const fields of [{ isAborting: true }, { queueLength: 1 }, { queueMessages: [{}] },
    { queueInsertion: {} }, { backgroundPending: 1 }, { queueLength: '0' }, { isWorking: true }, { session_id: 'foreign' }])
    assert.equal(idleResume({ ...idle, ...fields }, 'owned-session'), false);
  assert.equal(idleResume({ session_id: 'owned-session', isWorking: false }, 'owned-session'), false);
});

test('existing session is never used, sent to, or aborted; unconfirmed stop blocks subsequent work', async () => {
  const r = route('codex'), socket = new Socket();
  const collision = await ownedRun({ socket, request: async () => ({ status: 200 }), route: r, profile: 'default', sessionId: 'existing', queueId: 'owned-queue' });
  assert.equal(collision.state, 'not_sent'); assert.equal(socket.sent.length, 0);
  const timeout = await ownedRun({ socket, request: requestFor(r), route: r, profile: 'default', sessionId: 'owned-session', queueId: 'owned-queue', timeoutMs: 5, settleMs: 5 });
  assert.equal(timeout.safeToContinue, false); assert.equal(timeout.executionVerified, false);
});

test('transport failures preserve uncertainty and never trigger a duplicate run', async () => {
  const r = route('codex');
  const unused = new Socket();
  const preflight = await ownedRun({ socket: unused, request: async () => { throw Error('PRIVATE_TRANSPORT_ERROR'); },
    route: r, profile: 'default', sessionId: 'owned-session', queueId: 'owned-queue' });
  assert.equal(preflight.executionVerified, false); assert.equal(preflight.state, 'not_sent'); assert.equal(unused.sent.length, 0);
  const disconnected = new Socket((name, body, s) => {
    if (name === 'run') queueMicrotask(() => s.disconnect());
  });
  const result = await ownedRun({ socket: disconnected, request: requestFor(r), route: r,
    profile: 'default', sessionId: 'owned-session', queueId: 'owned-queue', sleep: async () => {} });
  assert.equal(result.state, 'uncertain'); assert.equal(result.safeToContinue, false);
  assert.equal(result.executionVerified, false); assert.equal(result.abortSent, false);
  assert.equal(disconnected.sent.filter(e => e.name === 'run').length, 1);
  assert.equal(disconnected.listenersAny.size, 0);
  const socket = new Socket(complete); let reads = 0;
  const historyError = await ownedRun({ socket, request: async () => {
    if (++reads === 1) return { status: 404 }; throw Error('PRIVATE_HISTORY_ERROR');
  }, route: r, profile: 'default', sessionId: 'owned-session', queueId: 'owned-queue' });
  assert.equal(historyError.state, 'completed'); assert.equal(historyError.executionVerified, false);
  assert.equal(historyError.historyStatus, 0); assert.equal(historyError.safeToContinue, true);
  assert(!JSON.stringify(historyError).includes('PRIVATE'));
});

test('failed run exposes a fixed category and cannot be promoted by an old persisted answer', async () => {
  const r = route('codex'), socket = new Socket((name, body, s) => {
    if (name === 'run') queueMicrotask(() => s.deliver('run.failed', {
      session_id: body.session_id, queue_id: body.queue_id, error: 'PRIVATE_KEY_SECRET HTTP 429' }));
  });
  const result = await ownedRun({ socket, request: requestFor(r), route: r,
    profile: 'default', sessionId: 'owned-session', queueId: 'owned-queue' });
  assert.equal(result.state, 'failed'); assert.equal(result.reason, 'quota_or_rate_limit');
  assert.equal(result.executionVerified, false); assert.equal(result.history.answerMatched, true);
  assert(!JSON.stringify(result).includes('PRIVATE'));
});

test('daily orchestration uses GET allowlist, no settings/login/hardware route, and safe report only', async () => {
  const plan = [studio, route('codex'), route('qwen')], paths = [];
  const socket = new Socket(complete); let session = 0; const histories = new Map();
  const request = async path => {
    paths.push(path); assert(allowedReadPath(path));
    if (path === '/health') return { status: 200, body: { webui_version: '0.8.24' } };
    if (path === '/api/auth/me') return { status: 200, body: { user: { id: 17, role: 'super_admin', password: 'PRIVATE_PASSWORD' } } };
    if (path.startsWith('/api/hermes/')) return { status: 200, body: modelBody() };
    if (path.startsWith('/api/coding-agents?')) return { status: 200, body: { tools: [{ id: 'codex', installed: true }, { id: 'qwen', installed: true }] } };
    if (path.includes('/models?')) { const id = new URL('http://local' + path).searchParams.get('agent'); return { status: 200, body: { agents: [{ agentId: id, status: id === 'qwen' ? 'auth_required' : 'empty', models: [] }] } }; }
    const id = new URL(path, 'http://local').pathname.split('/')[path.includes('/conversations/') ? 5 : 4];
    if (!histories.has(id)) { histories.set(id, ++session === 1 ? studio : route('codex')); return { status: 404 }; }
    return { status: 200, body: history(histories.get(id), id) };
  };
  const report = await dailyAudit({ auth, plan, request, socketFactory: () => socket });
  assert.equal(report.verifiedCount, 2); assert.equal(report.allRequestedExecuted, false);
  assert.equal(report.runs[2].preflight, 'auth_required'); assert.equal(report.runs[2].executionAttempted, false);
  assert.equal(report.runs[2].firmwarePreflightAccepts, true); // Tool policy still declines a paid probe.
  assert.equal(socket.sent.filter(e => e.name === 'run').length, 2);
  assert.equal(report.physicalDeviceTest, false); assert.equal(report.lowPrivilegeAccountTest, false);
  const safe = JSON.stringify(report);
  for (const secret of ['PRIVATE', 'fixture.jwt', INPUT, 'custom:test', 'exact-model', 'session_id']) assert(!safe.includes(secret));
  assert(socket.sent.every(e => ['run', 'abort', 'resume'].includes(e.name)));
  for (const path of ['/api/auth/mcu-login', '/api/mcu-devices', '/api/studio/tts/settings/x', '//foreign.example/health']) assert.equal(allowedReadPath(path), false);
});

test('direct production reader never follows redirects or writes and rejects oversized/malformed body', async t => {
  let visited = 0, method;
  const server = createServer((req, res) => { ++visited; method = req.method;
    if (visited === 1) { res.writeHead(302, { Location: 'http://127.0.0.1:1/secret' }); res.end(); }
    else if (visited === 2) { res.writeHead(200, { 'content-type': 'application/json' }); res.end('{broken'); }
    else if (visited === 3) { res.writeHead(200, { 'content-type': 'application/json' }); res.end(' '.repeat(2 * 1024 * 1024 + 1)); }
    else { res.writeHead(200, { 'content-type': 'application/json' }); res.flushHeaders(); }
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve)); t.after(() => new Promise(resolve => server.close(resolve)));
  const reader = directReader({ ...auth, baseUrl: `http://127.0.0.1:${server.address().port}` }, 1000);
  assert.deepEqual(await reader('/health'), { status: 302 }); assert.equal(visited, 1); assert.equal(method, 'GET');
  await assert.rejects(reader('/health'));
  await assert.rejects(reader('/api/auth/login')); assert.equal(visited, 2);
  await assert.rejects(reader('/health'));
  const deadline = directReader({ ...auth, baseUrl: `http://127.0.0.1:${server.address().port}` }, 15);
  await assert.rejects(deadline('/health'));
});

test('daily CLI rejects mutating legacy options before stdin/network, without leaking supplied credentials', () => {
  const result = spawnSync(process.execPath, [new URL('./stock_studio_validation.mjs', import.meta.url).pathname,
    '--mode', 'daily-supplement', '--voice-seed', 'not-used'], { input: JSON.stringify(auth), encoding: 'utf8' });
  assert.equal(result.status, 1); assert(!result.stdout.includes('fixture') && !result.stderr.includes('fixture'));
  assert.equal(failureCategory('PRIVATE_KEY_SENTINEL 429'), 'quota_or_rate_limit');
  assert.equal(failureCategory('PLUGIN_SCHEMA PRIVATE_KEY_SENTINEL'), 'host_plugin_schema');
});

test('an existing report is rejected before importing transport or making a paid request', async t => {
  const directory = await mkdtemp(join(tmpdir(), 'cardputer-daily-safe-')); t.after(() => rm(directory, { recursive: true, force: true }));
  const file = join(directory, 'report.json'); await writeFile(file, 'EXISTING_PUBLIC_REPORT', { mode: 0o600 });
  const result = spawnSync(process.execPath, [new URL('./stock_studio_validation.mjs', import.meta.url).pathname,
    '--mode', 'daily-supplement', '--modules', '/does-not-exist', '--report', file,
    '--agent-modes', 'codex:native'], { input: JSON.stringify(auth), encoding: 'utf8' });
  assert.equal(result.status, 1); assert.equal(await readFile(file, 'utf8'), 'EXISTING_PUBLIC_REPORT');
  assert(!result.stderr.includes('fixture.jwt') && !result.stderr.includes('/does-not-exist'));
});

test('server SHA gate is explicit, records provenance, and rejects changes before any request', async t => {
  const directory = await mkdtemp(join(tmpdir(), 'cardputer-entry-integrity-')); t.after(() => rm(directory, { recursive: true, force: true }));
  const entry = join(directory, 'index.js'), bytes = Buffer.from('fixture official backend'); await writeFile(entry, bytes);
  const expected = createHash('sha256').update(bytes).digest('hex');
  const options = new Map([['server-entry', entry], ['expected-server-sha', expected]]);
  const result = await hostIntegrity(options);
  assert.equal(result.matchesExpected, true); assert.equal(result.expectedSource, 'operator_supplied');
  assert.equal(result.unmodifiedRuntimeVerified, false); assert(!JSON.stringify(result).includes(directory));
  await writeFile(entry, 'modified fixture backend');
  const changed = await hostIntegrity(options); assert.equal(changed.matchesExpected, false);
  const report = await dailyAudit({ auth, plan: [route('codex')], request: () => assert.fail('Gate must run before HTTP'),
    socketFactory: () => assert.fail('Gate must run before sockets'), verifyHost: () => hostIntegrity(options) });
  assert.equal(report.stoppedBecauseHostIntegrity, true); assert.equal(report.executedCount, 0);
  await assert.rejects(hostIntegrity(new Map([['server-entry', entry]])));
  assert.deepEqual(await hostIntegrity(new Map()), { checked: false, unmodifiedRuntimeVerified: false });
});

test('mid-run cancellation aborts once, confirms its exact idle scope, and does not read history or resend', async () => {
  const controller = new AbortController(), r = route('codex'); let reads = 0;
  const socket = new Socket((name, body, s) => {
    if (name === 'run') queueMicrotask(() => controller.abort());
    if (name === 'resume') queueMicrotask(() => s.deliver('resumed', { session_id: body.session_id,
      isWorking: false, isAborting: false, queueLength: 0, queueMessages: [], queueInsertion: null }));
  });
  const result = await ownedRun({ socket, request: async () => { ++reads; return { status: 404 }; }, route: r,
    profile: 'default', sessionId: 'owned-session', queueId: 'owned-queue', signal: controller.signal, settleMs: 50 });
  assert.equal(result.state, 'interrupted'); assert.equal(result.abortSent, true); assert.equal(result.stoppedAfterAbort, true);
  assert.equal(result.executionVerified, false); assert.equal(reads, 1);
  assert.deepEqual(socket.sent.map(e => e.name), ['run', 'abort', 'resume']);
  const before = await ownedRun({ socket, request: () => assert.fail('No request after pre-cancel'), route: r,
    profile: 'default', sessionId: 'other-owned', queueId: 'other-queue', signal: controller.signal });
  assert.equal(before.state, 'not_sent');
});

for (const signalName of ['SIGINT', 'SIGTERM']) test(`actual daily CLI ${signalName} safely aborts its synthetic current run and finalizes a sanitized report`, async t => {
  const directory = await mkdtemp(join(tmpdir(), 'cardputer-signal-')); t.after(() => rm(directory, { recursive: true, force: true }));
  const packageDir = join(directory, 'node_modules/socket.io-client');
  const { mkdir } = await import('node:fs/promises'); await mkdir(packageDir, { recursive: true });
  await writeFile(join(directory, 'package.json'), '{}');
  await writeFile(join(packageDir, 'package.json'), JSON.stringify({ name: 'socket.io-client', main: 'index.cjs' }));
  await writeFile(join(packageDir, 'index.cjs'), `
const {EventEmitter}=require('node:events');
class Socket extends EventEmitter {
 constructor(){super();this.connected=true;this.any=new Set();}
 onAny(fn){this.any.add(fn)} offAny(fn){this.any.delete(fn)}
 connect(){queueMicrotask(()=>super.emit('connect'))}
 disconnect(){this.connected=false;super.emit('disconnect')}
 emit(name,body){
  process.send?.({kind:name});
  if(name==='resume')queueMicrotask(()=>super.emit('resumed',{session_id:body.session_id,isWorking:false,isAborting:false,queueLength:0,queueMessages:[],queueInsertion:null}));
  return true;
 }
}
exports.Manager=class{socket(namespace){if(namespace!=='/chat-run')throw Error('Unexpected namespace');return new Socket()} _close(){}};
`);
  const server = createServer((req, res) => {
    assert.equal(req.method, 'GET'); res.setHeader('content-type', 'application/json');
    if (req.url === '/health') res.end(JSON.stringify({ webui_version: '0.8.24' }));
    else if (req.url === '/api/auth/me') res.end(JSON.stringify({ user: { id: 17, role: 'user' } }));
    else if (req.url.startsWith('/api/coding-agents?')) res.end(JSON.stringify({ tools: [{ id: 'codex', installed: true }] }));
    else if (req.url.startsWith('/api/coding-agents/models?')) res.end(JSON.stringify({ agents: [{ agentId: 'codex', status: 'ready', models: [] }] }));
    else { res.statusCode = 404; res.end('{}'); }
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve)); t.after(() => new Promise(resolve => server.close(resolve)));
  const reportPath = join(directory, 'report.json');
  const child = fork(new URL('./stock_studio_validation.mjs', import.meta.url), ['--mode', 'daily-supplement',
    '--modules', directory, '--report', reportPath, '--agent-modes', 'codex:native'], { silent: true });
  t.after(() => { if (child.exitCode === null && child.signalCode === null) child.kill('SIGKILL'); });
  let stdout = '', stderr = '', runs = 0, aborts = 0;
  child.stdout.on('data', b => { stdout += b; }); child.stderr.on('data', b => { stderr += b; });
  child.on('message', value => {
    if (value.kind === 'run') { ++runs; child.kill(signalName); }
    if (value.kind === 'abort') ++aborts;
  });
  child.stdin.end(JSON.stringify({ ...auth, baseUrl: `http://127.0.0.1:${server.address().port}` }));
  const [code, signal] = await Promise.race([once(child, 'exit'), new Promise((_, reject) => { const timer = setTimeout(() => reject(Error('Synthetic signal test timed out')), 5000); timer.unref(); })]);
  assert.equal(code, 1); assert.equal(signal, null); assert.equal(runs, 1); assert.equal(aborts, 1);
  const report = JSON.parse(await readFile(reportPath, 'utf8'));
  assert.equal(report.interrupted, true); assert.equal(report.runs[0].state, 'interrupted');
  assert.equal(report.runs[0].stoppedAfterAbort, true); assert.equal(report.verifiedCount, 0);
  assert(!stdout.includes('fixture.jwt') && !stderr.includes('fixture.jwt') && !JSON.stringify(report).includes('fixture.jwt'));
});
