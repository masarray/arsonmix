/* ArSon Vocal Throw: controls ONLY, on the single approved /preview/ mixer.
   No browser DSP or optimistic FX ACK: all state comes from JUCE PCM graph. */
(() => {
"use strict";
const ID="armix.builtin.vocal-throw.v1";
const defaults=()=>({mode:2,bpm:120,division:0,repeats:3,captureMs:460,
 silenceMs:220,thresholdDb:-35,wet:.42,decay:.55,ducking:.7});
const presets={
 Concert:defaults(),
 Fast:{...defaults(),division:1,repeats:4,captureMs:330,decay:.6},
 Ballad:{...defaults(),bpm:72,captureMs:650,wet:.48},
 Word:{...defaults(),mode:0,captureMs:280,silenceMs:180}
};
const ranges={bpm:[60,200],repeats:[1,6],captureMs:[80,1500],silenceMs:[80,900],
 thresholdDb:[-65,-15],wet:[0,1],decay:[.15,.9],ducking:[0,1]};
const valid=s=>s&&[0,1,2].includes(s.mode)&&[0,1,2,3].includes(s.division)&&
 Object.entries(ranges).every(([k,[min,max]])=>
 Number.isFinite(s[k])&&s[k]>=min&&s[k]<=max);
const same=(a,b)=>Object.keys(defaults()).every(k=>a[k]===b[k]);
const clamp=(v,min,max)=>Math.max(min,Math.min(max,v));
const backend=()=>window.__JUCE__?.backend;
const native=()=>typeof backend()?.emitEvent==="function";
const snapshot=()=>window.__ARMIX_NATIVE_M2__?.state?.()?.current;

const panel=document.createElement("section");
panel.className="mx-vthrow";panel.id="mxNativeVocalThrow";panel.hidden=true;
panel.setAttribute("role","dialog");panel.setAttribute("aria-modal","false");
panel.setAttribute("aria-label","ArSon Vocal Throw");
panel.innerHTML=[
 '<header class="vt-face-header"><div class="brand"><span class="brand-mark">ARSON</span><span class="brand-name">VOCAL THROW</span></div><span class="channel" id="vtChannel"></span>',
 '<label class="vt-preset"><select id="vtPreset" aria-label="Vocal Throw preset"><option value="Concert">Concert</option><option value="Fast">Fast Echo</option><option value="Ballad">Ballad</option><option value="Word">Last Word</option><option value="Custom" disabled>Custom</option></select></label>',
 '<button class="mx-comp-power armix-builtin-fx-power" id="vtPower" data-fx-state="unknown" aria-pressed="false" aria-label="Vocal Throw state unavailable" type="button" disabled>⏻</button><button class="mx-comp-close armix-floating-close armix-builtin-fx-close" id="vtClose" aria-label="Close Vocal Throw" title="Close Vocal Throw" type="button">×</button></header>',
 '<div class="body vt-hardware"><section class="vt-console" aria-label="Vocal Throw hardware controls">',
 '<div class="vt-instrument"><div class="vt-encoder-bank" id="vtEncodersLeft"></div>',
 '<div class="vt-center"><button id="vtThrow" class="vt-throw-button" type="button" aria-label="Throw captured vocal phrase" title="THROW — send the captured phrase into the echo"><span class="vt-throw-aura" aria-hidden="true"></span></button><span class="vt-throw-engraving" aria-hidden="true">THROW</span></div>',
 '<div class="vt-encoder-bank" id="vtEncodersRight"></div></div>',
 '<div class="vt-performance"><div class="vt-wet-module"><button class="vt-hero-knob" id="vtWetDial" role="slider" type="button" aria-label="Echo wet mix" aria-valuemin="0" aria-valuemax="100" aria-valuenow="42" tabindex="0"><span class="vt-hero-pointer" aria-hidden="true"></span><span class="vt-hero-readout"><small>WET</small><output id="vtWetValue">42%</output></span></button></div>',
 '<div class="vt-trigger"><div class="vt-mode-buttons" role="group" aria-label="Vocal trigger mode"><button type="button" data-mode="0">MAN</button><button type="button" data-mode="1">AUTO</button><button type="button" data-mode="2">HYBRID</button></div><select id="vtMode" class="vt-virtual-input" tabindex="-1" aria-hidden="true"><option value="0">Manual</option><option value="1">Auto</option><option value="2">Hybrid</option></select></div>',
 '<button id="vtTap" class="vt-tap-button" type="button"><span>TAP</span><small>BPM</small></button></div></section>',
 '<section class="vt-transport" aria-label="Tempo and music clock"><div class="vt-tempo"><span class="vt-small-label">BPM</span><div class="vt-clock-screen"><output id="vtDisplayBpm">120</output><span>BPM</span><span id="vtClockLabel">MAN</span></div><label class="vt-manual-bpm"><input id="vtBpm" type="number" min="60" max="200" step="1" aria-label="Manual BPM"></label></div>',
 '<div class="vt-pulse"><span class="vt-small-label">RHYTHM</span><div class="vt-division-buttons" role="group" aria-label="Delay division"><button type="button" data-division="0">1/4</button><button type="button" data-division="1">1/8</button><button type="button" data-division="2">1/8 D</button><button type="button" data-division="3">1/2</button></div><select id="vtDivision" class="vt-virtual-input" tabindex="-1" aria-hidden="true"><option value="0">1/4</option><option value="1">1/8</option><option value="2">1/8 D</option><option value="3">1/2</option></select><input id="vtRepeats" class="vt-virtual-input" type="number" min="1" max="6" step="1" tabindex="-1" aria-hidden="true"><div id="vtBeatRail" class="vt-repeat-leds" aria-label="Echo repeat count"><i></i><i></i><i></i><i></i><i></i><i></i><span id="vtRhythm" class="vt-screen-reader">1/4 · HYBRID</span></div></div>',
 '<div class="vt-music-source"><label for="vtMusicSource" class="vt-small-label">MUSIC</label><select id="vtMusicSource" aria-label="Music tempo source"><option value="0">Manual / TAP</option></select><span id="vtMusicStatus" class="music-status">TAP</span></div></section>',
 '<footer class="vt-status-rail"><span class="vt-device"><i class="dot" id="vtDot"></i><span id="vtStatus">LIVE</span></span><span class="vt-hint" id="vtHint" role="status"></span><span id="vtCount" title="Completed throw cues">0</span></footer></div>'
].join("");
document.body.append(panel);
const $=id=>panel.querySelector("#"+id);
const encoderMeta={
 captureMs:["CAPTURE","ms",5,"vtEncodersLeft"],
 silenceMs:["END","ms",10,"vtEncodersLeft"],
 thresholdDb:["GATE","dB",1,"vtEncodersLeft"],
 decay:["DECAY","%",.01,"vtEncodersRight"],
 ducking:["DUCK","%",.01,"vtEncodersRight"],
 repeats:["REPEATS","x",1,"vtEncodersRight"]
};
for(const [key,[title,unit,,bank]] of Object.entries(encoderMeta)){
 const node=document.createElement("div");
 node.className="vt-encoder";node.dataset.key=key;
 node.innerHTML='<span class="vt-encoder-name">'+title+'</span>'+
  '<button class="vt-encoder-face" type="button" role="slider" tabindex="0" aria-label="'+title+'"><span class="vt-encoder-needle" aria-hidden="true"></span></button>'+
  '<output class="vt-encoder-value"></output>';
 node.querySelector("output").dataset.unit=unit;
 $(bank).append(node);
}
let current=null,accepted=defaults(),draft=defaults(),pending=null,
 readId="",readSent=0,seq=1,taps=[],windowDrag=null;
let tempoPending=null,musicSource="0",musicReadback=null;
const context=()=>{
 const state=snapshot(),id=current&&window.__ARMIX_NATIVE_M2__?.nativeIdFor?.(current.track);
 const fx=state?.graph?.channels?.find(ch=>ch.id===id)?.inserts?.[current?.slot];
 if(!state?.admitted||!id||fx?.catalogId!==ID||fx?.id!==current?.instanceId)return null;
 return {channelId:id,slot:current.slot,instanceId:fx.id,
  deviceEpoch:state.device.epoch,graphRevision:state.graph.revision};
};
const hint=text=>{$("vtHint").textContent=text;};
function refreshMusicChannels(){
 const state=snapshot(),ctx=context(),selector=$("vtMusicSource");
 const selected=tempoPending?.source||musicSource;
 const candidates=(state?.graph?.channels||[]).filter(ch=>
  ch&&ch.id&&String(ch.id)!==String(ctx?.channelId)&&
  ["INPUT","GROUP","FX_RETURN","FXRETURN"].includes(
   String(ch.role||"").toUpperCase().replaceAll("-","_")));
 selector.replaceChildren(new Option("Manual / TAP","0"));
 for(const ch of candidates)selector.add(new Option(
  String(ch.name||ch.id).slice(0,45),String(ch.id)));
 if([...selector.options].some(x=>x.value===String(selected)))
  selector.value=String(selected);
 else selector.value="0";
 selector.disabled=!ctx||!native()||Boolean(tempoPending);
}
function renderMusicStatus(tempo){
 if(tempo)musicReadback=tempo;
 const label=$("vtMusicStatus"),clock=$("vtClockLabel"),number=$("vtDisplayBpm");
 const fallback=()=>{number.textContent=String(Math.round(accepted.bpm));clock.textContent="MAN";};
 if(tempoPending){label.textContent="CONNECT";label.title="Validating music channel";clock.textContent="CHECK";return;}
 if(musicSource==="0"){label.textContent="TAP";label.title="Manual BPM / TAP";fallback();return;}
 if(!musicReadback?.musicSourceActive){label.textContent="NO SIGNAL";label.title="Music source unavailable · manual BPM fallback";fallback();return;}
 if(!musicReadback.musicLocked){label.textContent="LISTEN";label.title="Listening for beat · manual BPM fallback";fallback();return;}
 const confidence=Math.round(Math.max(0,Math.min(1,Number(musicReadback.musicConfidence)||0))*100);
 const beatGrid=musicReadback.musicGridAvailable===true;
 const bpm=Math.round(Number(musicReadback.musicBpm));
 label.textContent=beatGrid?bpm+" · GRID":bpm+" · BPM";
 label.title="Auto tempo "+bpm+" BPM · "+confidence+"% confidence · "+(beatGrid?"beat grid":"BPM only");
 clock.textContent=beatGrid?"LOCK":"AUTO";
 number.textContent=String(bpm);
}
function syncPower(){
 const state=snapshot(),id=current&&window.__ARMIX_NATIVE_M2__?.nativeIdFor?.(current.track);
 const lane=state?.graph?.channels?.find(ch=>ch.id===id),fx=lane?.inserts?.[current?.slot];
 const value=!context()?"unknown":lane.fxRackBypassed?"rack-off":fx.bypassed?"off":"on";
 panel.dataset.fxState=value;
 const btn=$("vtPower");btn.dataset.fxState=value;
 btn.disabled=value==="unknown"||value==="rack-off";
 btn.title=value==="on"?"Active · click to bypass":value==="off"?"Bypassed · click to enable":
  value==="rack-off"?"FX rack bypassed":"Native FX unavailable";
 btn.setAttribute("aria-pressed",String(value==="on"));
 btn.setAttribute("aria-label",btn.title);
 $("vtThrow").disabled=value!=="on"||!!pending;
 $("vtStatus").textContent=value==="on"?
  ($("vtThrow").dataset.tail==="true"?"TAIL":"LIVE"):
  value==="off"?"BYPASS":value==="rack-off"?"RACK OFF":"OFFLINE";
}
function paint(){
 $("vtMode").value=String(draft.mode);$("vtBpm").value=String(draft.bpm);
 $("vtDivision").value=String(draft.division);$("vtRepeats").value=String(draft.repeats);
 for(const [key,[,unit]] of Object.entries(encoderMeta)){
  const node=panel.querySelector('.vt-encoder[data-key="'+key+'"]');
  const min=ranges[key][0],max=ranges[key][1];
  const pct=(draft[key]-min)/(max-min);
  const turn=-130+260*pct;
  node.style.setProperty("--turn",turn.toFixed(2)+"deg");
  node.querySelector(".vt-encoder-value").textContent=
   (unit==="%"?Math.round(draft[key]*100):Math.round(draft[key]))+unit;
  const dial=node.querySelector(".vt-encoder-face");
  dial.setAttribute("aria-valuemin",String(min));
  dial.setAttribute("aria-valuemax",String(max));
  dial.setAttribute("aria-valuenow",String(draft[key]));
  dial.setAttribute("aria-valuetext",node.querySelector(".vt-encoder-value").textContent);
 }
 const wetPct=Math.round(draft.wet*100);
 $("vtWetValue").textContent=wetPct+"%";
 $("vtWetDial").style.setProperty("--turn",(-130+260*draft.wet).toFixed(2)+"deg");
 $("vtWetDial").setAttribute("aria-valuenow",String(wetPct));
 $("vtWetDial").setAttribute("aria-valuetext",wetPct+" percent");
 for(const b of panel.querySelectorAll(".vt-mode-buttons button"))
  b.setAttribute("aria-pressed",String(Number(b.dataset.mode)===draft.mode));
 for(const b of panel.querySelectorAll(".vt-division-buttons button"))
  b.setAttribute("aria-pressed",String(Number(b.dataset.division)===draft.division));
 $("vtPreset").value=Object.entries(presets).find(([,v])=>same(v,draft))?.[0]||"Custom";
 const rhythmic=["1/4","1/8","DOTTED 1/8","1/2"][accepted.division];
 const modes=["MANUAL","AUTO","HYBRID"];
 $("vtRhythm").textContent=rhythmic+" · "+modes[accepted.mode];
 for(const [index,led] of [...$("vtBeatRail").querySelectorAll("i")].entries())
  led.dataset.on=String(index<accepted.repeats);
 renderMusicStatus();
 syncPower();
}
function requestSnapshot(force=false){
 const ctx=context();if(!native()||!ctx||panel.hidden)return;
 const now=Date.now();if(!force&&readId&&now-readSent<900)return;
 readId="throwsnap-"+now+"-"+(++seq);readSent=now;
 backend().emitEvent("arMixAction",{action:"nativeThrowSnapshot",requestId:readId,...ctx});
}
function send(command){
 const ctx=context();
 if(!native()||!ctx){hint("Native DSP unavailable");return false;}
 if(pending){hint("Waiting for DSP ACK");return false;}
 if(command==="nativeThrowSet"&&(!valid(draft)||same(draft,accepted)))return false;
 const requestId="throw-"+String(Date.now()*1000+(++seq%1000));
 pending={requestId,command,ack:false,created:Date.now()};
 const payload={action:command,requestId,...ctx};
 if(command==="nativeThrowSet")payload.settings={...draft};
 backend().emitEvent("arMixAction",payload);
 hint(command==="nativeThrowTrigger"?"Waiting for audio callback":"Applying to DSP");
 syncPower();return true;
}
for(const [key,id] of [["mode","vtMode"],["bpm","vtBpm"],
 ["division","vtDivision"],["repeats","vtRepeats"]]){
 $(id).addEventListener("change",()=>{
  draft={...draft,[key]:Number($(id).value)};paint();send("nativeThrowSet");
 });
}
// Physical rotary control: vertical pointer drag, mouse wheel and keyboard.
// Update visual locally during movement; issue exactly ONE accepted native
// settings intent at drag release. No DOM/audio DSP loops are introduced.
function bindRotary(face,key,step){
 const [min,max]=ranges[key];
 const quantize=v=>clamp(Math.round(v/step)*step,min,max);
 let gesture=null;
 const update=v=>{if(pending)return;draft={...draft,[key]:quantize(v)};paint();};
 face.addEventListener("pointerdown",e=>{
  if(e.button!==0||pending)return;
  gesture={id:e.pointerId,y:e.clientY,initial:draft[key],changed:false};
  face.setPointerCapture(e.pointerId);e.preventDefault();
 });
 face.addEventListener("pointermove",e=>{
  if(!gesture||gesture.id!==e.pointerId)return;
  const next=quantize(gesture.initial+(gesture.y-e.clientY)*(max-min)/168);
  if(next!==draft[key]){gesture.changed=true;update(next);}
  e.preventDefault();
 });
 const finish=e=>{
  if(!gesture||e.pointerId!==gesture.id)return;
  const changed=gesture.changed;gesture=null;
  if(face.hasPointerCapture?.(e.pointerId))face.releasePointerCapture(e.pointerId);
  if(changed)send("nativeThrowSet");
 };
 face.addEventListener("pointerup",finish);
 face.addEventListener("pointercancel",()=>{gesture=null;paint();});
 face.addEventListener("wheel",e=>{
  if(pending)return;
  e.preventDefault();update(draft[key]+(e.deltaY<0?step:-step));
  send("nativeThrowSet");
 },{passive:false});
 face.addEventListener("keydown",e=>{
  const delta={ArrowUp:1,ArrowRight:1,ArrowDown:-1,ArrowLeft:-1,PageUp:5,PageDown:-5}[e.key];
  if(delta===undefined&&e.key!=="Home"&&e.key!=="End")return;
  e.preventDefault();
  update(e.key==="Home"?min:e.key==="End"?max:draft[key]+delta*step);
  send("nativeThrowSet");
 });
 face.addEventListener("dblclick",()=>{
  if(pending)return;
  update(defaults()[key]);send("nativeThrowSet");
 });
}
for(const [key,[,,step]] of Object.entries(encoderMeta))
 bindRotary(panel.querySelector('.vt-encoder[data-key="'+key+'"] .vt-encoder-face'),key,step);
bindRotary($("vtWetDial"),"wet",.01);
for(const button of panel.querySelectorAll(".vt-mode-buttons button")){
 button.addEventListener("click",()=>{
  $("vtMode").value=button.dataset.mode;
  $("vtMode").dispatchEvent(new Event("change",{bubbles:true}));
 });
}
for(const button of panel.querySelectorAll(".vt-division-buttons button")){
 button.addEventListener("click",()=>{
  $("vtDivision").value=button.dataset.division;
  $("vtDivision").dispatchEvent(new Event("change",{bubbles:true}));
 });
}
$("vtMusicSource").addEventListener("change",()=>{
 const ctx=context(),source=String($("vtMusicSource").value);
 if(!ctx||!native()||tempoPending){refreshMusicChannels();return;}
 const requestId="tempo-"+Date.now()+"-"+(++seq);
 tempoPending={requestId,source,created:Date.now()};
 backend().emitEvent("arMixAction",{
  action:"nativeThrowTempoSource",requestId,sourceChannelId:source,...ctx});
 renderMusicStatus();refreshMusicChannels();
});
$("vtPreset").addEventListener("change",()=>{
 const p=presets[$("vtPreset").value];if(!p)return;
 draft={...p};paint();send("nativeThrowSet");
});
$("vtThrow").addEventListener("click",()=>send("nativeThrowTrigger"));
$("vtTap").addEventListener("click",()=>{
 const now=performance.now();
 if(taps.length&&now-taps[taps.length-1]>2500)taps=[];
 taps.push(now);if(taps.length>6)taps.shift();
 if(taps.length<3){hint("Tap at least three beats");return;}
 const deltas=taps.slice(1).map((time,i)=>time-taps[i]).filter(v=>v>=280&&v<=1200);
 if(deltas.length<2){hint("Tap a steady beat");return;}
 const ordered=[...deltas].sort((a,b)=>a-b);
 const median=ordered[Math.floor(ordered.length/2)];
 draft={...draft,bpm:clamp(Math.round(60000/median),60,200)};
 paint();send("nativeThrowSet");
});
$("vtPower").addEventListener("click",()=>{
 if(current&&context())window.__ARMIX_NATIVE_M2__?.consumeAction?.(
  current.track,"slotPower",{dataset:{slot:String(current.slot)}});
});
$("vtClose").addEventListener("click",()=>{panel.hidden=true;pending=null;tempoPending=null;readId="";});
window.addEventListener("keydown",e=>{
 if(e.key==="Escape"&&!panel.hidden){panel.hidden=true;pending=null;readId="";}
});
const head=panel.querySelector("header");
head.addEventListener("pointerdown",e=>{
 if(e.button||e.target.closest("button,select"))return;
 const box=panel.getBoundingClientRect();
 windowDrag={id:e.pointerId,dx:e.clientX-box.left,dy:e.clientY-box.top};
 head.setPointerCapture(e.pointerId);e.preventDefault();
});
head.addEventListener("pointermove",e=>{
 if(!windowDrag||e.pointerId!==windowDrag.id)return;
 panel.style.left=clamp(e.clientX-windowDrag.dx,0,Math.max(0,innerWidth-panel.offsetWidth))+"px";
 panel.style.top=clamp(e.clientY-windowDrag.dy,0,Math.max(0,innerHeight-panel.offsetHeight))+"px";
});
head.addEventListener("pointerup",e=>{windowDrag=null;head.releasePointerCapture?.(e.pointerId);});
window.ArSonMixNativeVocalThrow=Object.freeze({
 id:ID,
 insert(target){return Boolean(window.__ARMIX_NATIVE_M2__?.insertPlugin?.(ID,target));},
 open(track,slot){
  current={track,slot,instanceId:null};pending=null;tempoPending=null;musicSource="0";musicReadback=null;readId="";
  const state=snapshot(),id=window.__ARMIX_NATIVE_M2__?.nativeIdFor?.(track);
  const insert=state?.graph?.channels?.find(ch=>ch.id===id)?.inserts?.[slot];
  if(native()&&state?.admitted&&insert?.catalogId===ID&&insert.id){
   current.instanceId=insert.id;draft=defaults();accepted=defaults();
   hint("");
  }else if(!native()){draft=defaults();accepted=defaults();
   hint("Browser preview only · no PCM");}
  else hint("Native FX instance unavailable");
  $("vtChannel").textContent=track.name||"CH";
  panel.hidden=false;paint();refreshMusicChannels();requestSnapshot(true);
 },
 close(){panel.hidden=true;}
});
backend()?.addEventListener?.("arMixBuiltinThrowTempoAck",reply=>{
 if(!tempoPending||reply?.requestId!==tempoPending.requestId)return;
 if(reply.status==="APPLIED"){
  musicSource=tempoPending.source;
  hint("");
 }else hint("Tempo source rejected · "+(reply.errorCode||"STALE"));
 tempoPending=null;refreshMusicChannels();renderMusicStatus();
 requestSnapshot(true);
});
backend()?.addEventListener?.("arMixBuiltinThrowQueue",reply=>{
 if(!pending||reply?.requestId!==pending.requestId)return;
 if(reply.status==="REJECTED"){hint("DSP rejected · "+(reply.errorCode||"STALE"));
  pending=null;requestSnapshot(true);syncPower();}
});
backend()?.addEventListener?.("arMixBuiltinThrowAck",reply=>{
 if(!pending||reply?.requestId!==pending.requestId)return;
 if(reply.status!=="APPLIED"){hint("DSP rejected · "+(reply.errorCode||"UNKNOWN"));
  pending=null;requestSnapshot(true);syncPower();return;}
 if(pending.command==="nativeThrowTrigger"){hint("");
  pending=null;requestSnapshot(true);syncPower();}
 else{pending.ack=true;requestSnapshot(true);}
});
backend()?.addEventListener?.("arMixBuiltinThrowSnapshot",reply=>{
 if(panel.hidden||reply?.requestId!==readId)return;
 readId="";readSent=0;
 const ctx=context();
 if(!ctx||reply.status!=="ACCEPTED"||reply.channelId!==ctx.channelId||
    reply.instanceId!==ctx.instanceId||reply.deviceEpoch!==ctx.deviceEpoch||
    reply.graphRevision!==ctx.graphRevision||!valid(reply.settings)||
    reply.addedLatencySamples!==0){hint("Stale FX graph · reopen editor");return;}
 if(reply.musicSourceChannelId!==undefined) {
  musicSource=String(reply.musicSourceChannelId||"0");
  refreshMusicChannels();renderMusicStatus(reply);
 }
 $("vtCount").textContent=String(reply.throwCount||0);
 $("vtCount").setAttribute("aria-label",String(reply.throwCount||0)+" completed throws");
 $("vtDot").classList.toggle("active",reply.tailActive===true);
 $("vtBeatRail").dataset.playing=String(reply.tailActive===true);
 $("vtThrow").dataset.tail=String(reply.tailActive===true);
 $("vtStatus").textContent=reply.tailActive?"TAIL":"LIVE";
 if(pending){
  if(!pending.ack||reply.appliedRequestId!==pending.requestId)return;
  accepted={...reply.settings};draft={...accepted};pending=null;
  hint("");
 }else{accepted={...reply.settings};draft={...accepted};}
 paint();
});
backend()?.addEventListener?.("arMixConsoleSnapshot",()=>{
 if(panel.hidden)return;
 if(native()&&!context()){
  pending=null;tempoPending=null;
  hint("FX graph changed · reopen");
 }
 refreshMusicChannels();syncPower();
});
window.setInterval(()=>{
 if(panel.hidden)return;
 if(pending&&Date.now()-pending.created>3500){hint("DSP ACK timeout");pending=null;}
 if(tempoPending&&Date.now()-tempoPending.created>3500){
  hint("Tempo source ACK timeout");tempoPending=null;refreshMusicChannels();
 }
 if(native())requestSnapshot();
 syncPower();
},250);
})();
