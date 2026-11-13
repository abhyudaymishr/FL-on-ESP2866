import {readFile} from 'node:fs/promises';
import {apiCall} from './deploy.mjs';
const {CLOUDFLARE_ACCOUNT_ID:account,CLOUDFLARE_API_TOKEN:token}=process.env;
const worker=process.env.PIFL_WORKER_NAME||'fl-on-esp2866';
if(!token||!/^[a-f0-9]{32}$/.test(account||'')||!/^[a-z0-9][a-z0-9-]{0,62}$/.test(worker))throw new Error('Invalid Cloudflare credentials or Worker name');
let versions;
if(process.argv.includes('--previous')){
  const previous=JSON.parse(await readFile('dist/rollback.json','utf8'));versions=previous.versions;
  if(!versions?.length)throw new Error('First deployment has no previous version; inspect the failed release manually');
}else versions=[{version_id:process.env.PIFL_ROLLBACK_VERSION,percentage:100}];
if(!versions.every(v=>/^[a-f0-9-]{36}$/.test(v.version_id)&&v.percentage>0)||Math.abs(versions.reduce((s,v)=>s+v.percentage,0)-100)>.001)throw new Error('Invalid rollback version');
await apiCall(`https://api.cloudflare.com/client/v4/accounts/${account}/workers/scripts/${worker}/deployments`,{token,method:'POST',body:{strategy:'percentage',versions,annotations:{'workers/message':'PIFL code rollback; database retained'}}});
console.log('Previous code deployment restored. Database contents were retained.');
