import {spawnSync} from 'node:child_process';
const compiler=process.env.CXX||'g++';
if(spawnSync(compiler,['--version'],{stdio:'ignore'}).status!==0)throw new Error('A C++ compiler is required for the Cloudflare release checks');
const steps=[['npm',['run','check']],['npm',['test']],['python3',['-m','unittest','discover','tests','-v']],
  ['python3',['-m','pip','install','platformio==6.1.18']],['python3',['-m','platformio','run','-e','nodemcuv2']],['npm',['run','build']]];
for(const [command,args] of steps){
  console.log(`Checking: ${command} ${args.join(' ')}`);
  const result=spawnSync(command,args,{stdio:'inherit',env:{...process.env,CXX:compiler,PIFL_SANITIZE:'1'}});
  if(result.error)throw result.error;
  if(result.status!==0)process.exit(result.status||1);
}
