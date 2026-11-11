/* ArSonMix — one CONTROL-only user preset bar for all built-in FX editors.
 * Native .armixfx is the ONLY user library. This module never synthesizes
 * DSP acceptance, uses localStorage or sends an audio edit itself.
 * Adapters supply their existing SPSC edit + AUDIO ACK/readback receipt.
 */
(()=>{
"use strict";
const native=()=>typeof window.__JUCE__?.backend?.emitEvent==="function";
const backend=()=>window.__JUCE__?.backend;
const idValid=v=>typeof v==="string"&&
 /^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/i.test(v);
const clone=obj=>JSON.parse(JSON.stringify(obj));
const equal=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
const make=(tag,cls,text)=>{
 const e=document.createElement(tag);if(cls)e.className=cls;
 if(text!==undefined)e.textContent=text;
 return e;
};
function create(adapter){
 const {host,select,catalogId,context,stable,readAccepted,
        validate,apply,notice}=adapter;
 const sameSettings=typeof adapter.sameSettings==="function"?
     adapter.sameSettings:equal;
 // Most built-ins publish one parameter-set ACK. EQ publishes eight
 // sequential band ACKs; its deadline must outlast the owned batch.
 const recallTimeoutMs=Number.isFinite(adapter.recallTimeoutMs)?
     Math.max(5500,Math.min(60000,adapter.recallTimeoutMs)):5500;
 if(!host||!select||!catalogId||typeof context!=="function"||
    typeof apply!=="function")throw Error("Built-in FX preset adapter invalid");
 const saveAs=make("button","mx-fxp-button mx-fxp-save-as","+ Save As");
 const manage=make("button","mx-fxp-button mx-fxp-manage","···");
 saveAs.type=manage.type="button";
 saveAs.title="Save current accepted sound as My Preset";
 manage.title="Manage selected user preset";
 saveAs.setAttribute("aria-label","Save As user preset");
 manage.setAttribute("aria-label","Manage selected user preset");
 saveAs.disabled=manage.disabled=true;
 select.after(saveAs,manage);
 const menu=make("div","mx-fxp-menu");menu.hidden=true;
 menu.setAttribute("aria-label","My Presets actions");
 const options=[
  ["save","Save / Overwrite…"],["rename","Rename…"],["delete","Delete…"]
 ].map(([op,text])=>{
  const button=make("button","mx-fxp-action",text);
  button.type="button";button.dataset.operation=op;
  menu.append(button);return button;
 });
 const form=make("div","mx-fxp-form");form.hidden=true;
 form.setAttribute("role","group");form.setAttribute("aria-label","User preset action");
 const label=make("label","mx-fxp-prompt","MY PRESET");
 const input=make("input","mx-fxp-name");input.maxLength=80;
 input.autocomplete="off";input.spellcheck=false;
 input.setAttribute("aria-label","User preset name");
 const confirm=make("button","mx-fxp-confirm","Save");
 const cancel=make("button","mx-fxp-cancel","Cancel");
 confirm.type=cancel.type="button";
 form.append(label,input,confirm,cancel);
 host.append(menu,form);
 const originals=new Map();
 let selectedId="",saved=null,dirty=false,operation="",targetId="";
 let task=null,recall=null,verified=false,readyForList="",seq=0;
 let presetSelectIsChanging=false;

 function bound(){
  const c=context();
  return native()&&c&&!c.workspaceToken&&c.deviceEpoch!=="0"?c:null;
 }
 function binding(c){return c.instanceId+"@"+c.deviceEpoch+"/"+c.graphRevision;}
 function canWrite(){return verified&&Boolean(bound())&&stable()&&!task&&!recall;}
 function paint(){
  const c=bound(),can=canWrite();
  saveAs.disabled=!can;manage.disabled=!can||!selectedId||!originals.has(selectedId);
  const custom=select.querySelector('option[value="CUSTOM"]')||
               select.querySelector('option[value="Custom"]');
  if(custom)custom.textContent=dirty?"Custom · Modified":"Custom";
  for(const item of options)item.disabled=manage.disabled;
  if(selectedId&&!dirty&&originals.has(selectedId)){
   const value="user:"+selectedId;
   if(select.value!==value)select.value=value;
  }else if(selectedId&&dirty&&select.value==="user:"+selectedId){
   // A modified sound is not the selected saved file. Show the current
   // state so reselecting the user preset emits a genuine change event.
   const fallback=adapter.factoryValue(readAccepted());
   select.value=[...select.options].some(x=>x.value===fallback)?
     fallback:"CUSTOM";
  }
 }
 function closeForm(){form.hidden=true;operation="";targetId="";}
 function closeAll(){closeForm();menu.hidden=true;}
 function reset(){
  task=null;recall=null;verified=false;readyForList="";
  selectedId="";saved=null;dirty=false;closeAll();
  originals.clear();
  select.querySelector('optgroup[data-armix-fx-user]')?.remove();
  paint();
 }
 function request(op,args={}){
  const c=bound();
  if(!canWrite()||!c){
   notice("MY PRESET · WAIT FOR ACCEPTED DSP");return false;
  }
  const requestId="fxpres-"+Date.now()+"-"+(++seq);
  task={op,requestId,context:c,started:Date.now(),
        acceptedBefore:clone(readAccepted()),...args};
  backend().emitEvent("arMixAction",{action:"nativeBuiltinFxUserPreset",
     operation:op,requestId,catalogId,...c,...args});
  if(op!=="list")notice("MY PRESET · "+op.toUpperCase()+" PENDING");
  paint();return true;
 }
 function refresh(){
  const c=bound();
  if(!verified||!c||!stable()||task||recall||binding(c)===readyForList)return;
  request("list");
 }
 function rebuild(items){
  const optgroup=make("optgroup");optgroup.dataset.armixFxUser="1";
  optgroup.label="MY PRESETS";
  originals.clear();
  select.querySelector('optgroup[data-armix-fx-user]')?.remove();
  for(const item of items){
   if(!idValid(item?.id)||typeof item?.name!=="string")continue;
   originals.set(item.id,{id:item.id,name:item.name,category:item.category});
   const option=make("option","",item.name);
   option.value="user:"+item.id;optgroup.append(option);
  }
  if(optgroup.childElementCount)select.append(optgroup);
  if(selectedId&&!originals.has(selectedId)){
   selectedId="";saved=null;dirty=false;
  }
  paint();
 }
 function openForm(op,target=""){
  if(!canWrite()){notice("MY PRESET · WAIT FOR ACCEPTED DSP");return;}
  if(op!=="saveAs"&&op!=="recall"&&!originals.has(selectedId)){
   notice("SELECT A USER PRESET FIRST");return;
  }
  closeAll();operation=op;targetId=target;
  input.hidden=op!=="saveAs"&&op!=="rename";
  label.textContent={
   saveAs:"SAVE AS · MY PRESETS",save:"OVERWRITE THIS USER PRESET?",
   rename:"RENAME MY PRESET",delete:"DELETE PRESET? · AUDIO STAYS",
   recall:"DISCARD MODIFIED SOUND?"
  }[op];
  input.value=op==="saveAs"?
    (originals.get(selectedId)?.name||"My "+adapter.label):
    originals.get(selectedId)?.name||"";
  confirm.textContent={
   saveAs:"Save As",save:"Overwrite",rename:"Rename",
   delete:"Delete",recall:"Recall"
  }[op];
  form.hidden=false;
  if(!input.hidden){input.focus();input.select();}
  else confirm.focus();
 }
 function submit(){
  if(!operation)return;
  const name=input.value;
  if((operation==="saveAs"||operation==="rename")&&
     (!name||name.trim()!==name||name.length>80)){
   notice("PRESET NAME INVALID");return;
  }
  const args=operation==="saveAs"?{name}:
   operation==="rename"?{presetId:selectedId,name}:
   operation==="recall"?{presetId:targetId}:{presetId:selectedId};
  if(request(operation==="recall"?"read":operation,args))closeForm();
 }
 saveAs.addEventListener("click",()=>openForm("saveAs"));
 manage.addEventListener("click",()=>{if(!manage.disabled)menu.hidden=!menu.hidden;});
 options.forEach(button=>button.addEventListener("click",
  ()=>openForm(button.dataset.operation)));
 confirm.addEventListener("click",submit);
 cancel.addEventListener("click",closeForm);
 input.addEventListener("keydown",e=>{
  if(e.key==="Enter"){e.preventDefault();submit();}
  if(e.key==="Escape"){e.preventDefault();closeForm();}
 });
 function selectUser(value){
  if(!value.startsWith("user:"))return false;
  const target=value.slice(5);
  if(!originals.has(target)){paint();return true;}
  if(dirty&&selectedId)openForm("recall",target);
  else request("read",{presetId:target});
  // The option is never audio-applied merely by choosing it.
  if(selectedId&&!dirty)select.value="user:"+selectedId;
  else select.value=adapter.factoryValue(readAccepted());
  return true;
 }
 function factoryIntent(requestId){
  if(typeof requestId==="string"&&requestId)
   recall={factory:true,requestId,started:Date.now()};
 }
 function onAccepted(accepted,appliedId=""){
  verified=true;
  if(recall&&recall.requestId===appliedId){
   if(recall.factory){
    selectedId="";saved=null;dirty=false;
   }else if(validate(accepted)&&sameSettings(accepted,recall.settings)){
    selectedId=recall.presetId;saved=clone(recall.settings);dirty=false;
    notice("MY PRESET APPLIED · VERIFIED");
    }else {
     // The accepted DSP may already be different from the old sound.
     // Never report "audio unchanged" after an acknowledged write.
     if(selectedId&&saved)dirty=!sameSettings(accepted,saved);
     notice("MY PRESET READBACK MISMATCH · REVIEW ACCEPTED AUDIO");
    }
    recall=null;
  }else if(selectedId&&saved)dirty=!sameSettings(accepted,saved);
  paint();refresh();
 }
 backend()?.addEventListener?.("arMixBuiltinFxPresetResult",reply=>{
  if(!task||reply?.requestId!==task.requestId||
     reply.operation!==task.op||reply.catalogId!==catalogId)return;
  const c=bound();
  if(!c||reply.instanceId!==c.instanceId||
     reply.channelId!==c.channelId||reply.deviceEpoch!==c.deviceEpoch||
     reply.graphRevision!==c.graphRevision)return;
  const sent=task;task=null;
  if(reply.status!=="OK"){
   notice("MY PRESET REJECTED · "+(reply.errorCode||"UNKNOWN"));paint();return;
  }
  if(sent.op==="list"){
   readyForList=binding(c);
   rebuild(Array.isArray(reply.items)?reply.items:[]);return;
  }
  if(sent.op==="read"){
   if(reply.presetId!==sent.presetId||!originals.has(reply.presetId)||
      !validate(reply.settings)){
    notice("MY PRESET FILE INVALID · AUDIO UNCHANGED");paint();return;
   }
   const values=clone(reply.settings),accepted=readAccepted();
   if(sameSettings(values,accepted)){
    selectedId=sent.presetId;saved=values;dirty=false;paint();return;
   }
   const applied=apply(values);
   if(!applied){notice("MY PRESET DSP APPLY REJECTED");paint();return;}
   recall={presetId:sent.presetId,settings:values,requestId:applied,
           started:Date.now()};
   paint();return;
  }
  if(["saveAs","save","rename","delete"].includes(sent.op)){
   const id=sent.op==="saveAs"?reply.presetId:sent.presetId;
   if(!idValid(id)){
    notice("MY PRESET RESPONSE ID INVALID");paint();return;
   }
   if(sent.op==="delete"){
    if(selectedId===id){selectedId="";saved=null;dirty=false;}
    notice("USER PRESET DELETED · AUDIO UNCHANGED");
   }else if(sent.op==="rename"){
    notice("USER PRESET RENAMED · AUDIO UNCHANGED");
   }else{
    selectedId=id;saved=sent.acceptedBefore;
    dirty=!sameSettings(readAccepted(),saved);
    notice(dirty?"MY PRESET SAVED · SOUND HAS CHANGED":
                 "MY PRESET SAVED TO DISK");
   }
   readyForList="";paint();refresh();
  }
 });
 // Multi-edit effects can fail midway: release Recall truthfully, without
 // pretending the audio was unchanged or clearing native audio history.
 function rejectRecall(requestId,reason){
  if(!recall||recall.requestId!==requestId)return;
  recall=null;
  if(selectedId&&saved)dirty=!sameSettings(readAccepted(),saved);
  notice(reason);paint();
 }
 function tick(){
  if(task&&Date.now()-task.started>5000){
   task=null;notice("MY PRESET REQUEST TIMEOUT");paint();
  }
  if(recall&&Date.now()-recall.started>recallTimeoutMs){
   recall=null;notice("MY PRESET DSP ACK TIMEOUT");paint();
  }
 }
 return Object.freeze({reset,paint,refresh,selectUser,factoryIntent,
                       onAccepted,rejectRecall,tick,close:closeAll});
}
window.ArSonMixBuiltinFxPresetBar=Object.freeze({create});
})();
