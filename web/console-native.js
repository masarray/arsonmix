// SPDX-License-Identifier: AGPL-3.0-only
// PRODUCTION GUI. No demo PCM, fake plugin catalog, or browser-authored audio graph.
// Render the approved single-console geometry from native accepted snapshots ONLY.
(() => {
'use strict';
const api=window.__JUCE__?.backend;
const el=id=>document.getElementById(id);
const esc=x=>String(x??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const palette=['#1699cd','#b92e41','#cf6b1e','#b28c27','#34a544','#168762','#0e95c9','#066fa9','#7140b5','#b52893','#c32974','#586889'];
const pending=new Map(),lastValues=new Map();
let snapshot=null,rows=[],bank=0,selected=null,serial=0,lastSignature='',native=!!(api?.emitEvent&&api?.addEventListener);
let controlsEnabled=false;
const announce=(message,problem=false)=>{const n=el('info');if(n){n.textContent=message;n.style.color=problem?'#e2a5a5':'';}};
const opAvailable=op=>!!(snapshot?.productionCallbackLive?
    snapshot.graph?.capabilities?.supportedOps?.includes(op):
    (op.startsWith('NATIVE_')?
      snapshot?.legacyM4?.nativeBus?.supportedOps?.includes(op):
      snapshot?.legacyM4?.supportedOps?.includes(op)));
const liveLegacy=()=>snapshot?.legacyM4?.controlPlane==='LEGACY_M4_ORDINAL'&&
                         Array.isArray(snapshot.legacyM4?.acceptedChannels);
const channelId=id=>String(id);
const fmt=v=>{const n=Number(v);return Number.isFinite(n)?n.toFixed(1):'0.0'};
function normalize(s){
 const graph=s?.graph,legacy=s?.legacyM4,actual=[];
 if(s?.productionCallbackLive&&Array.isArray(graph?.channels)&&graph.channels.length){
   for(const ch of graph.channels)actual.push({
     id:channelId(ch.id),name:String(ch.name||ch.role||'CHANNEL'),role:ch.role,
     format:ch.processingFormat||'MONO',source:ch.source?.portId||'',
     out:ch.primaryOutId||'',controls:ch.controls||{},fx:ch.inserts||[],
     sends:ch.sends||[],legacy:false
   });
 }else if(liveLegacy()){
   const list=legacy.acceptedChannels;
   for(let i=0;i<list.length;i++){
     const config=list[i]||{},control=legacy.channelControls?.[i]||{};
     actual.push({id:String(i+1),name:String(config.name||'CH '+String(i+1).padStart(2,'0')),
       role:'INPUT',format:String(config.format||'MONO'),
       source:config.source||'',out:config.output||'MAIN',controls:control,
       fx:legacy.fxInstances?.[i]||[],sends:[],legacy:true});
   }
   const extras=legacy.nativeBus?.channels||[];
   for(const ch of extras) {
     if(ch.role!=='INPUT'||actual.some(x=>x.id===String(ch.id)))continue;
     const patch=ch.source||null;
     actual.push({id:String(ch.id),name:String(ch.name||'Channel '+ch.id),
       role:'INPUT',format:String(ch.format||'MONO'),
       source:patch?.portId||'',out:String(ch.primaryOutput||''),
       controls:{faderDb:20*Math.log10(Math.max(Number(ch.fader)||0,1e-6)),
                 pan:(Number(ch.pan)||0)*100,mute:!!ch.mute,solo:!!ch.solo},
       fx:[],sends:ch.parallelSends||[],legacy:false,nativeScoped:true});
   }
 }
 return actual;
}
function listedRows(){const filter=el('viewFilter')?.value||'all';
 return rows.filter(r=>filter==='all'||filter==='solo'&&!!r.controls.solo||
   filter==='audio'&&r.role==='INPUT'||filter==='bus'&&r.role==='GROUP'||
   filter==='fx'&&r.role==='FX_RETURN');
}
function gain(v){const n=Number(v);return Number.isFinite(n)?Math.max(-60,Math.min(10,n)):-60}
function sourceOptions(row){
 const list=['<option value="">UNPATCHED</option>'];
 for(const e of snapshot?.device?.inputEndpoints||[]){
  if(!e.active)continue;
  list.push('<option value="'+esc(e.id)+'"'+(row.source===e.id?' selected':'')+'>'+esc(e.label||e.id)+'</option>');
 }
 return list.join('');
}
function outputOptions(row){
 let opts=['<option value="MAIN">MAIN</option>'];
 for(const d of snapshot?.graph?.destinations||[])
  if(d.role==='GROUP'||d.role==='MAIN')opts.push('<option value="'+esc(d.id)+'"'+
  (row.out===d.id?' selected':'')+'>'+esc(d.name||d.id)+'</option>');
 return opts.join('');
}
function nativeStrip(r){
 const main=r.role==='MAIN',db=gain(r.controls.faderDb),pan=Number(r.controls.pan)||0;
 const can=op=>opAvailable(op)&&native;
 const act=(op,attrs='')=>' data-op="'+op+'" data-id="'+esc(r.id)+'"'+attrs+
    (can(op)?'':' disabled title="Requires native accepted operation"');
 const role=main?'master':r.role==='GROUP'?'bus':r.role==='FX_RETURN'?'fx':'audio';
 const color=palette[(rows.indexOf(r)+palette.length)%palette.length];
 const meterTick=[0,-6,-12,-18,-24,-36,-48,-60].map(v=>'<span class="vu-tick" style="top:'+(-v/60*100)+'%"><span>'+v+'</span></span>').join('');
 const gainTick=[10,0,-6,-12,-24,-36,-48,-60].map(v=>'<span class="gain-tick" style="top:'+((10-v)/70*100)+'%"><span>'+v+'</span></span>').join('');
 const format=String(r.format).toUpperCase()==='STEREO'?'STEREO':'MONO';
 const fx=(r.fx||[]).slice(0,8).map(slot=>{
   const token=esc(slot.instanceId||slot.id||'');
   return '<div class="fxslot"><button class="fxslot-name"'+act('OPEN_INSERT_EDITOR',' data-insert="'+token+'"')+'>'+esc(slot.name||'VST3')+'</button>'+
   '<button class="fxslot-bypass"'+act('SET_INSERT_BYPASS',' data-insert="'+token+'" data-bypassed="'+!!slot.bypassed+'"')+'>'+(slot.bypassed?'OFF':'ON')+'</button></div>';
 }).join('');
 return '<article class="strip '+role+'" data-id="'+esc(r.id)+'" style="--track-color:'+color+';--ch:'+color+'" aria-label="'+esc(r.name)+'">'+
   '<div class="strip-identity-top"><span class="identity-number">'+(main?'MASTER':esc(r.id))+'</span><span class="identity-name">'+esc(r.name)+'</span></div>'+
   '<div class="section route-section"><div class="route-top"><span class="route-indicator">IN / OUT</span><button class="mini format-badge"'+act('SET_PROCESSING_FORMAT')+'>'+ (format==='MONO'?'M':'ST')+'</button></div>'+
   '<div class="route-field"><span class="route-caption">IN</span><select class="route-select"'+act(r.nativeScoped?'NATIVE_PATCH_CHANNEL':'SET_SOURCE')+'>'+sourceOptions(r)+'</select></div>'+
   '<div class="route-field"><span class="route-caption">OUT</span><select class="route-select"'+act('SET_PRIMARY_OUT')+'>'+outputOptions(r)+'</select></div></div>'+
   '<div class="section fx-section"><div class="fxslot-list">'+fx+
   '<button class="fxslot-add" disabled title="LOAD_FX not admitted by native engine">+ ADD VST3</button></div></div>'+
   '<div class="section send-section"><span class="route-caption">SEND</span><span class="route-identity">'+esc((r.sends||[]).length)+' native sends</span></div>'+
   '<div class="section pan-section"><span class="pan-icon">PAN</span><span class="pan-control" style="--pan-start:'+Math.min(50,50+pan/2)+'%;--pan-end:'+Math.max(50,50+pan/2)+'%"><input class="center-pan" type="range" min="-100" max="100" step="1" value="'+pan+'"'+act('SET_PAN')+'></span><output>'+Math.round(pan)+'</output></div>'+
   '<div class="section switch-section"><span class="buttons-wrapper codepen-buttons-wrapper">'+
   '<button class="button switch mute '+(r.controls.mute?'on':'')+'"'+act('SET_MUTE')+' aria-pressed="'+!!r.controls.mute+'"><span class="fa">MUTE</span></button>'+
   '<button class="button switch solo '+(r.controls.solo?'on':'')+'"'+act('SET_SOLO')+' aria-pressed="'+!!r.controls.solo+'"><span class="fa">SOLO</span></button>'+
   '</span><button class="switch cue" disabled title="Native CUE not admitted">PFL</button></div>'+
   '<div class="fader-section"><div class="fader-head"><output class="fader-readout">'+fmt(db)+' dB</output><span class="clip-indicator">CLIP</span></div>'+
   '<div class="fader-workspace"><div class="meter-box '+(format==='MONO'?'mono':'stereo')+'" aria-label="Native measured dBFS"><div class="vu-bars"><div class="meter-lane"><div class="meter-fill left"></div></div>'+
   (format==='MONO'?'':'<div class="meter-lane"><div class="meter-fill right"></div></div>')+
   '</div><div class="vu-scale"><div class="vu-scale-travel">'+meterTick+'</div></div></div>'+
   '<div class="fader-box" style="--fader-fill:'+((db+60)/70*100)+'%;--fader-y:'+(100-(db+60)/70*100)+'%"><div class="fader-travel"><div class="cap"></div></div>'+
   '<input class="fader" type="range" min="-60" max="10" step="0.1" value="'+db+'"'+act('SET_FADER')+'></div>'+
   '<div class="fader-scale"><div class="gain-scale-travel">'+gainTick+'</div></div></div></div>'+
   '<div class="bottom-section"><div class="db-value">'+fmt(db)+'</div><div class="footer-label"><span class="name">'+esc(r.name)+'</span><span class="footer-icon">♫</span><div class="color-strip"></div></div></div>'+
   '</article>';
}
function render(){
 const list=listedRows(),max=Math.max(1,Math.ceil(list.length/8));bank=Math.min(bank,max-1);
 const start=bank*8,visible=list.slice(start,start+8);
 el('tracks').innerHTML=visible.map(nativeStrip).join('');
 const main=rows.find(r=>r.role==='MAIN');
 el('master').innerHTML=main?nativeStrip(main):'';
 el('trackCount').textContent=rows.length+' NATIVE CHANNELS · '+(snapshot?.productionCallbackLive?'UNIVERSAL':'LEGACY M4 COMPAT');
 const selector=el('bankSelect');selector.innerHTML=Array.from({length:max},(_,i)=>'<option value="'+i+'">BANK '+(i+1)+'</option>').join('');selector.value=String(bank);
 el('bankPrev').disabled=bank===0;el('bankNext').disabled=bank>=max-1;
 if(el('nativeAddAudio'))el('nativeAddAudio').disabled=!(opAvailable('CREATE_CHANNEL')||opAvailable('NATIVE_CREATE_CHANNEL'));
 for(const [id,op] of [['addTrack','CREATE_CHANNEL'],['addBus','CREATE_GROUP'],['addFx','CREATE_FX_RETURN'],['clearCue','SET_CUE']])
  if(el(id))el(id).disabled=!snapshot?.productionCallbackLive||!opAvailable(op);
 for(const id of ['saveScene','recallScene','sceneSafe','sofToggle','pluginsButton','group','ungroup','copy','paste','pluginScan','pluginImport','pluginAddPath','routeParallel','routeOnly','wireMaster','wireMode'])
  if(el(id))el(id).disabled=true;
 if(el('pluginBrowser'))el('pluginBrowser').hidden=true;
 paintMeters();
}
function paintMeters(){
 if(!snapshot)return;
 const legacy=snapshot.legacyM4||{};
 for(const r of rows){
  const meter=document.querySelector('.strip[data-id="'+CSS.escape(r.id)+'"] .meter-box');
  if(!meter)continue;
  const index=Number(r.id)-1;
  const dynamic=snapshot.telemetry?.channelMeterRmsPeak?.[r.id];
  let left=r.legacy?Number(legacy.channelPeaksL?.[index]):Number(dynamic);
  let right=r.legacy?Number(legacy.channelPeaksR?.[index]):Number(dynamic);
  for(const [side,value] of [['left',left],['right',right]]){
   const node=meter.querySelector('.meter-fill.'+side);
   if(!node)continue;
   const amp=Number.isFinite(value)?Math.max(0,value):0;
   const db=amp>0?20*Math.log10(amp):-60;
   node.style.setProperty('--lit',Math.max(0,Math.min(100,(db+60)/60*100))+'%');
  }
 }
}
function send(op,row,payload){
 if(!api||!snapshot){announce('NATIVE ENGINE NOT CONNECTED',true);return;}
 if(!opAvailable(op)){announce(op+' NOT AVAILABLE IN INSTALLED AUDIO ENGINE',true);return;}
 if(pending.size){announce('Awaiting prior native ACK',true);return;}
 const graph=snapshot.graph||{},device=snapshot.device||{};
 const requestId='console-'+Date.now().toString(36)+'-'+(++serial).toString(36);
 const envelope={requestId,deviceEpoch:String(device.epoch),baseGraphRevision:String(graph.revision),
   baseRosterRevision:String(graph.rosterRevision),operation:op,targetChannelId:row.id,payload};
 if(row.legacy)envelope.legacyRoutingRevision=String(snapshot.legacyM4.routingRevision);
 if(op.startsWith('NATIVE_')){
   const bus=snapshot.legacyM4?.nativeBus;
   if(!bus){announce('NATIVE PCM ROSTER NOT ADMITTED',true);return;}
   envelope.legacyRoutingRevision=String(snapshot.legacyM4.routingRevision);
   envelope.nativeGraphRevision=String(bus.graphRevision);
   envelope.nativeRosterRevision=String(bus.rosterRevision);
 }
 pending.set(requestId,{op,row:row.id});
 announce('PENDING · '+op+' · awaiting native ACK');
 api.emitEvent('arMixConsoleIntent',envelope);
}
function rowFor(id){return rows.find(x=>x.id===id);}
function submitNode(n){
 const row=rowFor(n.dataset.id),op=n.dataset.op;
 if(!row||!op)return;
 if(op==='NATIVE_PATCH_CHANNEL'){
    const pin=snapshot?.device?.inputEndpoints?.find(x=>x.id===n.value);
    if(pin)send(op,row,{physicalLeft:pin.port,physicalRight:-1,sourceFormat:'MONO'});
 }
 else if(op==='SET_SOURCE')send(op,row,{portId:n.value,sourceFormat:'MONO'});
 else if(op==='SET_PROCESSING_FORMAT')send(op,row,{format:row.format==='MONO'?'STEREO':'MONO'});
 else if(op==='SET_PRIMARY_OUT')send(op,row,{destinationId:n.value});
 else if(op==='SET_FADER')send(op,row,{valueDb:Number(n.value)});
 else if(op==='SET_PAN')send(op,row,{value:Number(n.value)});
 else if(op==='SET_MUTE'||op==='SET_SOLO')send(op,row,{enabled:!row.controls[op==='SET_MUTE'?'mute':'solo']});
 else if(op==='OPEN_INSERT_EDITOR')send(op,row,{insertId:n.dataset.insert});
 else if(op==='SET_INSERT_BYPASS')send(op,row,{insertId:n.dataset.insert,bypassed:n.dataset.bypassed!=='true'});
}
function accept(s){
 if(!s||s.schemaVersion!==6)return;
 snapshot=s;rows=normalize(s);
 const signature=JSON.stringify([rows.map(r=>[r.id,r.name,r.role,r.format,r.source,r.out,r.controls,r.fx]),s.legacyM4?.supportedOps,s.legacyM4?.nativeBus?.supportedOps,s.graph?.capabilities?.supportedOps]);
 if(signature!==lastSignature){lastSignature=signature;render();}else paintMeters();
 el('transport').textContent=s.device?.outputArmed?'OUTPUT ARMED':'OUTPUT DISARMED';
 if(el('nativeOutput')){
   el('nativeOutput').textContent=s.device?.outputArmed?'OUTPUT ARMED · MUTE':'OUTPUT MUTED · ARM';
   el('nativeOutput').setAttribute('aria-pressed',String(!!s.device?.outputArmed));
 }
 if(el('nativeDevice'))el('nativeDevice').title='Configure native audio device: '+String(s.device?.backend||'UNCONNECTED');
 announce(rows.length?('NATIVE SNAPSHOT · '+rows.length+' accepted channels · '+(s.device?.backend||'DEVICE NOT READY')):'NATIVE ENGINE · No accepted PCM channels');
}
function ack(a){
 const key=a?.requestId,op=pending.get(key);
 if(!op)return;
 pending.delete(key);
 announce(a.status==='ACCEPTED'?op.op+' ACCEPTED · verifying native snapshot':
   op.op+' REJECTED · '+(a.errorCode||'UNKNOWN'),a.status!=='ACCEPTED');
 // Never mutate GUI state from ACK alone; subsequent native snapshot is authoritative.
}
document.addEventListener('change',e=>{
 const n=e.target.closest('[data-op]');
 if(n)submitNode(n);
 if(e.target.id==='bankSelect'){bank=Number(e.target.value)||0;render();}
 if(e.target.id==='viewFilter'){bank=0;render();}
 if(e.target.id==='densitySelect')document.querySelector('.console-body').dataset.density=e.target.value;
});
document.addEventListener('click',e=>{
 const n=e.target.closest('button[data-op]');
 if(n)submitNode(n);
 if(e.target.closest('#bankPrev')){bank=Math.max(0,bank-1);render();}
 if(e.target.closest('#bankNext')){bank++;render();}
 const section=e.target.closest('[data-section]');
 if(section){const n=section.dataset.section,body=document.querySelector('.console-body');
   body.classList.toggle('collapsed-'+n);section.setAttribute('aria-expanded',String(!body.classList.contains('collapsed-'+n)));}
});
document.getElementById('nativeDevice')?.addEventListener('click',()=>{
 if(!native)return;
 api.emitEvent('arMixAction',{action:'audioSetup'});
});
document.getElementById('nativeOutput')?.addEventListener('click',()=>{
 if(!native)return;
 const armed=!!snapshot?.device?.outputArmed;
 const args={action:'monitor',value:!armed};
 if(!armed&&snapshot?.legacyM4?.controlPlane==='LEGACY_M4_ORDINAL'){
   args.expectedEpoch=Number(snapshot.device?.epoch);
   args.expectedRevision=Number(snapshot.legacyM4.routingRevision);
 }
 api.emitEvent('arMixAction',args);
 announce(!armed?'REQUESTING NATIVE OUTPUT ARM':'REQUESTING OUTPUT MUTE');
});
document.getElementById('nativeAddAudio')?.addEventListener('click',()=>{
 if(!native)return;
 const name='Channel '+(rows.length+1),op=opAvailable('CREATE_CHANNEL')?
   'CREATE_CHANNEL':opAvailable('NATIVE_CREATE_CHANNEL')?'NATIVE_CREATE_CHANNEL':null;
 if(op)send(op,{id:'0',legacy:false},{name,format:'MONO'});
});
for(const id of ['wireView','wireMode','sofTarget'])if(el(id))el(id).disabled=true;
if(!native){announce('NATIVE JUCE BACKEND REQUIRED · NO SIMULATED AUDIO',true);return;}
api.addEventListener('arMixConsoleSnapshot',accept);
api.addEventListener('arMixConsoleAck',ack);
api.addEventListener('arMixAudioArmAck',a=>{
 announce(a?.ok?(a.armed?'OUTPUT ARMED · Native audio':'OUTPUT MUTED · Safe'):
   'OUTPUT ARM REJECTED · '+(a?.error||'DEVICE/ROUTE NOT READY'),!a?.ok);
});

api.addEventListener('arMixStatus',s=>{if(s?.error)announce(String(s.error),true);});
api.addEventListener('arMixNotice',s=>{if(s?.text)announce(String(s.text),!!s.error);});
const start=()=>{api.emitEvent('arMixGuiReady',{protocol:1});
  api.emitEvent('arMixConsoleSubscribe',{protocol:'v6',requester:'native-console'});
};
if(document.fonts?.ready)document.fonts.ready.then(()=>requestAnimationFrame(start),()=>requestAnimationFrame(start));
else requestAnimationFrame(start);
})();