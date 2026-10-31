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

test('favorites sort to top, remain searchable and serialize in the portable catalog',()=>{
  const {component,dialog,listeners}=loadRenderer();
  const state={catalog:entries(4),scanning:false,message:'Ready',filter:'',
    chain:[],selectedCategory:'All',adding:false,addError:'',pendingPlugin:'',routedPickerMode:false};
  state.catalog[3].favorite=true;
  component.renderPicker(dialog,state,'CH 1 · SLOT 1 · LIVE VST3',['All'],()=> 'All');
  const rows=dialog.querySelector('#arPickerRows').innerHTML;
  assert.ok(rows.indexOf('Effect 003') < rows.indexOf('Effect 000'));
  assert.match(rows,/aria-pressed="true"/);
  const sent=[];
  component.init(()=>state,(id,value)=>sent.push({id,value}));
  let prevented=false;
  listeners.click({preventDefault(){prevented=true},target:{
    closest(sel){return sel==='[data-ar-favorite]'?{dataset:{arFavorite:'plugin-3'}}:null}
  }});
  assert.ok(prevented);
  assert.deepEqual(sent,[{id:'plugin-3',value:false}]);
  component.renderManager(dialog,state,()=>'<p>done</p>');
  listeners.click({target:{closest:sel=>sel==='#arManagerFavorites'?{}:null}});
  assert.equal((dialog.querySelector('#arPluginManagerRows').innerHTML.match(/class="ar-plugin-row"/g)||[]).length,1);
  const catalog=read('src/PluginCatalog.h');
  assert.match(catalog,/setProperty \("favorite", favorite\)/);
  assert.match(catalog,/e\.favorite = static_cast<bool>/);
  assert.match(catalog,/iter->favorite = favorite/);
  assert.match(catalog,/bool setFavorite \(/);
  assert.match(native,/action == "setPluginFavorite"/);
  assert.match(native,/catalog_\.setFavorite \(id, favorite\)/);
});

test('real routed FX rack remains separate from Routing and respects native apply/ACK',()=>{
  const monitor=read('web/monitor.js');
  const engine=read('src/LiveDryRoutingLab.h');
  assert.match(bridge,/M2 single console presenter/);
  assert.match(bridge,/native-rack-/);
  assert.match(bridge,/data-routed-fx=/);
  assert.match(bridge,/data-routed-apply="1"/);
  assert.match(bridge,/if\(pickerRouteMode\)/);
  assert.match(monitor,/window\.__arMixStageRackFx=/);
  assert.match(monitor,/window\.__arMixApplyRackFx=/);
  assert.match(monitor,/patchPreflight\(\)/);
  assert.match(monitor,/document\.getElementById\("arRouteApply"\)\.click\(\)/);
  assert.match(monitor,/rackFxDraft=null; \/\/ Native ACK became authoritative/);
  assert.match(engine,/loadedFx>0 && std::any_of\(selections\[0\]\.begin\(\)/);
  assert.match(engine,/maxFxPerLane=FxGraph::maxInsertsPerStrip/);
  assert.match(engine,/reuseAt\(lane,slot,\*prior,old,req.id\)/);
  assert.match(monitor,/window\.__arMixFxLaneView=channel/);
  assert.match(monitor,/window\.__arMixStageRackFx=\(channel,id,slot=0,operation="set"\)/);
  assert.match(monitor,/channels:draftRoutes\.map\(r=>\(\{\.\.\.typedSource\(r\),fxIds:laneIds\(r\)\}\)\)/);
  assert.match(bridge,/data-routed-move/);
  assert.match(bridge,/data-routed-slot/);
  assert.ok(!bridge.includes('＋ ROUTE / FX'));
});

test('WebView2 avoids hidden-page reload and white underlay; startup uses opaque splash',()=>{
  assert.match(native,/withKeepPageLoadedWhenBrowserIsHidden\(\)/);
  assert.match(native,/withBackgroundColour \(juce::Colour \(0xff101921\)\)/);
  assert.match(native,/setVisible \(false\);\s*\/\/ Begin WebView navigation/);
  assert.match(native,/setVisible \(true\);\s*toBack\(\)/);
  assert.match(read('src/ArSonMixStartupSplash.h'),/setOpaque \(true\)/);
  assert.doesNotMatch(native,/goToURL \(.*\);\s*\/\/ restore/);
});

test('P2 user field regressions: fixed catalog header, uniform material and original CH1 ADD after setup',()=>{
  const monitor=read('web/monitor.js');
  const splash=read('src/ArSonMixStartupSplash.h');
  const standalone=read('AR-MIX-Live-FX-Host-Standalone-v0.3.html');
  const generatedCss=read('web/styles.css');
  assert.match(ui,/<div class="ar-manager-table-scroll" role="rowgroup"/);
  assert.ok(ui.indexOf('class="ar-plugin-row ar-plugin-header"') <
            ui.indexOf('<div class="ar-manager-table-scroll" role="rowgroup"'));
  assert.match(css,/#nativePluginManager .ar-manager-table-scroll::-webkit-scrollbar-thumb/);
  assert.match(css,/#nativeFxSelector .ar-picker-categories\{/);
  assert.match(css,/grid-template-columns:repeat\(7,minmax\(0,1fr\)\)/);
  assert.match(css,/#nativePluginManager .ar-manager-action-group .native-btn\{/);
  assert.match(css,/#nativeFxSelector .native-btn:active:not\(:disabled\)/);
  assert.match(css,/#nativePluginManager .native-dialog-header .ar-dialog-close:active/);
  assert.match(css,/--b180-color:#ff626c/);
  assert.match(css,/font-family:"ArSonMix Inter"/);
  assert.match(generatedCss,/font-size:11px!important;min-width:0/);
  assert.ok(standalone.includes('<style>'+generatedCss+'</style>'),
    'canonical standalone CSS must track the shared BTN-180 text size');
  assert.match(bridge,/function fxLaneSnapshot\(channel,master=false\)/);
  assert.match(bridge,/function fxSlotMarkup\(lane,slot,compact=false\)/);
  assert.match(bridge,/send\("addCatalog",\{id,requestId\}\)/);
  assert.match(bridge,/Audio device is unavailable\. Open SETUP/);
  assert.match(monitor,/document\.fonts\.ready\.then\(guiPaintReady,guiPaintReady\)/);
  assert.match(native,/g\.fillAll \(juce::Colour \(0xff101921\)\);\s*if \(loaded\) return/);
  assert.match(native,/splash_->setBounds \(window_->getBounds\(\)\)/);
  assert.match(splash,/g\.fillAll \(juce::Colour \(0xff0b1823\)\)/);
});

test('G5 UI safety: compact Studio One-like browser and native insert truth',()=>{
  assert.match(ui,/KEEP IN STUDIO/);
  assert.match(ui,/LINK INSTALLED/);
  assert.match(ui,/Show favorite plugins only/);
  assert.match(ui,/c==="Dynamics"\?"COMP"/);
  assert.match(ui,/data-native-category="\$\{escapeHtml\(c\)\}"/);
  assert.match(css,/grid-template-columns:36px repeat\(6,minmax\(0,1fr\)\)/);
  assert.match(css,/\.ar-manager-action-group \.ar-manager-label/);
  assert.match(css,/\.rack \.native-insert-row \.insert-name/);
  assert.match(css,/#nativeFxSelector #arPickerFavorites svg/);
  assert.match(bridge,/MASTER FX · UNAVAILABLE/);
  assert.doesNotMatch(bridge,/data-native-unrouted="-1"/);
  assert.match(bridge,/Array\.from\(\{length:8\}/);
  assert.match(bridge,/data-fx-lane/);
  assert.match(bridge,/fxSlotMarkup\(lane,slot,true\)/);
  assert.match(bridge,/Array\.from\(\{length:6\},\(_,slot\)=>fxSlotMarkup\(lane,slot,true\)\)/);
  assert.match(bridge,/data-routed-fx/);
  assert.match(bridge,/data-native-fx="editor"/);
});


test('FXF1 factory tabs use source metadata only, never invent unshipped effects',()=>{
  const {component,dialog,listeners}=loadRenderer();
  const state={catalog:[
    {...entries(1)[0],id:'factory-verified',name:'ArSonEQ',source:'factory',factoryProductId:'arson-eq'},
    {...entries(1)[0],id:'user-import',name:'My Compressor',source:'user'},
  ],chain:[],scanning:false,message:'Ready',filter:'',selectedCategory:'All',
  adding:false,addError:'',pendingPlugin:'',routedPickerMode:false};
  component.renderPicker(dialog,state,'CH 1 · SLOT 1 · LIVE VST3',['All'],()=> 'EQ');
  component.init(()=>state,()=>{});
  assert.equal((dialog.querySelector('#arPickerRows').innerHTML.match(/class="ar-picker-row"/g)||[]).length,2);
  const clickScope=(scope,target)=>listeners.click({target:{
    closest(sel){return sel==='[data-ar-source-scope]'?
      {dataset:{arSourceScope:scope,arScopeTarget:target}}:null;}
  }});
  clickScope('FACTORY','picker');
  let rows=dialog.querySelector('#arPickerRows').innerHTML;
  assert.match(rows,/ArSonEQ/);
  assert.doesNotMatch(rows,/My Compressor/);
  assert.match(rows,/FACTORY/);
  clickScope('USER','picker');
  rows=dialog.querySelector('#arPickerRows').innerHTML;
  assert.doesNotMatch(rows,/ArSonEQ/);
  assert.match(rows,/My Compressor/);
  clickScope('ALL','picker');
  assert.equal((dialog.querySelector('#arPickerRows').innerHTML.match(/class="ar-picker-row"/g)||[]).length,2);
  component.renderManager(dialog,state,()=>'<span>Ready</span>');
  clickScope('FACTORY','manager');
  rows=dialog.querySelector('#arPluginManagerRows').innerHTML;
  assert.match(rows,/ArSonEQ/);
  assert.doesNotMatch(rows,/My Compressor/);
});

test('FXF1 manifest parser is fail-closed and factory entries do not persist in user vault',()=>{
  const factory=read('src/FactoryFxRegistry.h');
  const catalog=read('src/PluginCatalog.h');
  const schema=JSON.parse(read('docs/factoryfx-release-manifest.schema.json'));
  assert.equal(schema.properties.plugins.items.properties.hostUniqueId.type,'integer');
  for(const product of ['arson-eq','arson-comp','arson-verb','arson-harmony',
                        'arson-color','arsonkupik-enhancer'])
    assert.ok(schema.properties.plugins.items.properties.productId.enum.includes(product));
  assert.match(factory,/getHexDigitValue \(c\) < 0/);
  assert.match(factory,/SHA256 \(binary\)/);
  assert.match(factory,/production-six/);
  assert.match(factory,/licenseReview/);
  assert.match(factory,/contained \(appRoot, bundle\)/);
  assert.match(catalog,/scanFactoryIfPresent/);
  assert.match(catalog,/factoryOnly_/);
  assert.match(catalog,/factoryFavorites_/);
  assert.match(catalog,/if \(entry.factory\) continue/);
  assert.match(native,/catalog_\.scanFactoryIfPresent\(\)/);
  assert.match(cmake,/FactoryFxVerifiedManifestAndNegativePathGate/);
});

test('M2 one FX presenter and native BUS staging have shared safety gates',()=>{
  const monitor=read('web/monitor.js');
  for(const term of ['function fxLaneSnapshot','function fxSlotMarkup','data-bus-create','__arMixStageBusFromSelected'])assert.ok(bridge.includes(term));
  for(const term of ['row.mode="BUS"','kind==="group"','draftSends.push({from:source,to:target,tap:"POST"','const issue=patchPreflight()','Object.assign(row,previousRow)','openRoutingLab()','No unused native strip for BUS'])assert.ok(monitor.includes(term));
  assert.doesNotMatch(bridge,/if\(lab\?\.active && \(i!==0/);
});

test('M2B existing BUS supports group, parallel FX, rollback and feedback safety',()=>{
  const m=read('web/monitor.js');
  for(const part of ['SEND TO EXISTING BUS','data-bus-target','data-bus-assign="group"',
    'data-bus-assign="fx"','__arMixStageSendToBus'])
    assert.ok(bridge.includes(part)||m.includes(part),part);
  for(const part of ['source===target','d.acceptedRoute.channels[target]?.mode!=="BUS"',
    'draftRoutes[source].main=originalMain','const issue=patchPreflight()',
    'mixerDraftActive=true','Native 32-send budget reached'])
    assert.ok(m.includes(part),part);
});
