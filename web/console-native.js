'use strict';
/* Native-only control adapter. All sound ownership stays in JUCE. */
(()=>{
 const backend=window.__JUCE__?.backend,m=window.ArSonMixNativeModel;
 if(!backend?.emitEvent||!backend?.addEventListener||!m)return;
 let n=0;const pending=new Map(),last=new Map();
 const notice=x=>{const el=document.getElementById('info');if(el)el.textContent=String(x);};
 const stop=e=>{e.preventDefault();e.stopImmediatePropagation();e.stopPropagation();};
 const send=(operation,t,payload={})=>{
  const s=m.snapshot(),ops=m.supported();
  if(!s||!ops.includes(operation)){notice(operation+' not admitted in installed native engine');return false;}
  const legacy=s.productionCallbackLive!==true;
  if(legacy&&(!t?.nativeId||s.legacyM4?.controlPlane!=='LEGACY_M4_ORDINAL')){
   notice('No accepted physical route; audio state unchanged');return false;
  }
  const requestId='arsonmix-console:'+(++n);
  const request={requestId,operation,targetChannelId:t?.nativeId||null,payload,
   deviceEpoch:s.device.epoch,baseGraphRevision:s.graph.revision,
   baseRosterRevision:s.graph.rosterRevision};
  if(legacy)request.legacyRoutingRevision=s.legacyM4.routingRevision;
  pending.set(requestId,operation);
  backend.emitEvent('arMixConsoleIntent',request);return true;
 };
 function field(el,phase){
  const t=m.row(el),kind=el?.dataset?.field;if(!t?.nativeId||!kind)return;
  const v=Number(el.value);let op,payload;
  if(kind==='level'&&Number.isFinite(v)){
   op=t.kind==='fx'?'SET_AUX_FADER':'SET_FADER';
   payload=t.kind==='fx'?{value:Math.pow(10,v/20)}:{valueDb:v};m.visual(t,kind,v);
  }else if(kind==='pan'&&Number.isFinite(v)){
   op='SET_PAN';payload={value:v};m.visual(t,kind,v);
  }else if(kind==='input'&&/^in:[0-9]+$/.test(el.value)){
   op='SET_SOURCE';payload={portId:el.value,sourceFormat:'MONO'};
  }else if(kind==='output'){
   op='SET_PRIMARY_OUT';payload={destinationId:el.value==='Master'?'MAIN':'NONE'};
  }else{notice(kind+' not admitted; no simulated DSP');return;}
  const key=t.nativeId+':'+op,time=Date.now();
  if(phase==='input'&&(kind==='level'||kind==='pan')&&time-(last.get(key)||0)<40)return;
  last.set(key,time);send(op,t,payload);
 }
 window.ArSonMixNativeBridge=Object.freeze({field});
 const blocked=new Set(['preview','reset','saveScene','recallScene','sceneSafe','copy','paste',
  'group','ungroup','clearCue','sofToggle','pluginsButton','pluginImport','pluginScan',
  'pluginAddPath','routeOnly','routeParallel','wireMaster','wireWsUndo']);
 document.addEventListener('click',e=>{
  const b=e.target.closest?.('button');if(!b)return;
  if(b.id==='nativeDevice'){stop(e);backend.emitEvent('arMixAction',{action:'audioSetup'});return;}
  if(blocked.has(b.id)){stop(e);notice(b.id+' needs native implementation');return;}
  if(['addTrack','addBus','addFx'].includes(b.id)){
   stop(e);const op={addTrack:'CREATE_CHANNEL',addBus:'CREATE_GROUP_BUS',addFx:'CREATE_FX_RETURN'}[b.id];
   send(op,null,b.id==='addTrack'?{format:'MONO'}:{});return;
  }
  const a=b.dataset.action;if(!a)return;
  if(['format','mute','solo','slotPower','openPlugin'].includes(a)){
   stop(e);const t=m.row(b);if(!t)return;
   if(a==='format')send('SET_PROCESSING_FORMAT',t,{format:t.format==='mono'?'STEREO':'MONO'});
   else if(a==='mute'||a==='solo')send(a==='mute'?'SET_MUTE':'SET_SOLO',t,{enabled:!t[a]});
   else{
    const fx=t.nativeFxSlots?.[Number(b.dataset.slot)];
    if(!fx?.instanceId){notice('No real VST3 instance for this slot');return;}
    const op=a==='slotPower'?'SET_INSERT_BYPASS':'OPEN_INSERT_EDITOR';
    send(op,t,a==='slotPower'?{insertId:fx.instanceId,bypassed:!fx.bypassed}:{insertId:fx.instanceId});
   }return;
  }
  if(a!=='resetClip'){stop(e);notice(a+' requires native graph support');}
 },true);
 for(const type of ['input','change'])document.addEventListener(type,e=>{
  if(e.target?.dataset?.field&&e.target.closest?.('.console-body')){stop(e);field(e.target,type);}
 },true);
 document.addEventListener('pointerdown',e=>{
  if(e.target.closest?.('.wire-tray,.wire-node-in,.wire-node-out,.wire-pot,.wire-gain')){
   stop(e);notice('WIRE routing requires native accepted graph');
  }
 },true);
 document.addEventListener('dblclick',e=>{
  const t=m.row(e.target);if(!t)return;
  if(e.target.closest?.('.center-pan')){stop(e);send('SET_PAN',t,{value:0});}
  else if(e.target.closest?.('.fader')){stop(e);send('SET_FADER',t,{valueDb:0});}
  else if(e.target.closest?.('.footer-label,.strip-identity-top')){stop(e);notice('Native rename unavailable');}
 },true);
 document.addEventListener('keydown',e=>{
  if((e.ctrlKey||e.metaKey)&&['c','v','g'].includes(e.key.toLowerCase())){stop(e);notice('Native group/copy unavailable');}
 },true);
 backend.addEventListener('arMixConsoleSnapshot',m.project);
 backend.addEventListener('arMixConsoleAck',ack=>{
  const op=pending.get(ack?.requestId);if(!op)return;
  pending.delete(ack.requestId);
  notice(op+(ack.status==='ACCEPTED'?' accepted by JUCE; snapshot pending':' REJECTED: '+(ack.errorCode||'unavailable')));
 });
 backend.addEventListener('arMixNotice',e=>{if(e?.text)notice(e.text);});
 for(const id of ['preview','reset','saveScene','recallScene','sceneSafe','copy','paste','group','ungroup','clearCue','sofToggle','sofTarget']){
  const el=document.getElementById(id);if(el)el.disabled=true;
 }
 backend.emitEvent('arMixGuiReady',{});
 backend.emitEvent('arMixConsoleSubscribe',{protocol:'v6'});
 notice('Native JUCE console connected · AUDIO DEVICE → IN → MAIN');
})();