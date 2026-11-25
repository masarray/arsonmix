/* SPDX-License-Identifier: AGPL-3.0-only */
/* The approved native WebView's only de-Ezzer editor transport. No browser DSP. */
(function(root){
"use strict";
const catalog="armix.builtin.de-ezzer.v1";
const fields=["channelId","deviceEpoch","graphRevision","instanceId","slot"];
const same=(a,b)=>!!a&&!!b&&fields.every(k=>a[k]===b[k]);
function contextFor(view,channelId,slot){
 if(!view?.admitted||view.retained||view.logical||!Number.isInteger(slot)||slot<0||slot>=8)return null;
 const row=view.graph?.channels?.find(ch=>ch.id===channelId),insert=row?.inserts?.[slot];
 if(!insert||insert.catalogId!==catalog||typeof insert.id!=="string"||!insert.id||typeof channelId!=="string"||typeof view.device?.epoch!=="string"||typeof view.graph.revision!=="string")return null;
 return {channelId,deviceEpoch:view.device.epoch,graphRevision:view.graph.revision,instanceId:insert.id,slot};
}
function createTransport({backend,context,initialContext,token,now=()=>performance.now(),onInitial,onFault}){
 let listener=null,presetListener=null,read=null,write=null,serial=0,closed=false;
 const current=()=>same(context(),initialContext);
 const emit=(action,requestId,extra={})=>backend.emitEvent("arMixAction",{...initialContext,action,requestId,...extra});
 function fault(){if(closed)return;closed=true;listener?.disconnect();onFault?.();}
 function tick(){
  if(closed)return;
  if(!current()){fault();return;}
  if(read&&now()-read.at>1000){fault();return;}
  if(!read){read={id:token+"-r"+(++serial),at:now()};try{emit("nativeDeEzzerSnapshot",read.id);}catch{fault();}}
 }
 function send(command){
  if(closed||!current()||write||command.audition!=="Normal")return false;
  write=token+"-w"+command.sequence;
  try{emit("nativeDeEzzerSet",write,{command});return true;}catch{write=null;fault();return false;}
 }
 function receive(kind,payload){
  if(closed||!same(initialContext,payload)||!current())return false;
  if(kind==="snapshot"&&read?.id===payload.requestId){
   read=null;if(payload.ok!==true||!payload.snapshot){fault();return false;}
   if(onInitial){const initial=onInitial;onInitial=null;initial(payload.snapshot);}else listener?.telemetry(payload.snapshot);
   return true;
  }
  if(kind==="queue"&&write===payload.requestId){if(payload.ok!==true){write=null;fault();}return true;}
  if(kind==="ack"&&write===payload.requestId){write=null;if(payload.ok!==true||!payload.ack){fault();return false;}listener?.ack(payload.ack);return true;}
  return false;
 }
 return Object.freeze({send,tick,receive,receivePreset(p){if(!closed&&current()&&same(initialContext,{...p,slot:initialContext.slot}))presetListener?.(p);},subscribePresetResult(fn){presetListener=fn;},subscribe(s){listener=s;return ()=>{listener=null;presetListener=null;closed=true;};},cancelAudition(){/* Native product route never admits audition. */}});
}
if(typeof module==="object"&&module.exports){module.exports={contextFor,createTransport};return;}
const api=root.ArSonMixNativeDeEzzer,backend=root.__JUCE__?.backend;
if(!api||!backend?.emitEvent||!backend?.addEventListener)return;
const doc=root.document,host=doc.createElement("div");host.id="mxNativeDeEzzerHost";host.hidden=true;
Object.assign(host.style,{position:"fixed",left:"0",top:"0",width:"min(640px,100%)",zIndex:"16000"});doc.body.append(host);
let active=null;
for(const [kind,event] of [["snapshot","arMixBuiltinDeEzzerSnapshot"],["queue","arMixBuiltinDeEzzerQueue"],["ack","arMixBuiltinDeEzzerAck"]])backend.addEventListener(event,p=>active?.transport.receive(kind,p));
backend.addEventListener("arMixBuiltinFxPresetResult",p=>active?.transport.receivePreset(p));
function close(){active?.editor.dispose();}
function open(track,slot){
 const context=()=>{const n=root.__ARMIX_NATIVE_M2__;return contextFor(n?.fxEditorState?.(),n?.nativeIdFor?.(track),slot);};
 const initialContext=context();if(!initialContext)return false;
 close();host.hidden=false;
 const token="dez-"+root.crypto.randomUUID();let editor,timer;
 const transport=createTransport({backend,context,initialContext,token,onInitial:s=>editor.model.attach(s)});
 editor=api.mount(host,{transport,monitorSafe:false,presetContext:()=>same(context(),initialContext)?initialContext:null,close:()=>{clearInterval(timer);host.hidden=true;active=null;}});
 active={editor,transport};timer=setInterval(()=>transport.tick(),50);transport.tick();return true;
}
root.ArSonMixNativeDeEzzer=Object.freeze({...api,open,close});
})(globalThis);
