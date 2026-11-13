export const NOW=1794567600000;
export function snapshot(at=NOW){return {schemaVersion:1,gatewayId:'lab-01',sessionId:'11111111-1111-1111-1111-111111111111',capturedAt:at,
  nodes:[{id:1,label:'Thermal core',bootId:101,round:4,sampleCount:42,roleFlags:1,seenAt:at-100,temperatureAt:at-100,modelAt:at-500,
    temperature:26.25,loss:.125,model:{w1:.125,w2:-.03125,b:.5}}],
  aggregate:{leaderId:1,bootId:101,round:4,loss:.1,seenAt:at-500,model:{w1:.125,w2:-.03125,b:.5}},
  counters:{temperature:42,model:7,aggregation:4,discovery:21,rejected:0}};}
