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
const fields={captureMs:["Capture","ms",5],silenceMs:["End pause","ms",10],
 thresholdDb:["Voice gate","dB",1],wet:["Echo mix","%",.01],
 decay:["Decay","%",.01],ducking:["Vocal duck","%",.01]};
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
 '<header><span class="brand">Vocal Throw</span><span class="channel" id="vtChannel"></span>',
 '<select id="vtPreset" aria-label="Vocal Throw preset"><option value="Concert">Concert</option><option value="Fast">Fast Echo</option><option value="Ballad">Ballad</option><option value="Word">Last Word</option><option value="Custom" disabled>Custom</option></select>',
 '<button class="power" id="vtPower" aria-label="FX power" type="button">⏻</button><button class="close armix-floating-close" id="vtClose" aria-label="Close Vocal Throw" type="button">×</button></header>',
 '<div class="body"><div class="row"><span class="label">TRIGGER MODE</span><select id="vtMode" aria-label="Trigger mode"><option value="0">Manual</option><option value="1">Auto</option><option value="2">Hybrid</option></select></div>',
 '<div class="topline"><div class="field"><label for="vtBpm">TEMPO · BPM</label><input id="vtBpm" type="number" min="60" max="200" step="1"></div>',
 '<div class="field"><label for="vtDivision">BEAT</label><select id="vtDivision"><option value="0">1/4</option><option value="1">1/8</option><option value="2">1/8 D</option><option value="3">1/2</option></select></div>',
 '<div class="field"><label for="vtRepeats">REPEATS</label><input id="vtRepeats" type="number" min="1" max="6" step="1"></div></div>',
 '<div class="manual"><button id="vtThrow" class="throw" type="button">THROW</button><button id="vtTap" class="tap" type="button">TAP BPM</button></div>',
 '<div class="hint" id="vtHint">Native DSP · waiting for accepted channel</div>',
 '<details class="detail"><summary>Phrase & echo shaping</summary><div class="ranges" id="vtRanges"></div></details>',
 '<div class="status"><span><span class="dot" id="vtDot"></span><span id="vtStatus">FX READY</span></span><span id="vtCount">0 throws</span></div></div>'
].join("");
document.body.append(panel);
const $=id=>panel.querySelector("#"+id);
for(const [key,[title,unit,step]] of Object.entries(fields)){
 const label=document.createElement("label");label.className="range";label.dataset.key=key;
 label.innerHTML='<span>'+title+'</span><output></output><input type="range">';
 const input=label.querySelector("input"),bounds=ranges[key];
 input.min=String(bounds[0]);input.max=String(bounds[1]);input.step=String(step);
 input.setAttribute("aria-label",title);label.querySelector("output").dataset.unit=unit;
 $("vtRanges").append(label);
}
let current=null,accepted=defaults(),draft=defaults(),pending=null,
 readId="",readSent=0,seq=1,taps=[],windowDrag=null;
const context=()=>{
 const state=snapshot(),id=current&&window.__ARMIX_NATIVE_M2__?.nativeIdFor?.(current.track);
 const fx=state?.graph?.channels?.find(ch=>ch.id===id)?.inserts?.[current?.slot];
 if(!state?.admitted||!id||fx?.catalogId!==ID||fx?.id!==current?.instanceId)return null;
 return {channelId:id,slot:current.slot,instanceId:fx.id,
  deviceEpoch:state.device.epoch,graphRevision:state.graph.revision};
};
const hint=text=>{$("vtHint").textContent=text;};
function syncPower(){
 const state=snapshot(),id=current&&window.__ARMIX_NATIVE_M2__?.nativeIdFor?.(current.track);
 const lane=state?.graph?.channels?.find(ch=>ch.id===id),fx=lane?.inserts?.[current?.slot];
 const value=!context()?"unknown":lane.fxRackBypassed?"rack-off":fx.bypassed?"off":"on";
 const btn=$("vtPower");btn.dataset.state=value;
 btn.disabled=value==="unknown"||value==="rack-off";
 btn.title=value==="on"?"Active · click to bypass":value==="off"?"Bypassed · click to enable":
  value==="rack-off"?"FX rack bypassed":"Native FX unavailable";
 btn.setAttribute("aria-pressed",String(value==="on"));
 $("vtThrow").disabled=value!=="on"||!!pending;
}
function paint(){
 $("vtMode").value=String(draft.mode);$("vtBpm").value=String(draft.bpm);
 $("vtDivision").value=String(draft.division);$("vtRepeats").value=String(draft.repeats);
 for(const [key,[,unit]] of Object.entries(fields)){
  const label=$("vtRanges").querySelector('[data-key="'+key+'"]');
  label.querySelector("input").value=String(draft[key]);
  label.querySelector("output").textContent=
   (unit==="%"?Math.round(draft[key]*100):Math.round(draft[key]))+unit;
 }
 $("vtPreset").value=Object.entries(presets).find(([,v])=>same(v,draft))?.[0]||"Custom";
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
for(const key of Object.keys(fields)){
 const input=$("vtRanges").querySelector('[data-key="'+key+'"] input');
 input.addEventListener("input",()=>{
  const value=Number(input.value),bounds=ranges[key];
  draft={...draft,[key]:clamp(value,bounds[0],bounds[1])};paint();
 });
 input.addEventListener("change",()=>send("nativeThrowSet"));
}
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
$("vtClose").addEventListener("click",()=>{panel.hidden=true;pending=null;readId="";});
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
  current={track,slot,instanceId:null};pending=null;readId="";
  const state=snapshot(),id=window.__ARMIX_NATIVE_M2__?.nativeIdFor?.(track);
  const insert=state?.graph?.channels?.find(ch=>ch.id===id)?.inserts?.[slot];
  if(native()&&state?.admitted&&insert?.catalogId===ID&&insert.id){
   current.instanceId=insert.id;draft=defaults();accepted=defaults();
   hint("Reading accepted DSP settings");
  }else if(!native()){draft=defaults();accepted=defaults();
   hint("Browser preview only · no PCM");}
  else hint("Native FX instance unavailable");
  $("vtChannel").textContent=track.name||"CH";
  panel.hidden=false;paint();requestSnapshot(true);
 },
 close(){panel.hidden=true;}
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
 if(pending.command==="nativeThrowTrigger"){hint("THROW executed by audio callback");
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
 $("vtCount").textContent=String(reply.throwCount||0)+" throws";
 $("vtDot").classList.toggle("active",reply.tailActive===true);
 $("vtStatus").textContent=reply.tailActive?"ECHO TAIL":"NATIVE DSP";
 if(pending){
  if(!pending.ack||reply.appliedRequestId!==pending.requestId)return;
  accepted={...reply.settings};draft={...accepted};pending=null;
  hint("Native DSP settings accepted");
 }else{accepted={...reply.settings};draft={...accepted};}
 paint();
});
backend()?.addEventListener?.("arMixConsoleSnapshot",()=>{
 if(panel.hidden)return;
 if(native()&&!context()){pending=null;hint("FX graph changed · reopen");}
 syncPower();
});
window.setInterval(()=>{
 if(panel.hidden)return;
 if(pending&&Date.now()-pending.created>3500){hint("DSP ACK timeout");pending=null;}
 if(native())requestSnapshot();
 syncPower();
},250);
})();
