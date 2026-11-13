// Programmatic Cloudflare deployment via the documented REST API. No third-party
// build/deploy dependencies; never installs a CLI or changes the billing plan.
import {readFile,readdir,appendFile,writeFile} from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
import path from 'node:path';
import {files,ROOT,MIME} from './build.mjs';
export async function apiCall(url,{token,method='GET',body}={},fetcher=fetch){
  const headers={Authorization:`Bearer ${token}`};
  if(body!==undefined&&!(body instanceof FormData)){headers['Content-Type']='application/json';body=JSON.stringify(body);}
  const response=await fetcher(url,{method,headers,body,signal:AbortSignal.timeout(60000)});
  let data;try{data=await response.json();}catch{throw new Error(`Cloudflare ${method} failed (${response.status})`);}
  if(!response.ok||data.success===false)throw new Error(`Cloudflare ${method} failed (${response.status}): ${(data.errors||[]).map(e=>e.code).join(',')}`);
  return data.result;
}
export function deploymentMetadata({databaseId,release,keys,readToken,assetsJwt,headers,migrations=['0001_initial.sql']}){
  return {main_module:'cloud/worker.mjs',compatibility_date:release.compatibilityDate,
    bindings:[{type:'d1',name:'DB',id:databaseId},{type:'assets',name:'ASSETS'},
      {type:'secret_text',name:'GATEWAY_KEYS',text:keys},{type:'secret_text',name:'READ_TOKEN',text:readToken},
      {type:'plain_text',name:'RELEASE_SHA',text:release.sha},
      {type:'plain_text',name:'EXPECTED_MIGRATIONS',text:JSON.stringify(migrations)}],
    assets:{jwt:assetsJwt,config:{html_handling:'auto-trailing-slash',not_found_handling:'none',run_worker_first:['/api/*'],_headers:headers}},
    observability:{enabled:true,head_sampling_rate:.1,logs:{enabled:true,invocation_logs:true},traces:{enabled:true,head_sampling_rate:.1}}};
}
export async function deploy(env=process.env,fetcher=fetch,artifactRoot=path.join(ROOT,'dist')){
  const manifest=JSON.parse(await readFile(path.join(artifactRoot,'manifest.json'),'utf8'));
  const release=JSON.parse(await readFile(path.join(artifactRoot,'release.json'),'utf8'));
  if(Object.keys(manifest).length<5)throw new Error('Incomplete build');
  for(const file of await files(path.join(artifactRoot,'modules'))){if(!file.endsWith('.mjs'))throw new Error('Unexpected Worker module');}
  if(process.argv.includes('--dry-run')){console.log(`Deployment artifact valid: ${Object.keys(manifest).length} assets; release ${release.sha}. No API requests sent.`);return;}
  for(const name of ['CLOUDFLARE_API_TOKEN','CLOUDFLARE_ACCOUNT_ID','PIFL_GATEWAY_KEYS','PIFL_READ_TOKEN'])if(!env[name])throw new Error(`Missing ${name}`);
  const keys=JSON.parse(env.PIFL_GATEWAY_KEYS),ids=Object.keys(keys);
  if(ids.length!==1||!/^[a-z0-9][a-z0-9-]{0,31}$/.test(ids[0])||typeof keys[ids[0]]!=='string'||keys[ids[0]].length<32||env.PIFL_READ_TOKEN.length<32||keys[ids[0]]===env.PIFL_READ_TOKEN)throw new Error('Use one gateway with distinct 32+ character read and gateway tokens');
  const name=env.PIFL_WORKER_NAME||'fl-on-esp2866';
  if(!/^[a-z0-9][a-z0-9-]{0,62}$/.test(name)||!/^[a-f0-9]{32}$/.test(env.CLOUDFLARE_ACCOUNT_ID))throw new Error('Invalid deployment identity');
  const base=`https://api.cloudflare.com/client/v4/accounts/${env.CLOUDFLARE_ACCOUNT_ID}`;
  const api=(route,options={})=>apiCall(base+route,{token:env.CLOUDFLARE_API_TOKEN,...options},fetcher);
  // Resolve workers.dev before provisioning anything, so first-use setup fails clearly.
  const subdomain=await api('/workers/subdomain');
  let databaseId=env.PIFL_D1_DATABASE_ID;
  if(!databaseId){const dbs=await api('/d1/database?per_page=100');const db=dbs.find(d=>d.name===name);
    databaseId=db?.uuid||(await api('/d1/database',{method:'POST',body:{name,primary_location_hint:'apac'}})).uuid;}
  if(!/^[a-f0-9-]{36}$/.test(databaseId))throw new Error('Invalid D1 database ID');
  const query=body=>api(`/d1/database/${databaseId}/query`,{method:'POST',body});
  const migrations=(await readdir(path.join(ROOT,'cloud/migrations'))).filter(n=>n.endsWith('.sql')).sort();
  if(env.PIFL_MIGRATION_MODE==='preprovisioned'){
    const applied=(env.PIFL_APPLIED_MIGRATIONS||'').split(',').filter(Boolean).sort();
    if(!env.PIFL_D1_DATABASE_ID||JSON.stringify(applied)!==JSON.stringify(migrations))throw new Error('Apply the numbered D1 migrations and update PIFL_APPLIED_MIGRATIONS before deploying');
  }else{
    await query({sql:'CREATE TABLE IF NOT EXISTS schema_migrations (name TEXT PRIMARY KEY, applied_at INTEGER NOT NULL)'});
    for(const migration of migrations){
      const rows=await query({sql:'SELECT name FROM schema_migrations WHERE name=?',params:[migration]});
      if(rows[0].results.length)continue;
      const sql=await readFile(path.join(ROOT,'cloud/migrations',migration),'utf8');
      const results=await query({batch:[{sql},{sql:'INSERT INTO schema_migrations VALUES (?, ?)',params:[migration,String(Date.now())]}]});
      if(results.some(result=>result.success===false))throw new Error(`Migration failed: ${migration}`);
    }
  }
  const script=`/workers/scripts/${name}`;
  let previous={versions:[]};
  try{const deployments=await api(script+'/deployments');previous=deployments.deployments[0]||previous;}
  catch(error){if(!error.message.includes('(404)'))throw error;}
  await writeFile(path.join(artifactRoot,'rollback.json'),JSON.stringify({versions:previous.versions})+'\n');
  const session=await api(script+'/assets-upload-session',{method:'POST',body:{manifest}});
  let completion=session.buckets.length?null:session.jwt;
  for(const bucket of session.buckets){
    const form=new FormData();
    for(const hash of bucket){const relative=Object.keys(manifest).find(p=>manifest[p].hash===hash);if(!relative)throw new Error('Unrecognized asset hash');
      const bytes=await readFile(path.join(artifactRoot,'public',relative.slice(1)));
      form.append(hash,new Blob([bytes.toString('base64')],{type:MIME[path.extname(relative)]||'application/octet-stream'}),hash);
    }
    const uploaded=await apiCall(base+'/workers/assets/upload?base64=true',{token:session.jwt,method:'POST',body:form},fetcher);
    if(uploaded?.jwt)completion=uploaded.jwt;
  }
  if(!completion)throw new Error('Asset upload did not return a completion token');
  const metadata=deploymentMetadata({databaseId,release,keys:JSON.stringify(keys),readToken:env.PIFL_READ_TOKEN,assetsJwt:completion,migrations,headers:await readFile(path.join(artifactRoot,'public/_headers'),'utf8')});
  const form=new FormData();form.append('metadata',new Blob([JSON.stringify(metadata)],{type:'application/json'}));
  for(const file of await files(path.join(artifactRoot,'modules'))){const relative=path.relative(path.join(artifactRoot,'modules'),file).split(path.sep).join('/');form.append(relative,new Blob([await readFile(file)],{type:'application/javascript+module'}),relative);}
  await api(script,{method:'PUT',body:form});
  await api(script+'/subdomain',{method:'POST',body:{enabled:true,previews_enabled:false}});
  const url=`https://${name}.${subdomain.subdomain}.workers.dev`;
  console.log(`Deployed ${release.sha}: ${url}`);
  if(env.GITHUB_OUTPUT)await appendFile(env.GITHUB_OUTPUT,`url=${url}\ndatabase_id=${databaseId}\n`);
  return url;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url))try{await deploy();}catch(error){console.error(error.message);process.exitCode=1;}
