// SPDX-License-Identifier: AGPL-3.0-only
// Exact source regression of the one native FX preset + picker workflow.
// Structural checks intentionally never claim physical ASIO sound evidence.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const read=file=>readFileSync(new URL('../'+file,import.meta.url),'utf8');
const host=read('src/Main.cpp');
const preset=read('site/preview-v2/assets/mixer-builtin-fx-preset-bar.js');
const harmony=read('site/preview-v2/assets/mixer-native-harmony.js');
const throwFx=read('site/preview-v2/assets/mixer-native-vocal-throw.js');
const bridge=read('web/approved-native-bridge.js');
const closeCss=read('site/preview-v2/assets/mixer-contextual-buttons.css');
const rackCss=read('site/preview-v2/assets/mixer-fx-rack.css');
const controller=read('site/preview-v2/assets/mixer-contextual-buttons.js');

test('operator Add Audio and rename preselection share original GUI/native owners',()=>{
  const html=read('site/preview-v2/index.html');
  assert.equal((html.match(/id="addTrack"/g)||[]).length,1);
  assert.match(html,/<details class="more-tools"[\s\S]*?id="addTrack"/);
  assert.match(controller,/add\.closest\('#moreTools'\)/);
  assert.match(controller,/bankNext\.insertAdjacentElement\('afterend',add\)/);
  assert.match(controller,/window\.innerWidth<=900/);
  assert.match(controller,/originalMenu\.insertBefore\(add,originalNext\)/);
  assert.match(controller,/window\.addEventListener\('resize',place/);
  assert.doesNotMatch(controller,/add\.cloneNode\(/);
  assert.match(controller,/body\?\.addEventListener\('dblclick'/);
  assert.match(controller,/input\.focus\(\{preventScroll:true\}\)/);
  assert.match(controller,/input\.select\(\)/);
  assert.match(html,/if\(e\.key==='Enter'\)\{e\.preventDefault\(\);applyChannelIdentity\(\);\}/);
  const composer=read('scripts/compose_operator_ux.py');
  assert.match(composer,/name\.focus\(\{preventScroll:true\}\);/);
  assert.match(composer,/name\.select\(\);/);
});

test('built-in FX preset store works with verified offline logical workspace',()=>{
  assert.match(host,/logicalPreset=fxRequest\.valid&&fxRequest\.unbound/);
  assert.match(host,/logicalFxTarget\(args,logicalChannel,logicalSlot\)/);
  assert.match(host,/logicalChannel==channel&&logicalSlot==static_cast/);
  assert.match(host,/!logicalMixer_\.readOnly\(\)/);
  assert.match(host,/auto offline=logicalPreset\?openLogicalBuiltinFx/);
  assert.match(host,/auto lease=physicalPreset\?engine_\.leaseNativeVst3Editor/);
  assert.match(host,/auto\* processor=logicalPreset\?offline\.get\(\)/);
  assert.match(host,/!logicalPreset&&\s*!realtime->stateSnapshotQuiescent\(\)/);
  assert.match(host,/BuiltInFxAcceptedSnapshot\s*::acceptedSettings/);
  assert.match(host,/result->setProperty\("workspaceToken",args\.getProperty/);
  assert.match(preset,/logical\?c\.deviceEpoch==="0":c\.deviceEpoch!=="0"/);
  assert.match(preset,/c\.workspaceToken&&reply\.workspaceToken!==c\.workspaceToken/);
  assert.match(harmony,/presetContextReady\(pc\)/);
  assert.match(harmony,/presetContextReady\(cPreset\)/);
  assert.match(harmony,/c\.workspaceToken&&reply\.workspaceToken!==c\.workspaceToken/);
});
test('FX picker closes and opens an actual editor only after accepted native or logical readback',()=>{
  assert.match(bridge,/function openVerifiedInsertedFx\(channelId,slot,catalogId,instanceId\)/);
  assert.match(bridge,/plan\.lease\.instanceId!==instanceId/);
  assert.match(bridge,/plan\.lease\.catalogId!==catalogId/);
  assert.match(bridge,/fxRackVM\.editorNavigationLeaseMatches\(current,lease,/);
  assert.match(bridge,/fxRackVM\.m4EditorPlan\(current,/);
  assert.match(bridge,/justInsertedLogicalFx/);
  assert.match(bridge,/scope\?\.closeNativePicker\?\.\(\);\s*openVerifiedInsertedFx\(p\.channelId,p\.slot,p\.catalogId,inserted\.id\)/);
  // Native editor requires verified accepted lease, not catalog key or ACK.
  const fxVM=read('web/approved-fx-rack-view-model.js');
  assert.match(bridge,/fxRackVM\.nativeInsertSettlement\(pendingFxInsert,next,confirmed,/);
  assert.match(bridge,/if\(fxInsert\.kind==="CONFIRMED"\)/);
  assert.match(bridge,/openVerifiedInsertedFx\(fxInsert\.channelId,fxInsert\.slot,/);
  assert.match(bridge,/fxInsert\.catalogId,fxInsert\.instanceId\)/);
  assert.match(fxVM,/confirmed\.requestId!==pending\.requestId/);
  assert.match(fxVM,/fx\.catalogId!==pending\.id/);
  assert.match(bridge,/pendingFxInsert=null;\s*scope\?\.closeNativePicker\?\.\(\)/);
  // G-MV30: the compatibility M4 insert must use the SAME accepted PCM
  // principle as ChannelId; ACK-attached native state is never renderer proof.
  assert.match(fxVM,/function m4InsertAckResult\(pending,ack\)/);
  assert.match(fxVM,/ack\.channel===pending\.channel&&/);
  assert.match(fxVM,/ack\.slot===pending\.slot/);
  assert.match(fxVM,/function m4InsertReadbackSettlement\(pending,snapshot,/);
  assert.match(bridge,/fxRackVM\.m4InsertReadbackSettlement\(p,snapshot,/);
  assert.match(bridge,/openRoutedFxEditor\(decision\.channel,decision\.slot,decision\.instanceId,/);
  const m4AckHandler=bridge.split('backend.addEventListener("arMixRoutingFxInsertAck",ack=>{')[1]
    ?.split('backend.addEventListener("arMixCatalog",catalog=>{')[0];
  assert.ok(m4AckHandler);
  assert.doesNotMatch(m4AckHandler,/ack\?\.state|paintM4FxInstances\(state\)|openRoutedFxEditor\(/);
  assert.match(bridge,/consumeAction\?\.\(track,"openPlugin"/);
});
test('Vocal Throw wheel does not flicker performance button and presets have dark dropdown',()=>{
  assert.doesNotMatch(throwFx,/vtThrow"\)\.disabled=value!=="on"\|\|!!pending/);
  assert.match(throwFx,/vtThrow"\)\.disabled=value!=="on"\|\|pending\?\.command==="nativeThrowTrigger"/);
  assert.match(throwFx,/if\(remaining<=0\)flushRotary\(\)/);
  assert.match(throwFx,/if\(pending\)return;[\s\S]*?wheelFlush=setTimeout\(flushRotary,remaining\)/);
  assert.match(throwFx,/if\(!rotaryDirty\)draft=\{\.\.\.accepted\}/);
  assert.match(rackCss,/\.mx-vthrow \.vt-preset select\{[\s\S]*?color-scheme:dark/);
  assert.match(rackCss,/\.mx-vthrow \.vt-preset :is\(option,optgroup\)\{background-color:#272b34;color:#e8eaf1/);
});
test('modeless utility windows are centered/dismissible without dismissing FX editors',()=>{
  for(const id of ['pluginBrowser','arsonPlayer','wireWorkspace','wireConfigWindow'])
    assert.match(controller,new RegExp("'"+id+"'"));
  assert.match(controller,/function centerFirst\(panel\)/);
  assert.match(controller,/function dismissOutside\(target\)/);
  assert.match(controller,/close\.click\(\)/);
  assert.doesNotMatch(controller,/\.mx-comp-slider,\.mx-harm-slider input\[type="range"\]/);
  const gesture=read("site/preview-v2/assets/mixer-builtin-fx-gesture.js");
  assert.match(gesture,/input\.dispatchEvent\(new Event\("input"/);
  assert.match(gesture,/e\.stopPropagation\(\)/);
  assert.match(closeCss,/--armix-floating-close-icon:url\("data:image\/svg\+xml/);
  assert.match(closeCss,/M6\.4,19L5,17\.6L10\.6,12L5,6\.4/);
  assert.match(closeCss,/margin-inline-start:auto!important/);
  assert.doesNotMatch(closeCss,/content:"×"!important/);
});

test('shared FX wheel helper clamps, preserves fine steps, ignores zoom and disabled controls',()=>{
 const code=read("site/preview-v2/assets/mixer-builtin-fx-gesture.js");
 const win={};new Function("window",code)(win);
 const handlers={};let edits=[];
 const input={min:"0",max:"10",step:".5",value:"5",disabled:false,isConnected:true,
  closest:()=>null,addEventListener:(name,fn)=>{handlers[name]=fn;}};
 win.ArMixFxGesture.bindRange(input,{onChange:v=>edits.push(v)});
 const fire=(deltaY,extra={})=>{const e={deltaY,deltaX:0,preventDefault(){this.defaultPrevented=true;},stopPropagation(){this.stopped=true;},...extra};handlers.wheel(e);return e;};
 assert.equal(fire(-120).stopped,true);assert.equal(edits.at(-1),6.5);
 fire(120,{shiftKey:true});assert.equal(edits.at(-1),6);
 fire(0,{deltaX:-120,shiftKey:true});assert.equal(edits.at(-1),6.5);
 fire(0,{deltaX:120,shiftKey:true});assert.equal(edits.at(-1),6);
 const count=edits.length;
 assert.equal(fire(-120,{ctrlKey:true}).defaultPrevented,undefined);
 assert.equal(fire(-120,{deltaX:200}).defaultPrevented,undefined);
 input.disabled=true;fire(-120);assert.equal(edits.length,count);
 input.disabled=false;input.value="9.5";fire(-120);assert.equal(edits.at(-1),10);
 fire(-120);assert.equal(edits.at(-1),10);
});

test('Harmonice wheel keeps V6 detents on precision ranges and accumulates held drafts',()=>{
 const code=read("site/preview-v2/assets/mixer-builtin-fx-gesture.js");
 const harmony=read("site/preview-v2/assets/mixer-native-harmony.js");
 const win={};new Function("window",code)(win);
 const normal=harmony.match(/normalSteps:key==="gainDb"\?(\d+):(\d+)/);
 const fine=harmony.match(/fineSteps:key==="gainDb"\?(\d+):(\d+)/);
 assert.ok(normal&&fine,"Harmonice must declare its wheel detents");
 for(const [index,min,max,start,coarse,small] of [[2,0,100,45,3,1],[1,-24,12,0,1.5,.5]]){
  let draft=start,handler,edits=0;
  const input={min:String(min),max:String(max),step:".1",value:String(start),
   disabled:false,isConnected:true,closest:()=>null,
   addEventListener:(name,fn)=>{if(name==="wheel")handler=fn;}};
  win.ArMixFxGesture.bindRange(input,{normalSteps:Number(normal[index]),
   fineSteps:Number(fine[index]),get:()=>draft,onChange:v=>{draft=v;edits++;}});
  const fire=(deltaY,shiftKey=false)=>handler({deltaY,deltaX:0,shiftKey,
   preventDefault(){this.defaultPrevented=true;},stopPropagation(){}});
  for(let i=1;i<=4;i++){input.value=String(start);fire(-120);
   assert.equal(draft,Math.min(max,start+i*coarse));}
  fire(120,true);assert.equal(draft,start+4*coarse-small);
  fire(-120,true);assert.equal(draft,start+4*coarse);
  assert.equal(edits,6);
 }
});
