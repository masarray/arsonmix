// SPDX-License-Identifier: AGPL-3.0-only
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
const app=readFileSync('web/app.js','utf8');
const bridge=readFileSync('web/bridge.js','utf8');
const pluginUi=readFileSync('web/plugin-ui.js','utf8');
const css=readFileSync('web/styles-src/42-mixer-premium.css','utf8');
const native=readFileSync('src/Main.cpp','utf8');
function extractFunction(name){
 const start=app.indexOf('function '+name+'(');
 assert.ok(start>=0,'Missing '+name);
 const begin=app.indexOf('{',start);
 let depth=0;
 for(let i=begin;i<app.length;i++){
  if(app[i]==='{')depth++;else if(app[i]==='}'&&!--depth)return app.slice(start,i+1);
 }
 throw Error('Unterminated '+name);
}
test('DAW reorder preserves real chain identity, metadata, and bypass',()=>{
 const channels=[{inserts:['Gate','EQ','Reverb','Limiter']}];
 const meta={'0:0':{name:'gate'},'0:1':{name:'eq'},'0:2':{name:'reverb'},'0:3':{name:'limiter'}};
 const bypassed={'0:0':false,'0:1':true,'0:2':false,'0:3':true};
 let saved=0,sends=[];
 const state={channels,fxParams:meta,bypassed,currentChannel:0,pluginEditorIndex:0,
  persistSession:()=>{saved++;},send:(...v)=>sends.push(v),
  setChain:(ch,items)=>{ch.inserts=[...items];},
 };
 vm.createContext(state);
 vm.runInContext('function fxKey(ch,slot){return ch+":"+slot;}\nfunction normalChain(ch){return ch.inserts.filter(Boolean);}\n'.replaceAll('\\n','\n')+
 extractFunction('moveFxMeta')+'\n'+extractFunction('movePluginTo'),state);
 assert.equal(state.movePluginTo(0,0,3),true);
 assert.deepEqual([...channels[0].inserts],['EQ','Reverb','Limiter','Gate']);
 assert.deepEqual([...Array(4)].map((_,i)=>meta['0:'+i].name),['eq','reverb','limiter','gate']);
 assert.deepEqual([...Array(4)].map((_,i)=>bypassed['0:'+i]),[true,false,true,false]);
 assert.equal(saved,1);assert.equal(sends.length,1);
 assert.equal(state.movePluginTo(0,3,0),true);
 assert.deepEqual([...channels[0].inserts],['Gate','EQ','Reverb','Limiter']);
 assert.equal(state.movePluginTo(0,0,0),false);
 assert.equal(sends.length,2,'no redundant graph change when dropped in place');
});
test('native has six consistent empty inserts and one DSP reorder per drop',()=>{
 assert.match(bridge,/Array.from\(\{length:6\}/);
  assert.match(bridge,/Array\.from\(\{length:6\},\(_,slot\)=>fxSlotMarkup\(lane,slot,true\)\)/);
 assert.match(bridge,/fx-drag-ghost/);
 assert.match(bridge,/send\("fxMove",\{slot:from,value:to-from\}\)/);
 assert.doesNotMatch(bridge,/FX FULL/);
 assert.match(css,/width:min\(46px,100%\)/);
 assert.match(css,/background:#26343c/);
 assert.match(css,/top:0;bottom:0;left:50%/);
 assert.match(css,/#nativeFxSelector .native-dialog::before/);
 assert.match(css,/#app #mixer .native-insert-row.has-orbit/);
 assert.match(bridge,/fx-context-delete/);
 assert.match(bridge,/pickerAnchor/);
 assert.match(bridge,/data\.activeSlot/);
 // Unpatched channels must open a real input patch, not an inert FX picker.
 assert.match(bridge,/window\.__arMixOpenChannelPatch\?\.\(id,"input"\)/);
 assert.doesNotMatch(bridge,/openPicker\(el,id,Math.max\(0,slot\)\)/);
 assert.match(native,/moreThanOneInstanceAllowed\(\) override \{ return false; \}/);
});


test('premium fold and per-slot popover avoid rectangle border and global backdrop',()=>{
 const b=readFileSync('web/bridge.js','utf8');
 const trough=css.slice(css.indexOf('/* P0 FADER BED RECOVERY'),css.indexOf('/* DAW insertion markers'));
 assert.match(trough,/background:linear-gradient\(90deg,#15232c/);
 assert.doesNotMatch(trough,/border:1px solid #fff|filter:blur\(/);
 assert.match(trough,/width:min\(46px,100%\)/);
 assert.match(trough,/border:0;border-radius:6px/);
 assert.doesNotMatch(trough,/filter:blur\(/);
 assert.match(b,/#nativeFxSelector\{position:fixed;inset:0;display:block;padding:0/);
 assert.match(b,/background:transparent!important;backdrop-filter:none!important/);
 assert.match(b,/pickerAnchor\?\.classList\.add\("fx-picker-target"\)/);
 assert.match(pluginUi,/state\.routedPickerMode\?channel<0:\(state\.chain\.length>=8\|\|channel!==0\)/);
 assert.match(b,/if\(pickerRouteMode\)/);
 assert.match(b,/data-routed-fx/);
 assert.match(b,/const pluginUi=window\.ArSonPluginUI/);
 assert.ok(b.includes('data-context-fx="delete"'));
 assert.match(b,/native-node\.has-orbit/);
});
