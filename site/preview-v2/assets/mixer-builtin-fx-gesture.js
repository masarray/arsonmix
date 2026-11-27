/* Shared built-in FX CONTROL-only wheel gesture. No audio authority. */
(()=>{
"use strict";
// Up increases, down decreases. Never hijack zoom or horizontal scrolling.
// One quantized step for fine; normal uses a bounded number of steps.
// Native ACK/coalescing remains the responsibility of the existing editor.
function direction(e){
 if(e.defaultPrevented||e.ctrlKey||e.metaKey||!Number.isFinite(e.deltaY))return 0;
 const dx=Number.isFinite(e.deltaX)?e.deltaX:0;
 // Chromium/Windows can transpose vertical wheel to horizontal while
 // Shift is held. On a parameter hit target only, treat it as fine wheel.
 // Unmodified horizontal scrolling and pinch/zoom still pass through.
 const dy=e.deltaY;
 const delta=e.shiftKey&&dy===0?dx:Math.abs(dx)>Math.abs(dy)?0:dy;
 return delta===0?0:delta<0?1:-1;
}
function bindRange(input,{get=()=>Number(input.value),enabled=()=>true,
                         normalSteps=3,fineSteps=1,onChange=null,target=input}={}){
 target.setAttribute?.("data-fx-wheel","");
 target.addEventListener("wheel",e=>{
  const dir=direction(e);
  if(!dir||input.disabled||!enabled()||!target.isConnected||
     target.closest("[hidden]"))return;
  const lo=Number(input.min),hi=Number(input.max),step=Number(input.step),value=get();
  if(![lo,hi,step,value].every(Number.isFinite)||step<=0||hi<=lo)return;
  e.preventDefault();e.stopPropagation();
  const steps=e.shiftKey?fineSteps:normalSteps;
  const next=Math.max(lo,Math.min(hi,Number((lo+
     Math.round((value-lo)/step+dir*steps)*step).toFixed(8))));
  if(next===value)return;
  input.value=String(next);
  if(onChange)onChange(next);
  else{
   input.dispatchEvent(new Event("input",{bubbles:true}));
   input.dispatchEvent(new Event("change",{bubbles:true}));
  }
 },{passive:false});
}
window.ArMixFxGesture=Object.freeze({direction,bindRange});
})();
