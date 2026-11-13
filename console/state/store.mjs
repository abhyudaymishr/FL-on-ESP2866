import {historyPoint} from '../../shared/domain.mjs';
export function createStore() {
  let state={mode:'disconnected',snapshot:null,receivedAt:null,points:[],error:null,paused:false,filter:'',status:'all',sort:'id',selected:null,range:60};
  const listeners=new Set();
  return {
    get:()=>state,
    subscribe(fn){listeners.add(fn);return()=>listeners.delete(fn);},
    set(patch){state={...state,...patch};for(const fn of listeners)fn(state);},
    ingest(snapshot,receivedAt){
      let points=state.points;
      if(snapshot&&snapshot.capturedAt!==state.snapshot?.capturedAt){
        const point=historyPoint(snapshot), minute=Math.floor(point.capturedAt/60000);
        points=[...points.filter(p=>Math.floor(p.capturedAt/60000)!==minute),point].sort((a,b)=>a.capturedAt-b.capturedAt).slice(-1440);
      }
      this.set({snapshot,receivedAt,points,error:null});
    }
  };
}
