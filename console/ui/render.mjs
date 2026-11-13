import {summarize,LIMITS} from '../../shared/domain.mjs';
export const escapeHtml=value=>String(value??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const number=(x,d=2)=>x===null||x===undefined?'—':Number(x).toFixed(d);
const loss=x=>x===null||x===undefined?'—':x<.001?x.toExponential(2):x.toFixed(4);
const text=(id,value)=>{document.getElementById(id).textContent=value;};
const empty=(title,note)=>`<div class="empty"><span class="empty-icon">◈</span><strong>${escapeHtml(title)}</strong><p>${escapeHtml(note)}</p></div>`;
export function chart(points,key,{color='#659c59',title='Telemetry chart',height=170,axis=true}={}) {
  const valid=points.filter(p=>Number.isFinite(p[key]));
  if(!valid.length)return axis?empty('No observations in this window','Connect a gateway or explore the simulation.') : '';
  const width=560,pad=axis?{l:43,r:15,t:15,b:26}:{l:0,r:0,t:4,b:0};
  let low=Math.min(...valid.map(p=>p[key])),high=Math.max(...valid.map(p=>p[key]));
  const spread=Math.max((high-low)*.15,Math.abs(high)*.01,.0001);low-=spread;high+=spread;
  const first=points[0].capturedAt,last=points.at(-1).capturedAt;
  const x=p=>pad.l+(p.capturedAt-first)/Math.max(last-first,1)*(width-pad.l-pad.r);
  const y=p=>pad.t+(high-p[key])/(high-low)*(height-pad.t-pad.b);
  // Break paths on missing samples or >2-minute outages; never bridge unknown data.
  let previous=null,path='';
  for(const p of points){if(!Number.isFinite(p[key])){previous=null;continue;}
    path+=`${!previous||p.capturedAt-previous.capturedAt>120000?'M':'L'}${x(p).toFixed(1)},${y(p).toFixed(1)} `;previous=p;}
  let axes='';
  if(axis){for(let i=0;i<4;i++){const yy=pad.t+i*(height-pad.t-pad.b)/3,v=high-i*(high-low)/3;
    axes+=`<line x1="${pad.l}" x2="${width-pad.r}" y1="${yy}" y2="${yy}" stroke="var(--line)" stroke-dasharray="3 4"/><text x="${pad.l-9}" y="${yy+3}" text-anchor="end">${Math.abs(v)<.1?v.toFixed(3):v.toFixed(1)}</text>`;}
    for(let i=0;i<4;i++){const at=first+(last-first)*i/3;axes+=`<text x="${pad.l+i*(width-pad.l-pad.r)/3}" y="${height-5}" text-anchor="middle">${new Date(at).toLocaleTimeString([],{hour:'2-digit',minute:'2-digit'})}</text>`;}}
  return `<svg viewBox="0 0 ${width} ${height}" role="img" aria-label="${escapeHtml(title)}">${axes}<path d="${path}" fill="none" stroke="${color}" stroke-width="${axis?2:2.5}" stroke-linejoin="round" stroke-linecap="round"/>${valid.length===1?`<circle cx="${x(valid[0])}" cy="${y(valid[0])}" r="3" fill="${color}"/>`:''}</svg>`;
}
function topology(snapshot,summary){
  if(!snapshot?.nodes.length)return empty('Your network starts here','Validated nodes will appear as your LAN gateway forwards observations.');
  const cx=270,cy=117,r=94,nodes=snapshot.nodes;
  let lines='',items='';
  nodes.forEach((n,i)=>{const angle=-Math.PI/2+i/nodes.length*Math.PI*2,x=cx+Math.cos(angle)*r*1.6,y=cy+Math.sin(angle)*r;
    const online=summary.gatewayOnline&&n.seenAt!==null&&snapshot.capturedAt-n.seenAt<=LIMITS.nodeFreshMs;
    lines+=`<line class="edge ${online?'':'offline'}" x1="${cx}" y1="${cy}" x2="${x}" y2="${y}"/>`;
    const temperatureFresh=online&&n.temperature!==null&&n.temperatureAt!==null&&snapshot.capturedAt-n.temperatureAt<=LIMITS.nodeFreshMs;
    items+=`<g class="node-button" data-node="${n.id}" role="button" tabindex="0" aria-label="Inspect ${escapeHtml(n.label)}"><circle cx="${x}" cy="${y}" r="19" fill="${online?'#edf5e5':'#f0f1ee'}" stroke="${online?'#bbd7a8':'#d6ddd3'}"/><text x="${x}" y="${y+3}" text-anchor="middle" font-size="10" fill="${online?'#4a7140':'#9ba397'}">${String(n.id).padStart(2,'0')}</text><circle cx="${x+14}" cy="${y-13}" r="3.3" fill="${online?'#6f9e59':'#b5bdb1'}" stroke="var(--surface)" stroke-width="2"/><text x="${x}" y="${y+33}" text-anchor="middle" font-size="8" fill="var(--muted)">${temperatureFresh?number(n.temperature,1)+' °C':online?'No fresh sample':'Offline'}</text></g>`;
  });
  return `<svg viewBox="0 0 540 245" role="group" aria-label="Federation membership; links are not physical adjacency">${lines}<circle cx="${cx}" cy="${cy}" r="48" fill="none" stroke="#dfe9d6" stroke-dasharray="3 5"/><circle cx="${cx}" cy="${cy}" r="35" fill="#173b28"/><text x="${cx}" y="${cy-3}" text-anchor="middle" fill="#caed97" font-size="17">∑</text><text x="${cx}" y="${cy+14}" text-anchor="middle" fill="#caed97" font-size="6" letter-spacing="1.5">FEDERATION</text>${items}</svg>`;
}
export function nodeDetails(node,snapshot,now){
  const at=value=>value===null?'Never observed':`${Math.max(0,Math.round((now-value)/1000))}s ago`;
  return `<dl class="details-grid">${[['Temperature',number(node.temperature)+' °C'],['Local training loss',loss(node.loss)],['Round',node.round],['Samples',node.sampleCount.toLocaleString()],['Boot session',node.bootId||'Undiscovered'],['Heartbeat',at(node.seenAt)],['Temperature sample',at(node.temperatureAt)],['Model sample',at(node.modelAt)],['w₁',number(node.model.w1,6)],['w₂',number(node.model.w2,6)],['b',number(node.model.b,6)],['Gateway',snapshot.gatewayId]].map(([k,v])=>`<div><dt>${escapeHtml(k)}</dt><dd>${escapeHtml(v)}</dd></div>`).join('')}</dl><p class="caption">Latest observed values are retained here for inspection. Check sample ages before using them. Local loss is a training metric.</p>`;
}
export function render(state,now=Date.now()){
  const {snapshot,receivedAt,mode}=state,s=summarize(snapshot,now,receivedAt);
  const sim=mode==='demo';
  text('mode-label',sim?'SIMULATION':mode==='live'?'CLOUD CONNECTED':'DISCONNECTED');
  document.getElementById('mode-label').className=`mode-pill ${sim?'demo':mode==='live'?'live':''}`;
  const banner=document.getElementById('connection-banner');
  banner.hidden=mode==='live'&&!!snapshot;
  banner.querySelector('strong').textContent=sim?'Simulation workspace':mode==='live'?'Gateway not seen yet':'Connect your thermal network';
  banner.querySelector(':scope > div > span').textContent=sim?'Synthetic observations for exploration. These values do not come from physical boards.':mode==='live'?'The connection is authorized. Start your LAN bridge to populate this dashboard.':'Use a read token to view your gateway, or explore the interactive simulation.';
  document.getElementById('demo').hidden=sim;
  document.getElementById('connect').textContent=sim?'Connect real gateway':'Connect';
  const error=document.getElementById('error');error.hidden=!state.error;error.textContent=state.error||'';
  document.getElementById('metric-nodes').innerHTML=`${snapshot?s.online:'—'}<small>/ ${snapshot?s.total:'—'}</small>`;
  text('metric-nodes-note',snapshot?s.gatewayOnline?'Heartbeat window · 6 seconds':'Gateway stale · last snapshot retained':'Waiting for a gateway');
  document.getElementById('node-meter').style.width=`${s.total?s.online/s.total*100:0}%`;
  document.getElementById('metric-temperature').innerHTML=`${number(s.temperature,1)}<small>°C</small>`;
  text('metric-round',s.aggregate?.round??'—');
  text('metric-leader',s.aggregate?`Leader · Node ${String(s.aggregate.leaderId).padStart(2,'0')}`:'Awaiting validated aggregation');
  text('metric-loss',loss(s.loss));text('metric-participants',`${s.participants} fresh training participant${s.participants===1?'':'s'}`);
  const points=state.points.filter(p=>p.capturedAt>=now-state.range*60000);
  document.getElementById('temperature-spark').innerHTML=chart(points.slice(-20),'temperature',{axis:false,height:65});
  document.getElementById('loss-spark').innerHTML=chart(points.slice(-20),'loss',{axis:false,height:65,color:'#d99358'});
  text('topology-count',`${s.total} nodes`);document.getElementById('topology').innerHTML=topology(snapshot,s);
  text('aggregate-status',s.aggregate?`Node ${s.aggregate.leaderId} · boot ${s.aggregate.bootId} · round ${s.aggregate.round}`:'Waiting for a fresh aggregate');
  const weights=s.aggregate?.model;
  document.getElementById('weights').innerHTML=[['w₁','w1',2],['w₂','w2',.5],['b','b',10]].map(([label,key,bound])=>`<div class="weight-row"><label>${label}</label><div class="weight-track"><span style="width:${weights?Math.min(100,Math.abs(weights[key])/bound*100):0}%"></span></div><strong>${number(weights?.[key],5)}</strong></div>`).join('');
  text('aggregate-loss',loss(s.aggregate?.loss));
  document.getElementById('temperature-chart').innerHTML=chart(points,'temperature',{title:'Network mean temperature, degrees Celsius'});
  document.getElementById('loss-chart').innerHTML=chart(points,'loss',{color:'#d99358',title:'Mean local training loss, linear scale'});
  const all=snapshot?.nodes||[],isOnline=n=>s.gatewayOnline&&n.seenAt!==null&&snapshot.capturedAt-n.seenAt<=LIMITS.nodeFreshMs;
  const filtered=all.filter(n=>(n.label+' '+n.id).toLowerCase().includes(state.filter.toLowerCase())&&(state.status==='all'||isOnline(n)===(state.status==='online')));
  filtered.sort((a,b)=>state.sort==='id'?a.id-b.id:(a[state.sort]??Infinity)-(b[state.sort]??Infinity));
  document.getElementById('node-rows').innerHTML=filtered.map(n=>{
    const online=isOnline(n),tempFresh=online&&n.temperatureAt!==null&&snapshot.capturedAt-n.temperatureAt<=LIMITS.nodeFreshMs,
      modelFresh=online&&n.modelAt!==null&&snapshot.capturedAt-n.modelAt<=LIMITS.modelFreshMs,leader=s.aggregate?.leaderId===n.id;
    return `<tr><td><div class="node-name"><span class="node-monogram">${String(n.id).padStart(2,'0')}</span><div><strong>${escapeHtml(n.label)}</strong><small>ESP8266 · ID ${n.id}</small></div></div></td><td><span class="status-label ${online?'':'offline'}"><i class="dot ${online?'green':'gray'}"></i>${online?'Online':'Offline'}</span></td><td>${tempFresh?number(n.temperature)+' °C':'—'}</td><td>${modelFresh?loss(n.loss):'—'}</td><td>${modelFresh?n.round:'—'}</td><td>${modelFresh?n.sampleCount.toLocaleString():'—'}</td><td><span class="role-pill ${leader?'leader':''}">${leader?'Leader':'Peer'}</span></td><td><button class="row-button" data-node="${n.id}" aria-label="Inspect ${escapeHtml(n.label)}">↗</button></td></tr>`;
  }).join('')||`<tr><td colspan="8">${empty(all.length?'No nodes match these filters':'Waiting for your edge fleet',all.length?'Try another name, ID, or status.':'Connect the LAN bridge or explore the simulation.')}</td></tr>`;
  text('fleet-count',all.length);text('table-summary',`Showing ${filtered.length} of ${all.length} nodes`);
  text('last-capture',snapshot?`Captured ${Math.max(0,Math.round((now-snapshot.capturedAt)/1000))}s ago`:'Waiting for telemetry');
  text('freshness',state.paused?'Updates paused':sim?'Simulation · no physical data':snapshot?s.gatewayOnline?'Gateway reachable · 30s refresh':'Gateway stale · check LAN bridge':'No gateway connected');
  document.getElementById('freshness-dot').style.background=s.gatewayOnline?'#82ad6a':'#b1b9ad';
  document.getElementById('export').disabled=!snapshot;text('pause',state.paused?'Resume updates':'Pause updates');
  const c=snapshot?.counters;
  document.getElementById('activity-content').innerHTML=`<div class="activity-grid">${[['Temperature','temperature'],['Model updates','model'],['Aggregates','aggregation'],['Discovery','discovery'],['Rejected','rejected']].map(([label,key])=>`<div class="activity-item"><span>${label}</span><strong>${c?c[key].toLocaleString():'—'}</strong></div>`).join('')}</div><p class="caption">Counters reset when the LAN bridge restarts. Rejections include invalid envelopes, unknown boot sessions, and admission-limit failures. This view summarizes the latest capture; it is not a packet event log.</p>`;
}
