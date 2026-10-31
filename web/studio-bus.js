// Studio Console M3E: real native ACK-backed independent GROUP pilot.
// This control projection NEVER guesses PCM or invents a CH11 in the browser.
// One existing JUCE callback owns the accepted dynamic ChannelId engine.
(() => {
  'use strict';
  const rail=document.querySelector('#app .left-rail');
  const anchor=document.getElementById('channelList');
  if(!rail||!anchor||document.getElementById('arStudioBusControls'))return;
  const native=!!window.__JUCE__?.backend?.emitEvent;
  const backend=window.__JUCE__?.backend;
  const selected=new Set();
  let last=-1,request='',pending=false,status=null,lastEpoch='';
  const panel=document.createElement('section');
  panel.id='arStudioBusControls';
  panel.className='arStudioBusControls';
  panel.setAttribute('aria-label','Studio native Group Bus');
  const title=document.createElement('div');
  title.className='arStudioBusTitle';
  title.textContent='GROUP BUS';
  const picked=document.createElement('small');
  picked.className='arStudioBusSelection';
  const action=document.createElement('button');
  action.type='button';action.className='btn-180 arStudioBusAction';
  const feedback=document.createElement('div');
  feedback.className='arStudioBusFeedback';
  feedback.setAttribute('role','status');
  const live=document.createElement('div');
  live.className='arStudioBusLive';
  panel.append(title,picked,action,feedback,live);
  anchor.after(panel);
  const route=()=>status?.dryRoutingLab||{};
  const accepted=()=>status?.studioBus||{};
  const validSelection=i=>Number.isInteger(i)&&i>=0&&i<10&&
    ['MONO','STEREO'].includes(
      String(route().acceptedRoute?.channels?.[i]?.mode||''));
  const selectedList=()=>[...selected].filter(validSelection).sort((a,b)=>a-b);
  function repaintSelection(){
    anchor.querySelectorAll('[data-choose]').forEach(row=>{
      const i=Number(row.dataset.choose);
      const on=selected.has(i);
      row.classList.toggle('arBusSelected',on);
      row.setAttribute('aria-checked',String(on));
    });
    document.querySelectorAll('.strip[data-strip]').forEach(strip=>{
      strip.classList.toggle('arBusSelected',selected.has(Number(strip.dataset.strip)));
    });
  }
  function render(){
    const d=route(),b=accepted(),ids=selectedList();
    const liveNow=!!b.active;
    picked.textContent=liveNow?
      'NATIVE BUS #'+String(b.groupChannelId||'?')+' · '+String(b.sourceCount||0)+' INPUTS':
      ids.length?ids.map(i=>'CH'+String(i+1).padStart(2,'0')).join(' + '):
      'Ctrl/Shift+click channel names to select';
    action.textContent=pending?'APPLYING…':liveNow?'STOP GROUP':'＋ BUS FOR SELECTED';
    const canApply=native&&!pending&&!!d.ready&&!!d.hasAcceptedRoute&&
      !d.pending&&!status?.monitor&&!status?.secondaryOutputArmed&&
      (liveNow||ids.length>0);
    action.disabled=!canApply;
    action.title=liveNow?'MUTE output and return to legacy 10CH route':
      '0FX native Group pilot · MUTE output first · Add new Bus ChannelId and route selected inputs exclusively';
    live.hidden=!liveNow;
    if(liveNow)live.textContent=String(b.name||'GROUP')+' · '+(b.sources||[]).join(', ')+
      ' → MAIN · NATIVE PCM';
    if(!native)feedback.textContent='Web preview only · no hardware audio';
    else if(!feedback.textContent)feedback.textContent=
      '0FX pilot · verify native capture, MUTE output, then create Group';
    repaintSelection();
  }
  // Extend normal single-focus behavior without replacing app.js and without
  // launching an extra routing controller. Ctrl and Shift work like a Console.
  document.addEventListener('click',e=>{
    if(!e.ctrlKey&&!e.metaKey&&!e.shiftKey)return;
    const el=e.target.closest?.('#channelList [data-choose],.strip [data-editstrip]');
    if(!el)return;
    const row=el.closest('[data-choose]');
    const i=Number(row?row.dataset.choose:el.dataset.editstrip);
    if(!validSelection(i))return;
    if(e.shiftKey&&last>=0){
      const lo=Math.min(last,i),hi=Math.max(last,i);
      for(let x=lo;x<=hi;++x)if(validSelection(x))selected.add(x);
    }else if(selected.has(i))selected.delete(i);
    else selected.add(i);
    last=i;
    render();
  },true);
  action.addEventListener('click',()=>{
    if(!native||pending)return;
    const d=route(),b=accepted(),stop=!!b.active;
    const indices=selectedList();
    if(!stop&&!indices.length)return;
    // A user action, never 12Hz telemetry, is allowed to submit one event.
    request='bus-'+Date.now()+'-'+Math.random().toString(36).slice(2);
    pending=true;
    feedback.textContent=stop?'STOPPING · waiting for native ACK':
      'CREATING · waiting for native graph ACK';
    render();
    backend.emitEvent('arMixAction',{
      action:stop?'studioBusStop':'studioBusCreateForSelected',
      requestId:request,
      expectedEpoch:Number(d.epoch)||0,
      expectedRevision:Number(d.revision)||0,
      channels:indices
    });
  });
  backend?.addEventListener?.('arMixStudioBusAck',ack=>{
    if(!pending||ack?.requestId!==request)return;
    pending=false;request='';
    feedback.textContent=ack?.ok?
      (ack.session?.active?'GROUP LIVE · native prepared PCM ACK':'GROUP STOPPED · native ACK'):
      'REJECTED · '+String(ack?.error||'Live graph unchanged');
    if(ack?.ok && status)status.studioBus=ack.session;
    render();
  });
  backend?.addEventListener?.('arMixStatus',s=>{
    status=s;
    const d=route(),key=String(d.epoch||0);
    if(lastEpoch!==key){
      lastEpoch=key;selected.clear();last=-1;
      if(pending){pending=false;feedback.textContent='CANCELLED · device epoch changed';}
    }
    // Cheap state projection: do not redraw strip DOM/meter graphics at 12Hz.
    render();
  });
  render();
})();
