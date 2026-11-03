// SPDX-License-Identifier: AGPL-3.0-only
// Product gate for the actual approved M3 WebView UI embedded in JUCE.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {runInNewContext} from 'node:vm';
const read=p=>readFileSync(p,'utf8');
const html=read('web/index.html'),adapter=read('web/console-native.js');
const cmake=read('CMakeLists.txt'),main=read('src/Main.cpp');
const source=html.match(/<script>([\s\S]*?)<\/script>/)?.[1];
test('user-approved M3 one-Console HTML is the SINGLE embedded Windows GUI',()=>{
 assert.ok(source,'missing approved Console script');
 assert.doesNotThrow(()=>new Function(source));
 assert.doesNotThrow(()=>new Function(adapter));
 for(const id of ['tracks','master','viewport','pluginBrowser','nativeAuthority','nativeTruth','nativeDevice',
  'wireLayer','wireNavigatorToggle','densitySelect','bankNext','addTrack','addBus','addFx'])
  assert.ok(html.includes('id="'+id+'"'),'M3 Console missing '+id);
 assert.ok(html.includes('src="console-native.js"'));
 assert.doesNotMatch(html,/data-view="(?:FX RACK|SIGNAL FLOW|MIXER)"/);
 assert.doesNotMatch(cmake,/web\/(?:app|bridge|monitor|mixer-layout|mixer-io|plugin-ui)\.(?:js|css)/);
 assert.doesNotMatch(main,/BinaryData::(?:app_js|bridge_js|monitor_js|styles_css)/);
 assert.match(main,/BinaryData::index_html/);
 assert.match(main,/BinaryData::consolenative_js/);
 assert.match(main,/withResourceProvider \(assetFor\)/);
});
test('ALL 24 approved GUI visual assets are embedded and served at correct paths',()=>{
 const assets=[...cmake.matchAll(/web\/assets\/([A-Za-z0-9_.-]+)/g)].map(m=>m[1]);
 assert.equal(assets.length,24,'wrong copied approved GUI asset count');
 const linked=[...html.matchAll(/(?:href|src)="(assets\/[^"]+)"/g)].map(m=>m[1]);
 assert.equal(linked.length,20,'missing approved CSS/JS links');
 for(const asset of assets){
  const path='web/assets/'+asset;
  assert.ok(cmake.includes(path),'CMake BinaryData omits '+path);
  assert.ok(main.includes('path == "/assets/'+asset+'"'),'JUCE ResourceProvider omits '+path);
 }
 for(const linkedAsset of linked)
  assert.ok(assets.includes(linkedAsset.slice('assets/'.length)),linkedAsset+' missing');
});
test('native M3 mode NEVER starts fixed-ten JS mock or plugin catalog',()=>{
 assert.match(source,/const NATIVE=!!window\.__JUCE__\?\.backend\?\.emitEvent/);
 assert.match(source,/if\(NATIVE\)\{tracks=\[\];/);
 assert.match(source,/let pluginCatalog=\(NATIVE\?\[\]:/);
 assert.match(source,/window\.ArSonMixNativeModel=Object\.freeze/);
 assert.match(source,/function projectNative\(s\)/);
 assert.match(source,/Array\.isArray\(s\.graph\.channels\)/);
 assert.match(source,/acceptedChannels/);
 assert.match(source,/channelPeaksL/);
 assert.match(source,/channelPeaksR/);
 assert.match(adapter,/arMixConsoleSnapshot/);
 assert.match(adapter,/arMixConsoleAck/);
 assert.match(adapter,/arMixConsoleIntent/);
 assert.doesNotMatch(adapter,/AudioContext|OscillatorNode|createOscillator|createMediaStream/);
});
function harness(ops){
 const listeners=new Map(),events=[],dom=new Map(),modelState={snapshot:()=>{
  return {device:{epoch:'12'},graph:{revision:'0',rosterRevision:'0'},
   productionCallbackLive:false,legacyM4:{controlPlane:'LEGACY_M4_ORDINAL',
   routingRevision:'6',supportedOps:ops}};
 }};
 const row={nativeId:'7',kind:'audio',level:-12,pan:0,mute:false,solo:false};
 const model={snapshot:modelState.snapshot,supported:()=>ops,row:()=>row,
  visual:(t,k,v)=>{if(k==='level')t.level=v;else if(k==='pan')t.pan=v},
  project:s=>{modelState.current=s}};
 const backend={
  emitEvent:(name,payload)=>events.push({name,payload}),
  addEventListener:(name,callback)=>listeners.set(name,callback)
 };
 const document={
  getElementById:id=>{if(!dom.has(id))dom.set(id,{disabled:false,textContent:'',id});return dom.get(id)},
  addEventListener:(name,callback,capture)=>listeners.set('DOM:'+name,callback)
 };
 runInNewContext(adapter,{window:{__JUCE__:{backend},ArSonMixNativeModel:model},document,
  Date,Map,Set,Number,Math,Object,String}, {timeout:1000});
 const action=(type,element)=>{
  const e={target:element,preventDefault(){this.prevented=true},
   stopImmediatePropagation(){this.stopped=true},stopPropagation(){}};
  listeners.get('DOM:'+type)(e);return e;
 };
 return {events,listeners,row,action,dom};
}
test('physical IN, realtime fader, and mute drive genuine versioned JUCE request/ACK',()=>{
 const a=harness(['SET_SOURCE','SET_FADER','SET_MUTE','SET_PRIMARY_OUT']);
 assert.deepEqual(a.events.slice(0,2).map(e=>e.name),['arMixGuiReady','arMixConsoleSubscribe']);
 const input={dataset:{field:'input'},value:'in:3',closest:()=>({})};
 const e=a.action('change',input);
 assert.ok(e.stopped,'mock handler must never mutate a native input');
 let request=a.events.at(-1);
 assert.equal(request.name,'arMixConsoleIntent');
 assert.equal(request.payload.operation,'SET_SOURCE');
 assert.equal(request.payload.targetChannelId,'7');
 assert.equal(request.payload.deviceEpoch,'12');
 assert.equal(request.payload.legacyRoutingRevision,'6');
 assert.deepEqual(Object.assign({},request.payload.payload),{portId:'in:3',sourceFormat:'MONO'});
 a.action('change',{dataset:{field:'level'},value:'-6',closest:()=>({})});
 request=a.events.at(-1);
 assert.equal(request.payload.operation,'SET_FADER');
 assert.equal(request.payload.payload.valueDb,-6);
 assert.equal(request.payload.baseGraphRevision,'0');
 const button={id:'',dataset:{action:'mute'},closest:selector=>selector==='button'?button:({})};
 a.action('click',button);
 request=a.events.at(-1);
 assert.equal(request.payload.operation,'SET_MUTE');
 assert.equal(request.payload.payload.enabled,true);
 const count=a.events.length;
 a.listeners.get('arMixConsoleAck')({requestId:'arsonmix-console:2',status:'REJECTED',errorCode:'STALE_GRAPH_REVISION'});
 assert.equal(a.events.length,count,'ACK may not invent accepted audio');
 assert.match(a.dom.get('info').textContent,/REJECTED/);
});
test('dynamic channel and fake WIRE capabilities stay fail closed until real callback',()=>{
 const a=harness(['SET_FADER']);
 const before=a.events.length;
 const b={id:'addTrack',dataset:{},closest:s=>s==='button'?b:null};
 const e=a.action('click',b);
 assert.ok(e.stopped);
 assert.equal(a.events.length,before);
 assert.match(a.dom.get('info').textContent,/not admitted/);
 const wire={dataset:{},closest:s=>s.includes('.wire-tray')?{}:null};
 a.action('pointerdown',wire);
 assert.match(a.dom.get('info').textContent,/native accepted graph/);
});
