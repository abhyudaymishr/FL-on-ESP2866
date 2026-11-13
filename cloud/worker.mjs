import {validateSnapshot, ValidationError, GATEWAY_ID, LIMITS} from '../shared/domain.mjs';
import {TelemetryRepository} from './repository.mjs';
const security={'Cache-Control':'no-store','X-Content-Type-Options':'nosniff','Referrer-Policy':'no-referrer'};
const json=(data,status=200,extra={})=>Response.json(data,{status,headers:{...security,...extra}});
async function equalToken(a,b) {
  if (typeof a!=='string'||typeof b!=='string'||b.length<32||a.length>512) return false;
  const digest=async s=>new Uint8Array(await crypto.subtle.digest('SHA-256',new TextEncoder().encode(s)));
  const [x,y]=await Promise.all([digest(a),digest(b)]);
  let difference=0; for(let i=0;i<x.length;i++) difference|=x[i]^y[i];
  return difference===0;
}
async function boundedJson(request) {
  if (!request.headers.get('content-type')?.toLowerCase().startsWith('application/json')) throw new ValidationError('Use application/json');
  if (Number(request.headers.get('content-length'))>LIMITS.bodyBytes) throw new ValidationError('Snapshot exceeds 32 KiB');
  const reader=request.body?.getReader(); if(!reader) throw new ValidationError('Missing body');
  const chunks=[]; let size=0;
  try { for(;;) { const {done,value}=await reader.read(); if(done)break; size+=value.byteLength;
    if(size>LIMITS.bodyBytes) {await reader.cancel();throw new ValidationError('Snapshot exceeds 32 KiB');} chunks.push(value); }
  } finally {reader.releaseLock();}
  const bytes=new Uint8Array(size);let offset=0;for(const chunk of chunks){bytes.set(chunk,offset);offset+=chunk.length;}
  try{return JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(bytes));}catch{throw new ValidationError('Invalid JSON');}
}
export function createHandler({clock=Date.now,repository=env=>new TelemetryRepository(env.DB)}={}) {
  return async function fetch(request,env) {
    const url=new URL(request.url), path=url.pathname, now=clock();
    try {
      if(path==='/api/health'&&request.method==='GET') return json({status:'ok',service:'pifl-control-plane',schemaVersion:1,release:env.RELEASE_SHA||'development'});
      if(!path.startsWith('/api/')) return env.ASSETS ? env.ASSETS.fetch(request) : new Response('Not found',{status:404});
      const keys=JSON.parse(env.GATEWAY_KEYS||'{}');
      const gatewayIds=Object.keys(keys);
      if(gatewayIds.length!==1||!GATEWAY_ID.test(gatewayIds[0])||typeof keys[gatewayIds[0]]!=='string'||keys[gatewayIds[0]].length<32||!env.READ_TOKEN||env.READ_TOKEN.length<32) return json({error:'Server credentials are not configured'},503);
      const token=request.headers.get('authorization')?.replace(/^Bearer /,'');
      if(path==='/api/ingest') {
        if(request.method!=='POST')return json({error:'Method not allowed'},405,{Allow:'POST'});
        if(!await equalToken(token,keys[gatewayIds[0]])) return json({error:'Unauthorized'},401);
        const snapshot=validateSnapshot(await boundedJson(request),now);
        if(snapshot.gatewayId!==gatewayIds[0])return json({error:'Gateway does not match token'},403);
        const accepted=await repository(env).save(snapshot,now);
        return json({accepted,capturedAt:snapshot.capturedAt},accepted?202:200);
      }
      if(!await equalToken(token,env.READ_TOKEN)) return json({error:'Unauthorized'},401);
      if(request.method!=='GET')return json({error:'Method not allowed'},405,{Allow:'GET'});
      const gatewayId=url.searchParams.get('gateway')||gatewayIds[0];
      if(gatewayId!==gatewayIds[0])return json({error:'Unknown gateway'},404);
      if(path==='/api/ready') {
        const expected=JSON.parse(env.EXPECTED_MIGRATIONS||'["0001_initial.sql"]');
        if(!Array.isArray(expected)||!expected.length||!await repository(env).verifySchema(expected))return json({error:'Database schema is not ready'},503);
        return json({status:'ready',schemaVersion:1});
      }
      if(path==='/api/state')return json({...await repository(env).latest(gatewayId),serverTime:now,pollMs:LIMITS.pollMs});
      if(path==='/api/history') {
        const minutes=Number(url.searchParams.get('minutes')||60);
        if(!Number.isInteger(minutes)||minutes<1||minutes>1440)return json({error:'minutes must be 1–1440'},400);
        return json({points:await repository(env).history(gatewayId,now-minutes*60000),serverTime:now});
      }
      return json({error:'Not found'},404);
    } catch(error) {
      if(error instanceof ValidationError)return json({error:error.message},400);
      console.error(JSON.stringify({event:'request_failed',path,error:error.name}));
      return json({error:'Telemetry service unavailable'},503);
    }
  };
}
export default {fetch:createHandler()};
