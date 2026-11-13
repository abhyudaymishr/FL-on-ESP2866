// Pure domain rules. No DOM, network, storage, clocks, or framework dependencies.
export const LIMITS = Object.freeze({nodes: 12, gateways: 1, bodyBytes: 32768,
  historySlots: 1440, nodeFreshMs: 6000, modelFreshMs: 15000, gatewayFreshMs: 90000,
  pollMs: 30000, pushMs: 30000});
export const SCHEMA_VERSION = 1;
export const GATEWAY_ID = /^[a-z0-9][a-z0-9-]{0,31}$/;
export class ValidationError extends Error {}
const fail = message => { throw new ValidationError(message); };
const object = x => x !== null && typeof x === 'object' && !Array.isArray(x);
const num = (x, min, max) => typeof x === 'number' && Number.isFinite(x) && x >= min && x <= max;
const integer = (x, min, max) => Number.isInteger(x) && num(x, min, max);
const time = (x, capturedAt) => x === null || integer(x, 0, capturedAt);
function model(x) {
  if (!object(x) || !num(x.w1, -2, 2) || !num(x.w2, -.5, .5) || !num(x.b, -10, 10)) fail('Invalid model parameters');
  return {w1: x.w1, w2: x.w2, b: x.b};
}
export function validateSnapshot(input, now) {
  if (!object(input) || input.schemaVersion !== SCHEMA_VERSION || !GATEWAY_ID.test(input.gatewayId)) fail('Invalid snapshot identity');
  if (!integer(input.capturedAt, now - 120000, now + 30000)) fail('Snapshot clock is outside the acceptance window');
  if (typeof input.sessionId !== 'string' || !/^[a-f0-9-]{36}$/.test(input.sessionId)) fail('Invalid gateway session');
  if (!Array.isArray(input.nodes) || input.nodes.length > LIMITS.nodes) fail('At most 12 nodes are supported');
  const ids = new Set();
  const nodes = input.nodes.map(n => {
    if (!object(n) || !integer(n.id, 1, 255) || ids.has(n.id)) fail('Node IDs must be unique uint8 values');
    ids.add(n.id);
    if (typeof n.label !== 'string' || n.label.length > 48 || /[\x00-\x1f]/.test(n.label)) fail('Invalid node label');
    if (!integer(n.bootId, 0, 0xffffffff) || !integer(n.round, 0, 65535) || !integer(n.sampleCount, 0, 0xffffffff)) fail('Invalid node counters');
    if (!time(n.seenAt, input.capturedAt) || !time(n.temperatureAt, input.capturedAt) || !time(n.modelAt, input.capturedAt)) fail('Invalid observation times');
    if (n.temperature !== null && !num(n.temperature, -55, 125)) fail('Invalid temperature');
    if (n.loss !== null && !num(n.loss, 0, 1e12)) fail('Invalid local loss');
    if (!integer(n.roleFlags, 0, 1)) fail('Invalid node role');
    return {id:n.id,label:n.label,bootId:n.bootId,round:n.round,sampleCount:n.sampleCount,
      seenAt:n.seenAt,temperatureAt:n.temperatureAt,modelAt:n.modelAt,
      temperature:n.temperature,loss:n.loss,roleFlags:n.roleFlags,model:model(n.model)};
  }).sort((a,b)=>a.id-b.id);
  let aggregate = null;
  if (input.aggregate !== null) {
    const a = input.aggregate;
    if (!object(a) || !ids.has(a.leaderId) || !integer(a.bootId, 1, 0xffffffff) || !integer(a.round, 0, 65535) ||
        !num(a.loss,0,1e12) || !time(a.seenAt,input.capturedAt) || a.seenAt === null ||
        nodes.find(n=>n.id===a.leaderId).bootId !== a.bootId) fail('Invalid aggregate identity');
    aggregate = {leaderId:a.leaderId,bootId:a.bootId,round:a.round,loss:a.loss,seenAt:a.seenAt,model:model(a.model)};
  }
  if (!object(input.counters) || !['temperature','model','aggregation','discovery','rejected'].every(k=>integer(input.counters[k],0,Number.MAX_SAFE_INTEGER))) fail('Invalid packet counters');
  return {schemaVersion:SCHEMA_VERSION,gatewayId:input.gatewayId,sessionId:input.sessionId,
    capturedAt:input.capturedAt,nodes,aggregate,
    counters:Object.fromEntries(['temperature','model','aggregation','discovery','rejected'].map(k=>[k,input.counters[k]]))};
}
export function summarize(snapshot, now, receivedAt = snapshot?.capturedAt) {
  if (!snapshot) return {online:0,total:0,participants:0,temperature:null,loss:null,samples:0,gatewayOnline:false,aggregate:null};
  const gatewayOnline = now - receivedAt <= LIMITS.gatewayFreshMs;
  // Liveness is explicitly relative to capture, never invented between uploads.
  const online = n => gatewayOnline && n.seenAt !== null && snapshot.capturedAt - n.seenAt <= LIMITS.nodeFreshMs;
  const modelFresh = n => online(n) && n.modelAt !== null && snapshot.capturedAt - n.modelAt <= LIMITS.modelFreshMs;
  const temperatures = snapshot.nodes.filter(n=>online(n)&&n.temperatureAt!==null&&snapshot.capturedAt-n.temperatureAt<=LIMITS.nodeFreshMs&&n.temperature!==null).map(n=>n.temperature);
  const training = snapshot.nodes.filter(n=>modelFresh(n)&&n.loss!==null&&n.sampleCount>0);
  const a = snapshot.aggregate;
  return {online:snapshot.nodes.filter(online).length,total:snapshot.nodes.length,participants:training.length,
    temperature:temperatures.length ? temperatures.reduce((a,b)=>a+b,0)/temperatures.length : null,
    loss:training.length ? training.reduce((a,n)=>a+n.loss,0)/training.length : null,
    samples:training.reduce((a,n)=>a+n.sampleCount,0),gatewayOnline,
    aggregate: gatewayOnline && a && snapshot.capturedAt-a.seenAt<=LIMITS.modelFreshMs ? a : null};
}
export function historyPoint(snapshot) {
  const s = summarize(snapshot,snapshot.capturedAt);
  return {capturedAt:snapshot.capturedAt,temperature:s.temperature,loss:s.loss,
    online:s.online,round:s.aggregate?.round??null,aggregateLoss:s.aggregate?.loss??null};
}
export function diagnose(question, snapshot, now, receivedAt) {
  const q=question.toLowerCase(), s=summarize(snapshot,now,receivedAt);
  if (/loss|accuracy|converg|train/.test(q)) return s.loss === null ?
    'No fresh training loss is available. Check model broadcasts and gateway connectivity. Training loss does not establish held-out accuracy.' :
    `${s.participants} fresh training participants have mean local loss ${s.loss.toExponential(3)}. Contributor training loss is not validation accuracy. Compare frozen holdout benchmarks before claiming improvement.`;
  if (/leader|aggregat|round/.test(q)) return s.aggregate ?
    `Node ${s.aggregate.leaderId} supplied aggregate round ${s.aggregate.round}. The bridge checks leader election, boot identity, and uint16 round order. Aggregation runs on the boards.` :
    'No fresh, validated aggregate is available. Check discovery heartbeats, eligible leader identity, and aggregation broadcasts.';
  if (/cost|free|quota|cloud/.test(q)) return 'Default cloud cadence is one gateway snapshot and one viewer refresh every 30 seconds: 2,880 API requests per day each. History uses 1,440 fixed minute slots. Account-wide usage from other apps and additional viewers also counts.';
  if (/physics|equation|model/.test(q)) return 'Each board predicts temperature using diffusion plus a learned quadratic correction on standardized temperature. A displayed network link indicates participation, not a measured physical stencil edge. The cloud does not train the model.';
  if (/connect|offline|health|status|node|network/.test(q)) return !snapshot ?
    'No gateway snapshot is available. Connect with a read token, then run the LAN bridge with its gateway token. You can explore simulated data separately.' :
    `${s.online} of ${s.total} nodes were online at the latest capture. Gateway ${s.gatewayOnline?'is reachable':'is stale'}. Node liveness uses a 6-second heartbeat window at capture; model freshness uses 15 seconds. Check Wi-Fi, UDP port 4210, and the gateway clock.`;
  return 'I can inspect network health, explain training loss, identify the aggregate leader, explain the physics model, or estimate the free-tier request budget. This guide is read-only and uses the displayed telemetry.';
}
