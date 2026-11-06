// SPDX-License-Identifier: AGPL-3.0-only
// Run the shipped browser EQ against a deterministic DOM/native event source.
// This checks control ACK behavior, NOT a physical sound-card FAT.
import assert from 'node:assert/strict';
import vm from 'node:vm';
import fs from 'node:fs';
const source=fs.readFileSync('site/preview-v2/assets/mixer-builtin-eq.js','utf8');
class Node {
  constructor(tag) {
    this.tagName=tag;this.children=[];this.dataset={};this.attributes={};
    this.className='';this.style={setProperty(){}};this.textContent='';
    this.listeners=new Map();this.hidden=false;this.disabled=false;
    this.value='';this.isConnected=false;
    this.classList={toggle:(name,on)=>{
      const parts=new Set(this.className.split(/\s+/).filter(Boolean));
      if(on)parts.add(name);else parts.delete(name);
      this.className=[...parts].join(' ');
    }};
  }
  append(...nodes){for(const node of nodes)node.isConnected=true;this.children.push(...nodes);}
  prepend(...nodes){for(const node of nodes)node.isConnected=true;this.children.unshift(...nodes);}
  setAttribute(name,value){this.attributes[name]=String(value);}
  getAttribute(name){return this.attributes[name];}
  addEventListener(name,fn){
    const callbacks=this.listeners.get(name)||[];
    callbacks.push(fn);this.listeners.set(name,callbacks);
  }
  fire(name,props={}) {
    const evt={target:this,button:0,pointerId:1,clientX:0,clientY:0,
      preventDefault(){},stopPropagation(){},...props};
    for(const fn of this.listeners.get(name)||[])fn(evt);
  }
  querySelector(sel){
    const q=[...this.children];
    while(q.length){
      const n=q.shift();
      if(sel.startsWith('.')?n.className.split(/\s+/).includes(sel.slice(1)):
          n.tagName===sel)return n;
      q.unshift(...n.children);
    }
    return null;
  }
  closest(sel){return sel==='[data-slot]'&&this.dataset.slot!==undefined?this:null;}
  getBoundingClientRect(){return {left:0,top:0,width:520,height:240};}
  getContext(){
    const noop=()=>{};
    return Object.fromEntries(['setTransform','fillRect','beginPath','moveTo','lineTo',
      'stroke','fillText','closePath','fill','arc','setLineDash'].map(k=>[k,noop]));
  }
  setPointerCapture(){}
  focus(){}
}
const defaults=()=>[80,160,250,500,1500,4500,9500,18000].map((hz,i)=>({
  enabled:false,frequencyHz:hz,gainDb:0,q:i>=2&&i<=5?1:0.70710678,slope:12
}));
const snapshot=(epoch='9',revision='3',id='0',bands=defaults())=>({
  schemaVersion:1,channelId:'101',deviceEpoch:epoch,graphRevision:revision,
  sampleRate:44100,appliedRequestId:id,bands
});
function start(native) {
  const listeners=new Map(),sent=[];
  const backend={
    emitEvent(name,payload){sent.push({name,payload});},
    addEventListener(name,fn){
      const all=listeners.get(name)||[];
      all.push(fn);listeners.set(name,all);
    }
  };
  let epoch='9',revision='3';
  const window={devicePixelRatio:1,addEventListener(){}};
  if(native) {
    window.__JUCE__={backend};
    window.__ARMIX_BUILTIN_EQ_CHANNEL__={
      resolve:ui=>ui===42?'101':null,
      state:()=>({admitted:true,device:{epoch},graph:{revision,channels:[{id:'101'}]}})
    };
  }
  const document={readyState:'complete',body:new Node('body'),
    createElement:t=>new Node(t)};
  vm.runInNewContext(source,{window,document,Math,Number,String,BigInt,Date,
    requestAnimationFrame:fn=>{fn();return 0;},cancelAnimationFrame(){}},
    {filename:'mixer-builtin-eq.js',timeout:1500});
  const dialog=document.body.querySelector('section');
  assert.ok(dialog);
  const find=sel=>{
    const found=dialog.querySelector(sel);
    assert.ok(found,'Missing editor element '+sel);
    return found;
  };
  return {window,backend,sent,dialog,find,fire(name,data){
    for(const fn of listeners.get(name)||[])fn(data);
  },session(ep,rev){epoch=ep;revision=rev;}};
}
const lastAction=h=>h.sent.filter(x=>x.name==='arMixAction').at(-1)?.payload;
{
  const h=start(false);
  h.window.ArSonMixBuiltinEq.open({id:42,name:'Vocal',kind:'audio'});
  h.find('.mx-eq-switch').fire('click');
  assert.equal(h.sent.length,0,'Static Pages EQ must never issue a PCM command');
  assert.match(h.find('.mx-eq-status').textContent,/NO AUDIO/);
}
{
  const h=start(true),editor=h.window.ArSonMixBuiltinEq;
  editor.open({id:42,name:'Mic 1',kind:'audio'});
  assert.equal(lastAction(h).action,'builtinEqSnapshot');
  assert.equal(lastAction(h).channelId,'101');
  const power=h.find('.mx-eq-switch');
  assert.equal(power.disabled,true,'Wait for authoritative native EQ readback');
  h.fire('arMixBuiltinEqSnapshot',snapshot());
  assert.equal(power.disabled,false);
  h.sent.length=0;
  power.fire('click');
  const command=lastAction(h);
  assert.equal(command.action,'builtinEqSetBand');
  assert.equal(command.channelId,'101');
  assert.equal(command.deviceEpoch,'9');
  assert.equal(command.graphRevision,'3');
  assert.equal(command.slot,2);
  assert.equal(command.band.enabled,true);
  assert.match(command.requestId,/^[0-9]+$/);
  assert.equal(power.getAttribute('aria-pressed'),'false','QUEUED is not applied');
  h.fire('arMixBuiltinEqQueue',{requestId:command.requestId,channelId:'101',status:'QUEUED'});
  h.fire('arMixBuiltinEqAck',{requestId:command.requestId,channelId:'101',
     deviceEpoch:'9',graphRevision:'3',status:'APPLIED'});
  assert.equal(power.getAttribute('aria-pressed'),'false','ACK alone is not readback');
  h.fire('arMixBuiltinEqSnapshot',snapshot('9','3','0'));
  assert.equal(power.getAttribute('aria-pressed'),'false','Old readback must not win');
  const applied=defaults();applied[2].enabled=true;
  h.fire('arMixBuiltinEqSnapshot',snapshot('9','3',command.requestId,applied));
  assert.equal(power.getAttribute('aria-pressed'),'true','Only accepted PCM readback paints ON');
  h.fire('arMixBuiltinEqSnapshot',snapshot('9','2','123',defaults()));
  assert.equal(power.getAttribute('aria-pressed'),'true','Stale graph cannot mutate UI');
  h.fire('arMixConsoleSnapshot',{admitted:true,device:{epoch:'10'},
     graph:{revision:'3',channels:[{id:'101'}]}});
  assert.equal(power.disabled,true,'Hotplug must revoke old EQ control');
  h.session('10','4');
  editor.open({id:42,name:'Mic 1',kind:'audio'});
  h.fire('arMixBuiltinEqSnapshot',snapshot('10','4'));
  const canvas=h.find('canvas');
  const x=28+Math.log(250/20)/Math.log(1000)*(520-58);
  h.sent.length=0;
  canvas.fire('pointerdown',{pointerId:8,clientX:x,clientY:120});
  canvas.fire('pointermove',{pointerId:8,clientX:x+50,clientY:91});
  assert.equal(h.sent.length,0,'Pointer motion is a PREVIEW, not new DSP state');
  canvas.fire('pointerup',{pointerId:8,clientX:x+50,clientY:91});
  const drag=lastAction(h);
  assert.equal(h.sent.length,1,'Release issues exactly one intent');
  assert.equal(drag.band.enabled,true);
  assert.ok(drag.band.frequencyHz>250);
  assert.equal(power.getAttribute('aria-pressed'),'false','Drag UI stays accepted-only');
}
console.log('PASS: Built-in EQ runtime UI events, audio ACK, stale-graph fence, Pages isolation');