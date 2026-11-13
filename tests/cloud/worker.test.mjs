import test from 'node:test';import assert from 'node:assert/strict';
import {createHandler} from '../../cloud/worker.mjs';import {database} from './sqlite.mjs';import {snapshot,NOW} from './fixtures.mjs';
const READ='r'.repeat(40),WRITE='w'.repeat(40);
function setup(){const db=database();return {db,env:{DB:db,GATEWAY_KEYS:JSON.stringify({'lab-01':WRITE}),READ_TOKEN:READ},handle:createHandler({clock:()=>NOW})};}
const req=(path,token,body)=>new Request('https://pifl.test'+path,{method:body===undefined?'GET':'POST',headers:{...(token?{Authorization:`Bearer ${token}`}:{})},...(body===undefined?{}:{body:JSON.stringify(body),headers:{Authorization:`Bearer ${token}`,'Content-Type':'application/json'}})});
test('end-to-end Worker routes use real repository SQL and separate credentials',async()=>{
  const {db,env,handle}=setup();try{
    assert.equal((await handle(req('/api/state'),env)).status,401);
    assert.equal((await handle(req('/api/ingest',READ,snapshot()),env)).status,401);
    assert.equal((await handle(req('/api/state',WRITE),env)).status,401);
    assert.equal((await handle(req('/api/ingest',WRITE,snapshot()),env)).status,202);
    assert.equal((await handle(req('/api/ingest',WRITE,snapshot()),env)).status,200);
    const response=await handle(req('/api/state',READ),env);assert.equal(response.headers.get('cache-control'),'no-store');
    const data=await response.json();assert.equal(data.snapshot.nodes[0].temperature,26.25);
    const history=await (await handle(req('/api/history?minutes=60',READ),env)).json();assert.equal(history.points.length,1);
    assert.equal((await handle(req('/api/ready',READ),env)).status,503);
    db.raw.prepare('INSERT INTO schema_migrations VALUES (?, ?)').run('0001_initial.sql',NOW);
    assert.equal((await handle(req('/api/ready',READ),env)).status,200);
  }finally{db.raw.close();}
});
test('request guards reject invalid bodies, identities, query ranges and methods',async()=>{
  const {db,env,handle}=setup();try{
    const wrong=snapshot();wrong.gatewayId='other';assert.equal((await handle(req('/api/ingest',WRITE,wrong),env)).status,403);
    const invalid=snapshot();invalid.nodes[0].temperature=999;assert.equal((await handle(req('/api/ingest',WRITE,invalid),env)).status,400);
    const huge=new Request('https://pifl.test/api/ingest',{method:'POST',headers:{Authorization:`Bearer ${WRITE}`,'Content-Type':'application/json'},body:'x'.repeat(32769)});
    assert.equal((await handle(huge,env)).status,400);
    const malformed=new Request('https://pifl.test/api/ingest',{method:'POST',headers:{Authorization:`Bearer ${WRITE}`,'Content-Type':'application/json'},body:'{'});
    assert.equal((await handle(malformed,env)).status,400);
    const wrongContent=new Request('https://pifl.test/api/ingest',{method:'POST',headers:{Authorization:`Bearer ${WRITE}`},body:'{}'});
    assert.equal((await handle(wrongContent,env)).status,400);
    for(const minutes of ['0','1441','NaN','1.5'])assert.equal((await handle(req('/api/history?minutes='+minutes,READ),env)).status,400);
    assert.equal((await handle(req('/api/ingest',WRITE),env)).status,405);
    assert.equal((await handle(req('/api/state?gateway=other',READ),env)).status,404);
    assert.equal((await handle(req('/api/unknown',READ),env)).status,404);
    assert.equal((await handle(req('/api/state',READ),{})).status,503);
  }finally{db.raw.close();}
});
test('health is public and assets are delegated without exposing secrets',async()=>{
  const {db,env,handle}=setup();try{
    assert.equal((await handle(req('/api/health'),env)).status,200);
    env.ASSETS={fetch:()=>new Response('static')};assert.equal(await (await handle(req('/'),env)).text(),'static');
    const failed=createHandler({repository:()=>{throw new Error('sensitive backend details');}});
    const response=await failed(req('/api/state',READ),env);assert.equal(response.status,503);assert.ok(!(await response.text()).includes('sensitive'));
  }finally{db.raw.close();}
});
