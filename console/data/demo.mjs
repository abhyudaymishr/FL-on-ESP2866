// Explicit, deterministic simulation; never sent to the ingestion endpoint.
export function demoSnapshot(now=Date.now()) {
  const t=Math.floor(now/1000), round=Math.floor(t/7)%65536;
  const nodes=Array.from({length:8},(_,i)=>({id:i+1,label:['North chamber','Thermal core','East chamber','West chamber','South chamber','Ambient probe','Boundary A','Boundary B'][i],
    bootId:101+i,round,sampleCount:1240+i*37,roleFlags:1,seenAt:now-(i===7?16000:300),temperatureAt:now-(i===7?16000:200),modelAt:now-1000,
    temperature:24.5+Math.sin(t/17+i*.6)*2+i*.15,loss:.012+Math.sin(t/30+i)*.004+i*.002,
    model:{w1:.024+i*.001,w2:-.0032+i*.0001,b:.008+i*.0003}}));
  return {schemaVersion:1,gatewayId:'simulation',sessionId:'00000000-0000-0000-0000-000000000000',capturedAt:now,nodes,
    aggregate:{leaderId:1,bootId:101,round,loss:.0174,seenAt:now-2000,model:{w1:.0275,w2:-.00285,b:.00905}},
    counters:{temperature:Math.max(0,t%100000)*8,model:Math.floor(t%100000/5)*8,aggregation:round,discovery:Math.floor(t%100000/2)*8,rejected:3}};
}
export function demoHistory(now=Date.now(), minutes=60) {
  return Array.from({length:minutes},(_,i)=>{const at=now-(minutes-1-i)*60000;
    return {capturedAt:at,temperature:25.2+Math.sin(at/17000)*.3,loss:.019+Math.exp(-i/12)*.05,
      online:7,round:Math.floor(at/7000)%65536,aggregateLoss:.017+Math.exp(-i/12)*.04};});
}
