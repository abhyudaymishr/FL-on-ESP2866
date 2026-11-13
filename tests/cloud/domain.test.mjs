import test from 'node:test';import assert from 'node:assert/strict';
import {snapshot,NOW} from './fixtures.mjs';
import {validateSnapshot,summarize,diagnose,historyPoint} from '../../shared/domain.mjs';
import {createStore} from '../../console/state/store.mjs';
import {escapeHtml,chart} from '../../console/ui/render.mjs';
test('freshness separates heartbeat, sensor and training observations',()=>{
  const input=snapshot();assert.equal(summarize(input,NOW).participants,1);
  input.nodes[0].modelAt=NOW-16000;input.nodes[0].temperatureAt=NOW-7000;
  const summary=summarize(input,NOW);assert.equal(summary.online,1);assert.equal(summary.loss,null);assert.equal(summary.temperature,null);
  assert.equal(summarize(input,NOW+91000).online,0);assert.equal(summarize(input,NOW+91000).aggregate,null);
});
test('schema rejects invalid values, duplicates, overflow and future observations',()=>{
  for(const mutate of [s=>s.nodes.push(s.nodes[0]),s=>s.nodes[0].temperature=NaN,s=>s.nodes[0].model.w2=1,
    s=>s.nodes[0].sampleCount=-1,s=>s.nodes[0].seenAt=NOW+1,s=>s.nodes[0].roleFlags=3,
    s=>s.aggregate.bootId=999,s=>s.nodes[0].loss=Infinity,s=>s.capturedAt=NOW-120001,
    s=>s.gatewayId='../invalid',s=>s.nodes=Array(13).fill(s.nodes[0])]){
    const s=snapshot();mutate(s);assert.throws(()=>validateSnapshot(s,NOW));
  }
  assert.deepEqual(validateSnapshot(snapshot(),NOW),snapshot());
});
test('normalization drops extra fields and presentation escapes untrusted labels',()=>{
  const s=snapshot();s.secret='never retained';s.nodes[0].address='local address';s.nodes[0].label='<img src=x onerror=alert(1)>';
  const clean=validateSnapshot(s,NOW);assert.equal(clean.secret,undefined);assert.equal(clean.nodes[0].address,undefined);
  assert.equal(escapeHtml(clean.nodes[0].label),'&lt;img src=x onerror=alert(1)&gt;');
});
test('guide grounds answers in telemetry and identifies limits',()=>{
  assert.match(diagnose('explain training loss',snapshot(),NOW),/not validation accuracy/);
  assert.match(diagnose('leader',snapshot(),NOW),/Node 1/);
  assert.match(diagnose('health',null,NOW),/No gateway snapshot/);
  assert.match(diagnose('free cloud',snapshot(),NOW),/2,880/);
});
test('store bounds history and replaces rather than duplicates minute observations',()=>{
  const store=createStore();store.ingest(snapshot(),NOW);store.ingest(snapshot(NOW+1000),NOW+1000);assert.equal(store.get().points.length,1);
  store.set({points:Array.from({length:1440},(_,i)=>({capturedAt:NOW-(1440-i)*60000}))});
  store.ingest(snapshot(NOW+60000),NOW+60000);assert.equal(store.get().points.length,1440);
  assert.equal(historyPoint(snapshot()).round,4);
});
test('plots break at missing observations and outages',()=>{
  const svg=chart([{capturedAt:1,temperature:20},{capturedAt:60001,temperature:null},{capturedAt:120001,temperature:22}], 'temperature');
  const path=svg.match(/<path d="([^"]+)/)[1];assert.equal((path.match(/M/g)||[]).length,2);assert.ok(!path.includes('NaN'));
});
