// Dependency-free local preview. Real data is served by the Worker in production.
import http from 'node:http';
import {readFile} from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {ROOT,MIME} from './build.mjs';
export async function assetResponse(pathname){
  const clean=decodeURIComponent(pathname==='/'?'/index.html':pathname);
  const base=clean.startsWith('/shared/')?path.join(ROOT,'shared'):path.join(ROOT,'console');
  const relative=clean.startsWith('/shared/')?clean.slice(8):clean.slice(1),file=path.resolve(base,relative);
  if(!file.startsWith(base+path.sep))return new Response('Forbidden',{status:403});
  try{return new Response(await readFile(file),{headers:{'Content-Type':MIME[path.extname(file)]||'application/octet-stream','Cache-Control':'no-store'}});}catch{return new Response('Not found',{status:404});}
}
export function start(port=Number(process.env.PORT||3000)){
  const server=http.createServer(async(req,res)=>{
    try{
      const url=new URL(req.url,'http://localhost'),response=url.pathname.startsWith('/api/')?
        Response.json({error:'Local preview: use Explore simulation. Deploy the Worker for authenticated gateway data.'},{status:503}):await assetResponse(url.pathname);
      res.writeHead(response.status,Object.fromEntries(response.headers));res.end(Buffer.from(await response.arrayBuffer()));
    }catch{res.writeHead(400);res.end('Bad request');}
  });server.listen(port,'127.0.0.1',()=>console.log(`PIFL console: http://127.0.0.1:${server.address().port}`));
  return server;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url))start();
