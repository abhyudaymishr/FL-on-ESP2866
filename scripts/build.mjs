import {cp,mkdir,readFile,readdir,rm,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
export const ROOT=fileURLToPath(new URL('../',import.meta.url));
export const MIME={'.html':'text/html; charset=utf-8','.css':'text/css; charset=utf-8','.mjs':'text/javascript; charset=utf-8','.js':'text/javascript; charset=utf-8','.svg':'image/svg+xml','.json':'application/json','.txt':'text/plain; charset=utf-8'};
export async function files(directory){const result=[];for(const entry of await readdir(directory,{withFileTypes:true})){const full=path.join(directory,entry.name);if(entry.isDirectory())result.push(...await files(full));else if(entry.isFile())result.push(full);}return result.sort();}
export async function build(){
  const dist=path.join(ROOT,'dist');await rm(dist,{recursive:true,force:true});
  await mkdir(path.join(dist,'modules'),{recursive:true});
  await cp(path.join(ROOT,'console'),path.join(dist,'public'),{recursive:true});
  await cp(path.join(ROOT,'shared'),path.join(dist,'public/shared'),{recursive:true});
  for(const directory of ['cloud','shared'])await cp(path.join(ROOT,directory),path.join(dist,'modules',directory),{recursive:true,filter:source=>!source.includes('migrations')});
  const manifest={};
  for(const file of await files(path.join(dist,'public'))){
    if(path.basename(file).startsWith('_'))continue;
    const content=await readFile(file),relative='/'+path.relative(path.join(dist,'public'),file).split(path.sep).join('/');
    manifest[relative]={hash:createHash('sha256').update(content.toString('base64')+path.extname(file).slice(1)).digest('hex').slice(0,32),size:content.length};
  }
  await writeFile(path.join(dist,'manifest.json'),JSON.stringify(manifest,null,2)+'\n');
  await writeFile(path.join(dist,'release.json'),JSON.stringify({schemaVersion:1,sha:process.env.GITHUB_SHA||process.env.WORKERS_CI_COMMIT_SHA||'local',compatibilityDate:'2026-11-13'})+'\n');
  console.log(`Built ${Object.keys(manifest).length} static assets and the Worker module graph.`);
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url))await build();
