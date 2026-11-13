import test from 'node:test';import assert from 'node:assert/strict';
import {mkdtemp,mkdir,writeFile,readFile,rm} from 'node:fs/promises';import os from 'node:os';import path from 'node:path';
import {apiCall,deploy} from '../../scripts/deploy.mjs';
const ACCOUNT='a'.repeat(32),DB='11111111-1111-1111-1111-111111111111';
test('Cloudflare error messages expose status and codes without response secrets',async()=>{
  const fetcher=async()=>Response.json({success:false,errors:[{code:10000,message:'secret-token-sensitive'}]},{status:403});
  await assert.rejects(apiCall('https://example.test',{token:'private'},fetcher),error=>error.message.includes('403')&&error.message.includes('10000')&&!error.message.includes('secret'));
});
test('deployment registers assets, uses the upload JWT, applies migrations and retains rollback versions',async(t)=>{
  t.mock.method(console,'log',()=>{});
  const temp=await mkdtemp(path.join(os.tmpdir(),'pifl-release-'));
  try{
    await mkdir(path.join(temp,'public'));await mkdir(path.join(temp,'modules'));
    const names=['index.html','style.css','app.mjs','favicon.svg','extra.json'];const manifest={};
    for(const [i,name] of names.entries()){manifest['/'+name]={hash:String(i).padStart(32,'0'),size:4};await writeFile(path.join(temp,'public',name),'test');}
    await writeFile(path.join(temp,'public/_headers'),'/*\n X-Content-Type-Options: nosniff');
    await writeFile(path.join(temp,'modules/worker.mjs'),'export default {}');
    await writeFile(path.join(temp,'manifest.json'),JSON.stringify(manifest));await writeFile(path.join(temp,'release.json'),JSON.stringify({sha:'abc123',compatibilityDate:'2026-11-13'}));
    const calls=[];let metadata;
    const fetcher=async(url,options)=>{
      const route=new URL(url).pathname.split('/client/v4')[1],body=options.body instanceof FormData?options.body:options.body?JSON.parse(options.body):undefined;
      calls.push({route,options,body});
      let result;
      if(route.endsWith('/workers/subdomain'))result={subdomain:'lab'};
      else if(route.endsWith('/d1/database'))result=[{uuid:DB,name:'pifl-control-room'}];
      else if(route.endsWith('/query'))result=body.sql?.startsWith('SELECT')?[{results:[]}]:[{success:true,results:[]}];
      else if(route.endsWith('/deployments'))result={deployments:[{versions:[{version_id:DB,percentage:100}]}]};
      else if(route.endsWith('/assets-upload-session'))result={jwt:'upload-jwt',buckets:[[manifest['/index.html'].hash]]};
      else if(route.endsWith('/workers/assets/upload')){
        assert.equal(options.headers.Authorization,'Bearer upload-jwt');
        const part=body.get(manifest['/index.html'].hash);assert.equal(await part.text(),Buffer.from('test').toString('base64'));assert.match(part.type,/text\/html/);
        result={jwt:'completion-jwt'};
      }else if(options.method==='PUT'){
        metadata=JSON.parse(await body.get('metadata').text());assert.ok(body.get('worker.mjs'));result={success:true};
      }else result={};
      return Response.json({success:true,result});
    };
    const env={CLOUDFLARE_ACCOUNT_ID:ACCOUNT,CLOUDFLARE_API_TOKEN:'deploy-token',PIFL_D1_DATABASE_ID:DB,PIFL_GATEWAY_KEYS:JSON.stringify({'lab-01':'w'.repeat(40)}),PIFL_READ_TOKEN:'r'.repeat(40)};
    const url=await deploy(env,fetcher,temp);
    assert.equal(url,'https://fl-on-esp2866.lab.workers.dev');
    assert.equal(metadata.assets.jwt,'completion-jwt');assert.equal(metadata.assets.config.run_worker_first[0],'/api/*');
    assert.equal(metadata.observability.traces.enabled,true);assert.equal(metadata.bindings.find(b=>b.name==='DB').id,DB);
    assert.equal(metadata.bindings.find(b=>b.name==='READ_TOKEN').type,'secret_text');
    assert.equal(JSON.parse(await readFile(path.join(temp,'rollback.json'),'utf8')).versions[0].version_id,DB);
    assert.ok(calls.some(c=>c.body?.batch?.[1]?.sql.startsWith('INSERT INTO schema_migrations')));
    assert.ok(!calls.some(c=>c.route.includes('subscriptions')||c.route.includes('billing')));
    calls.length=0;
    await deploy({...env,PIFL_MIGRATION_MODE:'preprovisioned',PIFL_APPLIED_MIGRATIONS:'0001_initial.sql'},fetcher,temp);
    assert.ok(!calls.some(c=>c.route.includes('/d1/')));
    assert.deepEqual(JSON.parse(metadata.bindings.find(b=>b.name==='EXPECTED_MIGRATIONS').text),['0001_initial.sql']);
    await assert.rejects(deploy({...env,PIFL_MIGRATION_MODE:'preprovisioned',PIFL_APPLIED_MIGRATIONS:'0002_unknown.sql'},fetcher,temp),/Apply the numbered D1 migrations/);
  }finally{await rm(temp,{recursive:true,force:true});}
});
