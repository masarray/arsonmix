// SPDX-License-Identifier: AGPL-3.0-only
import {mount,valid,normalize,factoryPresets,factoryValue,sameSettings} from './editor.mjs';
import {nativeTransport} from './native_transport.mjs';
// In the approved resource bundle only, style is injected from the same
// editor.css. No browser PCM, no second mixer, no public preview fallback.
(()=>{
 const backend=window.__JUCE__?.backend;
 if(!backend?.emitEvent||!backend?.addEventListener)return;
 const ID='armix.builtin.anti-noiz.v1';
 const root=document.createElement('div');root.id='mxNativeAntiNoiz';root.hidden=true;
 document.body.append(root);
 const style=document.createElement('style');style.textContent=antiNoizStyle;document.head.append(style);
 let current=null,view=null,timer=null,transport=null,presets=null,latest=null;
 function target(){
  if(!current||root.hidden)return null;
  const api=window.__ARMIX_NATIVE_M2__;
  const n=api?.fxEditorState?.(),id=api?.nativeIdFor?.(current.track);
  const lane=n?.graph?.channels?.find(x=>x.id===id),insert=lane?.inserts?.[current.slot];
  if(!(n?.admitted||n?.logical)||!insert||insert.catalogId!==ID||insert.id!==current.instanceId)return null;
  if(typeof lane.fxRackBypassed!=='boolean'||typeof insert.bypassed!=='boolean'||
     (!n.logical&&typeof n.raw?.globalFxBypassed!=='boolean'))return null;
  const power=n.logical?'LOGICAL':n.raw.globalFxBypassed?'GLOBAL':lane.fxRackBypassed?'RACK':insert.bypassed?'SLOT':'ON';
  return {power,context:{channelId:id,deviceEpoch:n.logical?'0':n.device.epoch,graphRevision:n.graph.revision,
   instanceId:insert.id,slot:current.slot,...(n.logical?{workspaceToken:n.workspaceToken}:{})}};
 }
 const context=()=>target()?.context||null;
 const hostPower=()=>target()?.power||'UNAVAILABLE';
 function close(){
  clearInterval(timer);timer=null;presets?.dispose();presets=null;
  view?.dispose();view=null;transport=null;current=null;latest=null;root.hidden=true;
 }
 function open(track,slot){
  close();
  const api=window.__ARMIX_NATIVE_M2__,n=api?.fxEditorState?.(),id=api?.nativeIdFor?.(track);
  const insert=n?.graph?.channels?.find(x=>x.id===id)?.inserts?.[slot];
  if(!(n?.admitted||n?.logical)||insert?.catalogId!==ID||!insert.id)return;
  current={track,slot,instanceId:insert.id};root.hidden=false;
  transport=nativeTransport(backend,context);
  view=mount(root,transport,{nativeAssets:true,onClose:close,hostPower,
   // Slot bypass uses the same graph action as Parametric EQ. Internal DSP
   // enable remains the durable, ACK-owned parameter for existing presets.
   enableSlot(){if(target()?.power==='SLOT'&&current)window.__ARMIX_NATIVE_M2__?.consumeAction?.(current.track,'slotPower',{dataset:{slot:String(current.slot)}});},
   onState(state){
   latest=state;
   if(!state.accepted)presets?.reset();
   if(state.accepted&&!state.pending){if(state.error)presets?.rejectRecall(transport.lastRequestId(),state.error);presets?.onAccepted(state.accepted,state.appliedId);presets?.refresh();presets?.restoreSelection();}
   presets?.paint();
  }});
  const select=root.querySelector('[data-preset]');
  const bar=root.querySelector('.an-presets');
  presets=window.ArSonMixBuiltinFxPresetBar?.create({
    host:root,select,label:'Anti Noiz',catalogId:ID,context,
    stable:()=>!!latest?.accepted&&!latest.pending,readAccepted:()=>latest?.accepted||{},
    validate:valid,factoryValue,sameSettings,notice:m=>{const el=root.querySelector('[data-status]');if(el)el.textContent=m;},
    apply:s=>view?.model.submit('settings',normalize(s))?transport.lastRequestId():null
  });
  if(presets){
   bar.hidden=false;
   select.onchange=()=>{
    if(presets.selectUser(select.value))return;
    const factory=factoryPresets.find(p=>p.id===select.value);
    if(factory&&latest?.accepted)presets.selectFactory({...factory.settings,enabled:latest.accepted.enabled});
    else presets.restoreSelection();
   };
   if(latest?.accepted)presets.onAccepted(latest.accepted);
  }
  timer=setInterval(()=>{view?.refreshHostPower();view?.model.refresh();presets?.tick();},180);
 }
 window.ArSonMixNativeAntiNoiz=Object.freeze({id:ID,open,close,
  insert:target=>Boolean(window.__ARMIX_NATIVE_M2__?.insertPlugin?.(ID,target))});
 // Current global/rack/slot readback is authoritative even between DSP
 // telemetry polls. One singleton listener, not one per editor opening.
 backend.addEventListener('arMixConsoleSnapshot',()=>queueMicrotask(()=>view?.refreshHostPower()));
 window.addEventListener('keydown',e=>{if(!window.ArSonMixFxWorkspace&&e.key==='Escape'&&!root.hidden&&!e.target.closest?.('.mx-fxp-form'))close();});
})();
