// SPDX-License-Identifier: AGPL-3.0-only
// Fast, browser-independent regression of the approved floating workspaces.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';

const read=path=>readFileSync(new URL('../'+path,import.meta.url),'utf8');
const code=read('site/preview-v2/assets/mixer-contextual-buttons.js');
const css=read('site/preview-v2/assets/mixer-contextual-buttons.css');
const start=code.indexOf('/* ArSonMiX shared floating-window movement.');
assert.ok(start>=0,'one modeless pointer controller required');
const controller=code.slice(start);

test('All approved modeless tool windows share one drag source',()=>{
  for(const id of ['#fastPicker','#wireConfigWindow','#wireWorkspace','#routeChoice',
    '.armix-audio-dialog','#arIOOverlay .arIOWindow','#arSendEditor',
    '.arSourceChooser','#arRoutingLabPanel','#nativeFxSelector .native-dialog',
    '#nativePluginManager .native-dialog','#nativeMySounds .native-dialog',
    '#nativeOrbit .native-dialog'])
    assert.ok(controller.includes("'"+id+"'"),'missing floating contract for '+id);
  assert.match(controller,/event\.button!==0\|\|event\.isPrimary===false/);
  assert.match(controller,/event\.target\.closest\?\.\(blocked\)/);
  assert.match(controller,/handle\.setPointerCapture/);
  assert.match(controller,/window\.addEventListener\('pointercancel'/);
  assert.match(controller,/window\.addEventListener\('resize'/);
  assert.doesNotMatch(controller,/send\(|emitEvent\(|routingGraph|AudioContext|setInterval/);
});

test('Pointer drag clamps viewport, keeps click controls and releases capture',()=>{
  const listeners=new Map(),windowEvents=new Map(),data=new Map();
  let width=700,height=500,rect={left:30,top:40,width:300,height:180};
  let captured=false,released=false,prevented=false;
  const handle={
    contains:target=>target.kind==='header',
    setPointerCapture:()=>{captured=true;},
    hasPointerCapture:()=>captured,
    releasePointerCapture:()=>{released=true;captured=false;}
  };
  const style={setProperty:(k,v)=>data.set(k,v)};
  const panel={
    hidden:false,style,offsetWidth:300,offsetHeight:180,
    querySelector:()=>handle,
    closest:()=>null,
    getClientRects:()=>[{}],
    getBoundingClientRect:()=>({...rect}),
    classList:{add:c=>data.set('class',c)}
  };
  const target={
    kind:'header',
    closest:q=>q.startsWith('button,')?null:panel
  };
  const document={
    documentElement:{get clientWidth(){return width;},get clientHeight(){return height;}},
    addEventListener:(name,fn)=>listeners.set(name,fn),
    querySelectorAll:()=>[panel]
  };
  const window={innerWidth:width,innerHeight:height,
    addEventListener:(name,fn)=>windowEvents.set(name,fn)};
  vm.runInNewContext(controller,{document,window,console});
  const down=listeners.get('pointerdown');
  down({button:0,isPrimary:true,pointerId:7,clientX:60,clientY:65,
    target,preventDefault:()=>{prevented=true;}});
  assert.equal(prevented,true);
  assert.equal(captured,true);
  assert.equal(data.get('position'),'fixed');
  assert.equal(data.get('left'),'30px');
  assert.equal(data.get('top'),'40px');
  windowEvents.get('pointermove')({pointerId:7,clientX:1000,clientY:900});
  assert.equal(data.get('left'),'392px'); // 700 - 300 - 8
  assert.equal(data.get('top'),'312px'); // 500 - 180 - 8
  windowEvents.get('pointerup')({pointerId:7});
  assert.equal(released,true);
  windowEvents.get('pointermove')({pointerId:7,clientX:0,clientY:0});
  assert.equal(data.get('left'),'392px','no drag after release');
  const control={kind:'button',closest:q=>q.startsWith('button,')?{}:panel};
  down({button:0,isPrimary:true,pointerId:8,clientX:75,clientY:70,
    target:control,preventDefault:()=>{throw Error('button hijacked');}});
  assert.equal(captured,false);
  width=330;height=250;rect={left:392,top:312,width:300,height:180};
  windowEvents.get('resize')();
  assert.equal(data.get('left'),'22px');
  assert.equal(data.get('top'),'62px');
});

test('All floating closes and compact FX picker actions use one token family',()=>{
  for(const id of ['#identityClose','#pluginClose','#fastClose',
    '#wireConfigClose','#wireWsClose','#routeChoiceClose','#playerClose',
    '[data-audio-close]','[data-io-close]','[data-send-close]',
    '[data-source-close]','.armix-floating-close','.armix-builtin-fx-close'])
    assert.ok(css.includes(id),'close surface missing: '+id);
  for(const action of ['#fastManage','#pluginImport','#pluginScan','#pluginAddPath'])
    assert.ok(css.includes(action),'flat picker action not normalized: '+action);
  assert.match(css,/--armix-floating-action-bg:/);
  assert.match(css,/--armix-floating-close-face:/);
  assert.match(css,/min-height:28px!important/);
  assert.match(css,/cursor:grab;touch-action:none;user-select:none/);
});
