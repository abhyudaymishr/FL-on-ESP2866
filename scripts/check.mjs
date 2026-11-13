import {spawnSync} from 'node:child_process';
import {files,ROOT} from './build.mjs';
import path from 'node:path';
const directories=['cloud','console','shared','scripts','tests/cloud'];
let count=0;
for(const directory of directories)for(const file of await files(path.join(ROOT,directory))){
  if(!file.endsWith('.mjs'))continue;
  const result=spawnSync(process.execPath,['--check',file],{encoding:'utf8'});
  if(result.status!==0){console.error(result.stderr);process.exit(1);}count++;
}
console.log(`Syntax checked ${count} JavaScript modules.`);
