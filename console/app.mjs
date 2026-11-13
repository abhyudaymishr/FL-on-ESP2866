import {TelemetryClient} from './data/client.mjs';
import {demoSnapshot,demoHistory} from './data/demo.mjs';
import {createStore} from './state/store.mjs';
import {render,nodeDetails} from './ui/render.mjs';
import {diagnose,LIMITS} from '../shared/domain.mjs';
const store=createStore(), $=id=>document.getElementById(id);
let timer=null,controller=null,historyLoadedAt=0,client=new TelemetryClient();
const storage={get(key){try{return sessionStorage.getItem(key);}catch{return null;}},set(key,value){try{value===null?sessionStorage.removeItem(key):sessionStorage.setItem(key,value);}catch{}}};
function toast(message){$('toast').textContent=message;$('toast').hidden=false;clearTimeout(toast.timer);toast.timer=setTimeout(()=>$('toast').hidden=true,3500);}
store.subscribe(state=>render(state));
function stop(){clearTimeout(timer);controller?.abort();}
async function refresh(){
  stop();const state=store.get();if(state.paused||document.hidden)return;
  if(state.mode==='demo'){
    const now=Date.now();store.ingest(demoSnapshot(now),now);timer=setTimeout(refresh,2000);return;
  }
  if(state.mode!=='live')return;
  controller=new AbortController();const current=controller;
  const timeout=setTimeout(()=>current.abort(),8000);
  try{
    const result=await client.state(current.signal);
    if(current!==controller||store.get().mode!=='live')return;
    store.ingest(result.snapshot,result.receivedAt);
    if(Date.now()-historyLoadedAt>300000){
      const history=await client.history(store.get().range,current.signal);
      if(current!==controller||store.get().mode!=='live')return;
      store.set({points:history.points});historyLoadedAt=Date.now();
    }
  }catch(error){if(current===controller)store.set({error:error.name==='AbortError'?'Connection timed out. Retrying automatically.':error.message});}
  finally{clearTimeout(timeout);if(current===controller&&store.get().mode==='live'&&!store.get().paused)timer=setTimeout(refresh,LIMITS.pollMs);}
}
function openConnection(){ $('read-token').value=client.token;$('connection-dialog').showModal(); }
for(const id of ['connect','settings'])$(id).addEventListener('click',openConnection);
$('connection-form').addEventListener('submit',event=>{
  event.preventDefault();stop();client=new TelemetryClient({token:$('read-token').value.trim()});
  storage.set('pifl-read-token',client.token);historyLoadedAt=0;
  store.set({mode:'live',snapshot:null,points:[],receivedAt:null,error:null,paused:false});
  $('connection-dialog').close();refresh();
});
$('disconnect').addEventListener('click',()=>{stop();client.token='';storage.set('pifl-read-token',null);store.set({mode:'disconnected',snapshot:null,receivedAt:null,points:[],error:null});$('connection-dialog').close();});
$('demo').addEventListener('click',()=>{stop();historyLoadedAt=0;store.set({mode:'demo',snapshot:null,receivedAt:null,points:demoHistory(),error:null,paused:false});refresh();});
$('pause').addEventListener('click',()=>{const paused=!store.get().paused;store.set({paused});paused?stop():refresh();});
for(const [id,key] of [['search','filter'],['node-status','status'],['node-sort','sort']])$(id).addEventListener(id==='search'?'input':'change',event=>store.set({[key]:event.target.value}));
for(const select of document.querySelectorAll('.range-select'))select.addEventListener('change',event=>{
  const range=Number(event.target.value);store.set({range});historyLoadedAt=0;
  if(store.get().mode==='demo')store.set({points:demoHistory(Date.now(),range)});else refresh();
});
document.addEventListener('click',event=>{
  const close=event.target.closest('[data-close]');if(close)$(close.dataset.close).close();
  const item=event.target.closest('[data-node]');if(item){const snapshot=store.get().snapshot,node=snapshot?.nodes.find(n=>n.id===Number(item.dataset.node));
    if(node){$('node-dialog-title').textContent=node.label;$('node-details').innerHTML=nodeDetails(node,snapshot,Date.now());$('node-dialog').showModal();}}
});
document.addEventListener('keydown',event=>{const item=event.target.closest('[data-node][role=button]');if(item&&['Enter',' '].includes(event.key)){event.preventDefault();item.dispatchEvent(new MouseEvent('click',{bubbles:true}));}});
$('open-guide').addEventListener('click',()=>$('guide-dialog').showModal());
function ask(question){const {snapshot,receivedAt,mode}=store.get(),messages=$('guide-messages');
  for(const [kind,value] of [['user',question],['assistant',`${mode==='demo'?'Simulation context. ':''}${diagnose(question,snapshot,Date.now(),receivedAt)}`]]){
    const element=document.createElement('div');element.className=`guide-message ${kind}`;element.textContent=value;messages.appendChild(element);
  }
  while(messages.children.length>20)messages.firstChild.remove();messages.scrollTop=messages.scrollHeight;
}
$('guide-form').addEventListener('submit',event=>{event.preventDefault();const q=$('guide-question').value.trim();if(q)ask(q);$('guide-question').value='';});
for(const button of document.querySelectorAll('[data-question]'))button.addEventListener('click',()=>ask(button.dataset.question));
$('export').addEventListener('click',()=>{
  const {snapshot,mode}=store.get();if(!snapshot)return;
  const blob=new Blob([JSON.stringify({dataSource:mode==='demo'?'simulation':'gateway',exportedAt:new Date().toISOString(),snapshot},null,2)],{type:'application/json'});
  const url=URL.createObjectURL(blob),a=document.createElement('a');a.href=url;a.download=`pifl-${mode==='demo'?'simulation':snapshot.gatewayId}-${snapshot.capturedAt}.json`;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);toast('Snapshot exported.');
});
function route(){const view=['overview','nodes','learning','activity'].includes(location.hash.slice(1))?location.hash.slice(1):'overview';
  for(const name of ['overview','nodes','learning','activity'])$(`view-${name}`).hidden=name!==view;
  for(const link of document.querySelectorAll('[data-view]')){link.classList.toggle('active',link.dataset.view===view);if(link.dataset.view===view)link.setAttribute('aria-current','page');else link.removeAttribute('aria-current');}
  $('breadcrumb').textContent={overview:'Overview',nodes:'Node explorer',learning:'Learning',activity:'Activity'}[view];
  $('page-title').textContent={overview:'Network overview.',nodes:'Explore your edge fleet.',learning:'Learning, explained.',activity:'Network activity.'}[view];
  $('fleet').hidden=['learning','activity'].includes(view);
}
window.addEventListener('hashchange',route);route();
$('theme-toggle').addEventListener('click',()=>{const theme=document.documentElement.dataset.theme==='dark'?'light':'dark';document.documentElement.dataset.theme=theme;storage.set('pifl-theme',theme);});
document.documentElement.dataset.theme=storage.get('pifl-theme')||'light';
document.addEventListener('visibilitychange',()=>document.hidden?stop():refresh());
setInterval(()=>{const now=Date.now();$('clock').textContent=new Date(now).toLocaleTimeString([],{hour:'2-digit',minute:'2-digit'});
  if(store.get().mode!=='demo'&&!document.hidden)render(store.get(),now);
},5000);
const token=storage.get('pifl-read-token');if(token){client.token=token;store.set({mode:'live'});refresh();}else render(store.get());
