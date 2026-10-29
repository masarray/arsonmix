// SPDX-License-Identifier: AGPL-3.0-only
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';

const read=p=>readFileSync(p,'utf8');
const ui=read('web/plugin-ui.js'),bridge=read('web/bridge.js');
const native=read('src/Main.cpp'),index=read('web/index.html');
const cmake=read('CMakeLists.txt'),css=read('web/plugin-ui.css');

function loadRenderer(){
  const listeners={};
  const dialog=makeDialog();
  const doc={
    addEventListener(type,fn){listeners[type]=fn},
    getElementById(){return dialog},
  };
  const win={};
  vm.runInNewContext(ui,{window:win,document:doc}, {timeout:1000});
  return {component:win.ArSonPluginUI,dialog,listeners};
}
function makeDialog(){
  const nodes=new Map();
  const node=()=>({
    innerHTML:'',textContent:'',hidden:false,className:'',
    setAttribute(name,value){this[name]=value},
    querySelector(){return {textContent:''}},
  });
  return {
    nodes,
    querySelector(selector){if(!nodes.has(selector))nodes.set(selector,node());return nodes.get(selector)},
    querySelectorAll(){return []},
  };
}
const entries=count=>Array.from({length:count},(_,i)=>({
  id:'plugin-'+i,name:i===248?'Special <script>':'Effect '+String(i).padStart(3,'0'),
  vendor:'Vendor '+i%4,category:'Effect',
  path:'C:\\Program Files\\VST3\\Effect'+i+'.vst3',
}));

test('compact manager exposes all catalog variants with bounded rows, search and pagination',()=>{
  const {component,dialog,listeners}=loadRenderer();
  const state={catalog:entries(250),scanning:false,message:'Ready'};
  component.renderManager(dialog,state,()=>'<div>progress</div>');
  let rows=dialog.querySelector('#arPluginManagerRows').innerHTML;
  assert.equal((rows.match(/class="ar-plugin-row"/g)||[]).length,120);
  assert.match(rows,/INDEXED/);
  assert.doesNotMatch(rows,/Status: OK/);
  assert.equal(dialog.querySelector('#arManagerMore').hidden,false);
  component.init(()=>state);
  listeners.click({target:{closest:selector=>selector==='#arManagerMore'?{}:null}});
  rows=dialog.querySelector('#arPluginManagerRows').innerHTML;
  assert.equal((rows.match(/class="ar-plugin-row"/g)||[]).length,240);
  listeners.input({target:{id:'arManagerSearch',value:'special'}});
  rows=dialog.querySelector('#arPluginManagerRows').innerHTML;
  assert.equal((rows.match(/class="ar-plugin-row"/g)||[]).length,1);
  assert.match(rows,/Special &lt;script&gt;/);
  assert.equal(dialog.querySelector('#arManagerMore').hidden,true);
});

test('FX picker updates ONLY rows during typing, preserves search node and fixes ADD column',()=>{
  const {component,dialog}=loadRenderer();
  const state={catalog:entries(250),chain:[],scanning:false,filter:'',
    selectedCategory:'All',adding:false,addError:'',pendingPlugin:''};
  component.renderPicker(dialog,state,'CH 1 · SLOT 1 · LIVE VST3',['All','EQ'],()=> 'EQ');
  const shell=dialog.querySelector('.native-dialog-body').innerHTML;
  let rows=dialog.querySelector('#arPickerRows').innerHTML;
  assert.equal((rows.match(/class="ar-picker-row"/g)||[]).length,80);
  assert.match(rows,/ar-picker-details/);
  assert.match(rows,/ar-picker-add/);
  state.filter='Effect 019';
  component.updatePickerRows(dialog,state,()=> 'EQ',0);
  assert.equal(dialog.querySelector('.native-dialog-body').innerHTML,shell);
  assert.equal((dialog.querySelector('#arPickerRows').innerHTML.match(/class="ar-picker-row"/g)||[]).length,1);
  state.adding=true;
  component.updatePickerRows(dialog,state,()=> 'EQ',0);
  assert.match(dialog.querySelector('#arPickerRows').innerHTML,/disabled/);
});

test('Windows Explorer pickers are separate, SafePointer guarded and have explicit VST3 validation',()=>{
  const pick=native.slice(native.indexOf('void chooseImport ('),native.indexOf('// The selected location receives a NEW'));
  assert.match(pick,/useNativeDialogBox|selectBundle \? "\*" : "\*\.vst3"/);
  assert.match(pick,/canSelectFiles/);
  assert.match(pick,/canSelectDirectories/);
  assert.doesNotMatch(pick,/canSelectFiles\s*\|\s*juce::FileBrowserComponent::canSelectDirectories/);
  assert.match(pick,/SafePointer<ConsoleWindow>/);
  assert.match(pick,/lastPluginFolder_/);
  for(const action of ['importVst3','importVst3Bundle','importVst3InPlace','importVst3BundleInPlace'])
    assert.ok(native.includes('action == "'+action+'"'));
});

test('asset registration and no blur/focus thrash in native plugin UI',()=>{
  for(const f of ['plugin-ui.js','plugin-ui.css']){
    assert.ok(index.includes(f));
    assert.ok(cmake.includes('web/'+f));
    assert.ok(native.includes('"/'+f+'"'));
  }
  assert.ok(bridge.includes('pluginUi.updatePickerRows('));
  assert.ok(bridge.includes('renderPickerResults(); // Preserve search input, caret, focus'));
  assert.ok(bridge.includes('iconSvg("close")'));
  assert.match(bridge,/close:"M6\.4,19L5,17\.6/);
  assert.doesNotMatch(css,/backdrop-filter\s*:\s*blur\(/);
  assert.doesNotMatch(css,/:hover/);
});
