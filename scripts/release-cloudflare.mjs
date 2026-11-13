import {spawnSync} from 'node:child_process';
import {deploy} from './deploy.mjs';
try{
  const url=await deploy();
  const smoke=spawnSync(process.execPath,['scripts/smoke.mjs'],{stdio:'inherit',env:{...process.env,PIFL_CLOUD_URL:url}});
  if(smoke.error||smoke.status!==0){
    console.error('Release verification failed; restoring the previous code version.');
    const rollback=spawnSync(process.execPath,['scripts/rollback.mjs','--previous'],{stdio:'inherit'});
    if(rollback.error||rollback.status!==0)console.error('Automatic code rollback failed; inspect the release in Cloudflare.');
    process.exitCode=1;
  }
}catch(error){console.error(error.message);process.exitCode=1;}
