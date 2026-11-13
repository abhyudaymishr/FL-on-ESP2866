const url=process.env.PIFL_CLOUD_URL,token=process.env.PIFL_READ_TOKEN;
if(!url||!token)throw new Error('Set PIFL_CLOUD_URL and PIFL_READ_TOKEN');
async function request(route,authorized=false){return fetch(url+route,{headers:authorized?{Authorization:`Bearer ${token}`}:{},signal:AbortSignal.timeout(15000)});}
async function smoke(){
  const health=await request('/api/health');if(!health.ok)throw new Error('Health check failed');
  const info=await health.json(),expected=process.env.GITHUB_SHA||process.env.WORKERS_CI_COMMIT_SHA;
  if(expected&&info.release!==expected)throw new Error('Release SHA does not match deployed artifact');
  if((await request('/api/state')).status!==401)throw new Error('Unauthenticated state was not rejected');
  if((await request('/api/ingest')).status!==405)throw new Error('Ingestion method guard failed');
  if(!(await request('/api/state',true)).ok)throw new Error('Authenticated state check failed');
  if(!(await request('/api/ready',true)).ok)throw new Error('Database schema readiness check failed');
  if(!(await request('/api/history?minutes=60',true)).ok)throw new Error('History check failed');
  const page=await request('/');if(!page.ok||!(await page.text()).includes('Network overview'))throw new Error('UI smoke check failed');
}
for(let attempt=1;attempt<=6;attempt++){
  try{await smoke();console.log('Production smoke checks passed: release, UI, auth, method guard, state, history.');break;}
  catch(error){if(attempt===6)throw error;console.log(`Waiting for rollout (${attempt}/6): ${error.message}`);await new Promise(resolve=>setTimeout(resolve,5000));}
}
