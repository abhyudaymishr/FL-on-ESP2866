// The tour only controls presentation; telemetry and credentials stay in the app.
const STORAGE_KEY='pifl-dashboard-tour-v1';
const steps=[
  {
    title:'Your edge network, at a glance.',
    description:'Welcome to the PIFL control room. Take a quick tour to learn where to connect, read your telemetry, and inspect a board.',
    tips:['No boards nearby? Explore the simulation with clearly labeled sample data.','Have a live lab? Connect with your read-only dashboard token.']
  },
  {
    title:'Choose where your data comes from.',view:'overview',target:'connection',location:'Connection status · top of the dashboard',
    description:'Choose Connect and enter your read-only token to view your lab. The LAN bridge must be running on the boards’ network to send observations.',
    tips:['An authorized connection can still be empty until the bridge sends a capture.','Explore simulation uses synthetic data. Check the status badge to see which mode you are viewing.']
  },
  {
    title:'Start with the network metrics.',view:'overview',target:'network-metrics',location:'Overview · network metrics',
    description:'These cards summarize online boards, mean temperature, the latest aggregate round, and local training loss.',
    tips:['Online describes the latest capture. Check freshness before treating it as current.','A lower training loss alone does not prove better prediction accuracy.']
  },
  {
    title:'Follow the changes over time.',view:'overview',target:'history-panel',location:'Overview · temperature and training-loss charts',
    description:'Use the time-range menu to explore the last 15 minutes through 24 hours. The same window applies to both charts.',
    tips:['History keeps one observation per minute. Gaps indicate missing observations.','Pause updates to inspect a snapshot; resume when you want fresh data again.']
  },
  {
    title:'Get to know an individual board.',view:'nodes',target:'fleet',location:'Node explorer · searchable telemetry table',
    description:'Search by node name or ID, filter by status, and sort the table by temperature or loss. Use a row’s arrow to open its inspector.',
    tips:['The inspector includes sample ages, model weights, boot session, and heartbeat.','You can also select a board in the Overview network diagram.']
  },
  {
    title:'You’re ready to explore.',view:'learning',target:'view-learning',location:'Learning · how the network learns',
    description:'Learning explains the physics and federated model. Activity shows the gateway’s session counters. Ask the guide for help interpreting telemetry.',
    tips:['Export snapshot downloads the current observations as JSON.','Start with the simulation, connect your gateway, or finish the tour and explore at your own pace.']
  }
];

export function createOnboarding({navigate,restore,startDemo,openConnection,connectionTarget}){
  const $=id=>document.getElementById(id),dialog=$('tour-dialog');
  let index=0,highlighted=null,origin=null,dismissed=false;
  // Storage can be unavailable in private or restricted browser contexts.
  const seen=()=>{
    if(dismissed)return true;
    for(const name of ['localStorage','sessionStorage']){
      try{if(window[name].getItem(STORAGE_KEY))return true;}catch{}
    }
    return false;
  };
  function remember(){
    dismissed=true;
    for(const name of ['localStorage','sessionStorage']){
      try{window[name].setItem(STORAGE_KEY,'seen');}catch{}
    }
  }
  function clearHighlight(){highlighted?.classList.remove('tour-highlight');highlighted=null;}
  function showStep(){
    const step=steps[index];clearHighlight();
    if(step.view)navigate(step.view);
    dialog.classList.toggle('tour-welcome',index===0);
    $('tour-count').textContent=`Step ${index+1} of ${steps.length}`;
    $('tour-progress').max=steps.length;$('tour-progress').value=index+1;
    $('tour-title').textContent=step.title;$('tour-description').textContent=step.description;
    $('tour-tips').replaceChildren(...step.tips.map(tip=>{const item=document.createElement('li');item.textContent=tip;return item;}));
    $('tour-location').hidden=!step.location;$('tour-location').textContent=step.location||'';
    $('tour-back').hidden=index===0;
    $('tour-next').textContent=index===0?'Start tour →':index===steps.length-1?'Finish tour':'Next →';
    $('tour-shortcuts').hidden=index!==0&&index!==steps.length-1;
    $('tour-skip').textContent=index===0?'Skip for now':'Skip tour';
    if(step.target){
      highlighted=$(step.target==='connection'?connectionTarget():step.target);
      if(highlighted&&!highlighted.hidden){highlighted.classList.add('tour-highlight');highlighted.scrollIntoView({block:'start',behavior:'instant'});}
    }
    dialog.scrollTop=0;$('tour-title').focus({preventScroll:true});
  }
  function close(){
    if(!dialog.open)return;
    remember();clearHighlight();dialog.close();
    if(origin){restore(origin.hash);window.scrollTo({top:origin.y,left:origin.x,behavior:'instant'});}
    const focus=origin?.focus?.isConnected&&origin.focus!==document.body?origin.focus:$('open-tour');
    focus?.focus({preventScroll:true});origin=null;
  }
  function open(){
    if(dialog.open||document.querySelector('dialog[open]'))return;
    origin={hash:location.hash,x:window.scrollX,y:window.scrollY,focus:document.activeElement};
    index=0;dialog.showModal();showStep();
  }
  $('open-tour').addEventListener('click',open);
  $('tour-next').addEventListener('click',()=>{if(index===steps.length-1)close();else{index++;showStep();}});
  $('tour-back').addEventListener('click',()=>{if(index>0){index--;showStep();}});
  for(const id of ['tour-close','tour-skip'])$(id).addEventListener('click',close);
  dialog.addEventListener('cancel',event=>{event.preventDefault();close();});
  $('tour-demo').addEventListener('click',()=>{close();navigate('overview');window.scrollTo({top:0,behavior:'instant'});startDemo();});
  $('tour-connect').addEventListener('click',()=>{close();openConnection();});
  return {open,showOnFirstVisit(){if(!seen())open();}};
}
