// Reference mixer state smoke; Node built-ins only. No DSP or visual verification.
// Run: node design-references/audition-mixer-study/smoke.test.cjs
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),assert=require('node:assert/strict');
const html=fs.readFileSync(path.join(__dirname,'index.html'),'utf8');
const script=html.match(/<script>\s*([\s\S]*?)\s*<\/script>/)?.[1];
assert.ok(script,'embedded mixer JavaScript exists');
const elements=new Map(),cases=[];
function node(name){
 if(elements.has(name))return elements.get(name);
 const classes=new Set(),handlers={};
 const x={value:name==='viewFilter'?'all':'',innerHTML:'',textContent:'',scrollLeft:0,dataset:{},handlers,
  classList:{contains:k=>classes.has(k),add:k=>classes.add(k),remove:k=>classes.delete(k),
   toggle(k,force){const on=force===undefined?!classes.has(k):!!force;on?classes.add(k):classes.delete(k);return on;}},
  style:{_vars:{},setProperty(k,v){this._vars[k]=String(v)},getPropertyValue(k){return this._vars[k]||''}},setAttribute(){},addEventListener(k,cb){handlers[k]=cb;},
  querySelector(){return null;},
  dispatch(k){assert.ok(handlers[k],name+' '+k+' handler missing');handlers[k]({target:x});}};
 elements.set(name,x);return x;
}
const document={getElementById:node,querySelector:k=>k==='.console-body'?node('body'):k==='.section-rail'?node('rail'):k==='.console-pane'?node('pane'):null,querySelectorAll:()=>[]};
const fakeWindow={handlers:{},addEventListener(k,fn){this.handlers[k]=fn;}};
vm.runInNewContext(script,{document,window:fakeWindow,requestAnimationFrame(){}},{timeout:3000});
function check(ok,title){assert.ok(ok,title);cases.push(title);}
const count=()=> (node('tracks').innerHTML.match(/<article class="strip/g)||[]).length;
function filter(value){node('viewFilter').value=value;node('viewFilter').dispatch('change');}
function field(id,name,value){node('body').handlers.change({target:{dataset:{field:name},value,closest:k=>k==='.strip'?{dataset:{id}}:null}});}
function strip(id){const html=node('tracks').innerHTML,p=html.indexOf('data-id="'+id+'"');assert.ok(p!==-1,'strip '+id+' exists');return html.slice(p,html.indexOf('</article>',p));}
check(count()===10,'Initial bank has 10 full-width strips (remaining Bus accessible by arrow)');
check(node('master').innerHTML.includes('Master'),'pinned Master');
node('copy').dispatch('click');node('addTrack').dispatch('click');node('paste').dispatch('click');
check(node('info').textContent.includes('Pasted compatible'),'copy/paste');
node('addFx').dispatch('click');check(node('trackCount').textContent.includes('1 FX return'),'FX Return creation');
check(node('tracks').innerHTML.includes('FX 1'),'New FX Return is revealed on its bank immediately');
filter('fx');check(count()===1,'FX Return filter');filter('all');
node('addBus').dispatch('click');check(node('trackCount').textContent.includes('2 bus'),'Bus creation');
check(node('tracks').innerHTML.includes('Bus B'),'New Bus is visible immediately, not selected offscreen');
node('bankSelect').value='0';node('bankSelect').dispatch('change');
field(15,'output','Bus A');check(node('info').textContent.includes('output → Bus A'),'bus route');
field(11,'output','Bus B');check(node('info').textContent.includes('Routing rejected'),'feedback detection');
field(1,'activeSend','S2');check(node('info').textContent.includes('Send 2'),'send slot switch');
field(1,'send','FX 1');filter('all');check(strip(1).includes('<option value="FX 1" selected>'),'send to FX Return');
field(1,'activeSend','S1');filter('all');check(strip(1).includes('<option value="Bus A" selected>'),'16 sends preserve independent slots');
node('saveScene').dispatch('click');check(node('sceneSelect').value==='0','scene save');
node('mode').dispatch('click');check(node('body').classList.contains('narrow'),'narrow console');
filter('audio');check(count()===10,'audio filter shows first full-width bank');
node('recallScene').dispatch('click');check(node('viewFilter').value==='all'&&!node('body').classList.contains('narrow'),'scene recall');
node('reset').dispatch('click');
node('body').handlers.mousedown({target:{closest:k=>k==='.strip'?{dataset:{id:2}}:null},ctrlKey:true,metaKey:false,shiftKey:false});
node('group').dispatch('click');
check(strip(1).includes('group-indicator')&&strip(2).includes('group-indicator'),'channel grouping');
node('body').handlers.input({target:{dataset:{field:'level'},value:'5',closest:k=>k==='.strip'?{dataset:{id:1},querySelector(){return {textContent:''};}}:null},type:'input',altKey:false});
filter('all');check(/class="fader"[^>]*value="5"/.test(strip(2)),'relative grouped faders');
node('ungroup').dispatch('click');check(!strip(1).includes('group-indicator'),'ungroup');
check(html.includes('clip-path:inset(calc(100% - var(--lit,0%))'),'stationary-color meters');
check(!html.includes('window.prompt')&&!html.includes('fxPost'),'no dead modal or pseudo-FX mode');

// Live UX regression suite: plugin browser, center-detent pan, banks, SoF and Safe.
const plug=node('pluginBrowser');
node('pluginsButton').dispatch('click');
check(plug.hidden===false,'Plugin Manager opens in mixer without page navigation');
check(node('pluginResults').innerHTML.includes('Parametric Equalizer'),'Reference effects catalog populated');
node('pluginSearch').value='deesser';node('pluginSearch').dispatch('input');
check(node('pluginResults').innerHTML.includes('DeEsser')&&!node('pluginResults').innerHTML.includes('Studio Reverb'),'Plugin live search filters names');
node('pluginSearch').value='';node('pluginSearch').dispatch('input');
function pluginAction(type,id){
 node('pluginResults').handlers.click({target:{closest:k=>k==='[data-plugin-action]'?{dataset:{pluginAction:type,pluginId:id}}:null}});
}
pluginAction('fav','demo-7');
node('pluginView').value='favorites';node('pluginView').dispatch('change');
check(node('pluginResults').innerHTML.includes('Chorus'),'Favorite effect filtering');
node('pluginView').value='all';node('pluginView').dispatch('change');
pluginAction('hide','demo-7');
check(!node('pluginResults').innerHTML.includes('Chorus'),'Hidden effect removed from normal list');
node('pluginView').value='hidden';node('pluginView').dispatch('change');
check(node('pluginResults').innerHTML.includes('Chorus'),'Hidden effect discoverable in Hidden view');
pluginAction('hide','demo-7');
node('pluginView').value='all';node('pluginView').dispatch('change');
pluginAction('choose','demo-2');
check(plug.hidden===false,'Modeless Plugin Manager stays open while inserting FX');
filter('all');
check(strip(1).includes('Studio Reverb'),'FX insert changes in channel strip');
node('pluginClose').dispatch('click');node('pluginsButton').dispatch('click');
node('pluginView').value='recent';node('pluginView').dispatch('change');
check(node('pluginResults').innerHTML.includes('Studio Reverb'),'Recent plugin filter');
node('pluginClose').dispatch('click');
check(plug.hidden===true,'Plugin manager closes from explicit button');
const panWrapper={value:'',nextElementSibling:{textContent:''},setAttribute(k,v){this.value=v;}};
function panInput(v){
 const target={dataset:{field:'pan'},value:String(v),closest:k=>k==='.strip'?{dataset:{id:1}}:k==='.pan-control'?panWrapper:null};
 node('body').handlers.change({target,type:'change',altKey:false});return target;
}
let pan=panInput(4);
check(pan.value==='0'&&panWrapper.value.includes('--pan-start:50%;--pan-end:50%'),'Center pan snaps to 0 with center-zero fill');
pan=panInput(-60);
check(panWrapper.value.includes('--pan-start:20%;--pan-end:50%'),'Left pan fills outwards from center');
pan=panInput(40);
check(panWrapper.value.includes('--pan-start:50%;--pan-end:70%'),'Right pan fills outwards from center');
filter('all');node('bankSelect').value='0';node('bankSelect').dispatch('change');
check(count()===10,'Bank 1 fits ten full-width channels');
node('bankSelect').value='1';node('bankSelect').dispatch('change');
check(count()===1,'Bank 2 displays the remaining Bus strip');
node('bankSelect').value='0';node('bankSelect').dispatch('change');
node('reset').dispatch('click');
node('sofToggle').dispatch('click');
check(node('body').classList.contains('sends-mode')&&strip(1).includes('S -6.0'),'Sends on Faders shows channel send, not main fader');
const readout={textContent:''},faderStrip={dataset:{id:1},querySelector:()=>readout};
node('body').handlers.change({target:{dataset:{field:'level'},value:'-12',closest:k=>k==='.strip'?faderStrip:null},type:'change',altKey:false});
filter('all');check(strip(1).includes('S -12.0'),'Sends on Faders changes targeted send');
node('sofToggle').dispatch('click');filter('all');
check(/class="fader"[^>]*value="0"/.test(strip(1)),'Sends on Faders preserves main fader gain');
node('saveScene').dispatch('click');
node('sceneSafe').dispatch('click');filter('all');
check(strip(1).includes('safe-marker'),'Snapshot Safe indicated on channel strip');
node('body').handlers.change({target:{dataset:{field:'level'},value:'8',closest:k=>k==='.strip'?faderStrip:null},type:'change',altKey:false});
node('recallScene').dispatch('click');filter('all');
check(/class="fader"[^>]*value="8"/.test(strip(1)),'Snapshot recall preserves Safe channel live fader');
node('clearCue').dispatch('click');
check(node('info').textContent.includes('Cue / PFL'),'Separate cue clear command');
check(!html.includes("['Off','Read','Write','Latch','Touch']")&&!html.includes('data-field="automation"'),'DAW automation modes removed');
const registry=fakeWindow.ArSonMixReference;
check(registry&&typeof registry.setPluginCatalog==='function','Native-facing metadata-only registry adapter exists');
check(registry.setPluginCatalog([{name:'Compressor X',kind:'effect',format:'VST3',status:'ready',category:'Dynamics',latencySamples:64}])===true,'Effect manifest import accepted');
node('pluginsButton').dispatch('click');
check(node('pluginResults').innerHTML.includes('Compressor X')&&node('pluginResults').innerHTML.includes('64 smp'),'Reported latency surfaced without claiming measured latency');
check(registry.setPluginCatalog([{name:'Instrument X',kind:'instrument',format:'VST3',status:'ready'}])===false,'Instrument manifest rejected from effect browser');
node('pluginClose').dispatch('click');


// Explicit channel format + patch safety; do not infer stereo from a single input pin.
node('reset').dispatch('click');
check(strip(1).includes('data-action="format"')&&strip(1).includes('>M</button>'),'Live mono input strip shows Mono format badge');
check(strip(1).includes('Simulated mono dBFS meter'),'Mono strip draws a mono meter');
const toggleFormat=()=>node('body').handlers.click({target:{closest:k=>k==='[data-action]'?{
 dataset:{action:'format'},closest:q=>q==='.strip'?{dataset:{id:1}}:null
}:null}});
toggleFormat();filter('all');
check(strip(1).includes('>ST</button>')&&strip(1).includes('Simulated stereo dBFS meter'),'Flipping Mono→Stereo changes format and meter');
check(strip(1).includes('<option value="None" selected>None</option>'),'Flipping channel format does not invent an input patch');
toggleFormat();filter('all');
check(strip(1).includes('>M</button>')&&strip(1).includes('Simulated mono dBFS meter'),'Stereo→Mono restores mono controls');
field(1,'input','Input 1 (Mono)');filter('all');
check(strip(1).includes('<option value="Input 1 (Mono)" selected>Input 1 (Mono)</option>'),
      'Single enabled mono pin selected explicitly');
toggleFormat();filter('all');
check(strip(1).includes('>ST</button>') &&
      strip(1).includes('<option value="Input 1 (Mono)" selected>Input 1 (Mono)</option>'),
      'Mono source remains mono when processing channel changes to Stereo');
toggleFormat();filter('all');
check(strip(1).includes('<option value="Input 1 (Mono)" selected>Input 1 (Mono)</option>'),
      'Physical source binding survives round-trip processing-format changes');
node('addFx').dispatch('click');
filter('fx');node('bankNext').dispatch('click');
check(node('bankSelect').value==='0','Filtered bank arrows follow visible channel group, not global track count');
filter('all');


// Layout regression: collapsed rows must hide their content without hiding header labels.
check(!html.includes('8 Inserts · drag to reorder'),'FX strip no longer displays a long wrapping header');
check(html.includes('white-space:nowrap;overflow:hidden;text-overflow:ellipsis'),
  'FX labels use single-line truncation');
check(html.includes('.console-body.collapsed-fx .fxslot-list,.console-body.narrow .fxslot-list{display:none}'),
  'Collapsing FX hides the scroll container, not only its children');
check(html.includes('grid-template-columns:10px minmax(0,1fr) 28px'),
  'Send controls have constrained slider/dB columns');
check(html.includes('.console-body.collapsed-fx{--fx-h:23px}') &&
  html.includes('.console-body.collapsed-send{--send-h:23px}'),
  'FX and SEND folds share compact-height tokens');
check(html.includes('flex:0 0 var(--fx-h)') && html.includes('flex:0 0 var(--send-h)'),
  'FX and SEND bands cannot shrink independently of section rail');
const rail=node('rail'),body=node('body');
for(const name of ['fx','send','route']){
 const flags=new Set(),attrs={};
 const button={dataset:{section:name},classList:{toggle(k,on){on?flags.add(k):flags.delete(k);return on;}},setAttribute(k,v){attrs[k]=v;}};
 const target={closest:k=>k==='[data-section]'?button:null};
 rail.handlers.click({target});
 check(body.classList.contains('collapsed-'+name) && attrs['aria-expanded']==='false',
  name+' rail toggle collapses section and updates aria-expanded');
 rail.handlers.click({target});
 check(!body.classList.contains('collapsed-'+name) && attrs['aria-expanded']==='true',
  name+' rail toggle expands section again');
}


// Comfort / Senior operational ergonomics regression (visual-reference only).
check(html.includes('data-density="comfort"'),'Comfort view is default on first load');
check(html.includes("root.style.setProperty('--channel-w'") && html.includes('grid-template-columns:repeat(var(--bank-columns,10)'),
      'Master and source strips share ONE calculated slot width, including partial banks');
check(html.includes('data-field="input"') && strip(1).includes('class="route-caption">IN</span>') &&
      strip(1).includes('class="route-caption">OUT</span>'),
      'Source and primary output have labeled separate rows');
check(strip(1).includes('class="fader-head"') && strip(1).includes('class="fader-readout"'),
      'Fader dB numeric display is above the tactile fader');
check(strip(1).includes('class="clip-indicator') && strip(1).includes('class="meter-box mono"'),
      'Mono strip includes explicit clip LED and one meter lane');
check(strip(1).includes('class="switch mute') && strip(1).includes('class="switch solo') && strip(1).includes('class="switch cue'),
      'Three equal-width brushed-metal buttons MUTE, SOLO, PFL, no I');
check(html.includes('.console-body .fader-box::before') && html.includes('.console-body .fader-box::after') &&
      html.includes('.fader-travel .cap') && html.includes('RAIL3'),
      'Calibrated old fader housing and cap travel restored with original RAIL3 material');
check(html.includes('.console-body .meter-lane:before') &&
      html.includes('repeating-linear-gradient(to top,transparent'),
      'Segmented LED meter overlays are present');
const density=node('densitySelect');
density.value='standard';density.dispatch('change');
check(node('body').dataset.density==='standard'&&!node('body').classList.contains('narrow'),
      'Standard view keeps full processing and wider controls');
density.value='compact';density.dispatch('change');
check(node('body').dataset.density==='compact'&&node('body').classList.contains('narrow'),
      'Compact view keeps narrow-console behavior');
density.value='comfort';density.dispatch('change');
check(node('body').dataset.density==='comfort'&&!node('body').classList.contains('narrow'),
      'Comfort restores full effects, send and EQ visibility');
check(html.includes('.console-body.collapsed-route .route-field{display:none}') &&
      html.includes('.console-body.collapsed-send .send-section .send-select,.console-body.collapsed-send .send-section .sendlevel{display:none}'),
      'No stale expanded CSS declarations in collapsed route and send');
check(html.includes('right:var(--master-w)!important') && html.includes('--channel-w'),
      'Plugin overlay and pinned Master use coherent single-width channel geometry');
check(html.includes('.plugin-browser{position:absolute!important') && html.includes('margin-right:0!important') && html.includes('Floating Plugin Manager must never consume a mixer bank'),
      'Floating Manager is independent from bank width and never triggers channel resize');
check(html.includes('function bankCapacity()') && html.includes('Math.floor((usable+2)/minimum)') && html.includes('const DENSITY_WIDTH=Object.freeze({comfort:150,standard:125,compact:105})'),
      'Bank slot count protects full VU/rail width with no bottom scrollbar');


// V3 integrated visual/layout behavior: no bottom-scrollbar and 10-channel bank.
node('reset').dispatch('click');
check(count()===10 && node('bankSelect').value==='0',
 'Mix opens with 10 fitted channel strips, fixed Master is separate');
node('bankNext').dispatch('click');
check(node('bankSelect').value==='1'&&count()===1&&node('tracks').innerHTML.includes('Bus A'),
 'Top right arrow opens remaining bank without horizontal dragging');
node('bankPrev').dispatch('click');
check(node('bankSelect').value==='0'&&count()===10,
 'Top left arrow restores first 10-channel bank');
check(html.includes('overflow:hidden!important;scrollbar-width:none!important') &&
 html.includes('.console-body #viewport::-webkit-scrollbar{display:none!important'),
 'Viewport has no horizontal scrollbar on browser engines');
check(html.includes('grid-template-columns:repeat(var(--bank-columns,10),minmax(0,1fr))') &&
 html.includes("tracksEl.style.setProperty('--bank-columns'"),
 'Visible strip widths are evenly computed from current bank size');
check(html.includes('--ari-silver:url(data:image/png;base64,iVBORw0KGgo') &&
 html.includes('--ari-rose:url(data:image/png;base64,iVBORw0KGgo') &&
 html.includes('--ari-pan:url(data:image/png;base64,iVBORw0KGgo'),
 'Original three ARI PNG artwork assets embedded, no external CDN');
check(html.includes('width:36px!important;height:77px!important') &&
 html.includes('width:18px!important;height:40px!important'),
 'Natural 36x77 fader and 18x40 pan dimensions preserved');
check(html.includes('grid-template-columns:51px minmax(36px,1fr) 26px!important') &&
 strip(1).indexOf('class="meter-box mono"')<strip(1).indexOf('class="fader-box"') &&
 strip(1).indexOf('class="fader-box"')<strip(1).indexOf('class="fader-scale"'),
 'Fader paint and DOM use left VU, central fader, right ruler');
check(html.includes('class="console-body collapsed-send"') && !strip(1).includes('Add effect</button><button class="fx-remove"'),
 'Mix default prioritizes expanded FX rack and only one Add FX affordance');
check(!strip(1).includes('eq-section') && !html.includes('<button class="rail-cell eq'),
 'EQ row and section control are removed: EQ is accessed as an FX plugin');
node('pluginsButton').dispatch('click');
check(node('pluginBrowser').hidden===false && !node('body').classList.contains('plugin-open'),
 'Plugin Manager floats independently without changing mixer channel widths');
node('pluginClose').dispatch('click');
check(node('pluginBrowser').hidden===true,
 'Closing floating Plugin Manager leaves fixed bank untouched');
node('body').clientWidth=1180;filter('all');
check(count()===6,'At 1180px Comfort reserves at least 150px per strip, showing six sources plus MAIN');
node('body').clientWidth=1680;filter('all');
check(count()===10,'At 1680px viewport returns to ten source channels plus equal Master');


check(!strip(1).includes('data-action="mon"')&&!strip(1).includes('data-field="sendPan"'),
      'I monitor button and stereo Send PAN removed without changing preserved session model');
check(!strip(1).includes('trim-section') && strip(1).includes('data-field="level"'),
      'TRIM row removed, fader remains an accessible gain input');
check(html.includes('--b180-shadow-surface') &&
      html.includes('inset 5px 5px 2px -1px rgba(0,0,0,.18)') &&
      html.includes('inset 3px 2px 2px -1px rgba(0,0,0,.50)'),
      'Exact seven-layer BTN-180 resting and physical-press shadows imported from old source');
check(html.includes('data:image/png;base64')&&html.includes('background:var(--ari-rose) center/36px 77px'),
      'Original 36×77 channel and RoseGold Master cap used within the exact detent travel');
check(html.includes('aria-label="Simulated')===false || strip(1).includes('dBFS meter'),
      'Demo meter explicitly reported in dBFS, not misleading gain scale');


// Geometry contract checks use the exact JavaScript width equation, without a fake browser pass.
node('body').clientWidth=1680;node('bankSelect').value='0';filter('all');
check(node('tracks').style.getPropertyValue('--bank-columns')==='10',
  'Primary bank uses exactly ten equal grid columns at 1680px');
check(node('pane').style.getPropertyValue('--master-w')===
      node('body').style.getPropertyValue('--channel-w'),
  'Master + dock offset equal one channel slot width to the CSS subpixel');
node('bankNext').dispatch('click');
check(count()===1 && node('tracks').style.getPropertyValue('--bank-columns')==='10',
  'CH11 single partial bank retains ten constant-size columns: no gigantic Bus fader');
node('pluginsButton').dispatch('click');
check(node('tracks').style.getPropertyValue('--bank-columns')==='10' &&
 node('pane').style.getPropertyValue('--master-w')===node('body').style.getPropertyValue('--channel-w'),
 'Floating Manager never reduces channel count or changes Master slot width');
node('pluginClose').dispatch('click');
check(node('tracks').style.getPropertyValue('--bank-columns')==='10',
  'Closing floating Manager preserves unchanged ten-slot bank');
node('bankPrev').dispatch('click');

// V5: ADD FX is NOT Plugin Manager. Picker is contextual and keyboard-searchable.
node('reset').dispatch('click');
check(registry.setPluginCatalog([{id:'v5-reverb',name:'Studio Reverb',kind:'effect',format:'VST3',status:'ready',category:'Reverb'}, {id:'v5-eq',name:'Parametric Equalizer',kind:'effect',format:'VST3',status:'ready',category:'EQ'}]),'Demo plugin registry for quick picker seeded after manifest-import tests');
const chooseFX={dataset:{action:'openPlugin',slot:'0'},
 closest:k=>k==='.strip'?{dataset:{id:1}}:null};
node('body').handlers.click({target:{closest:k=>k==='[data-action]'?chooseFX:null}});
check(node('fastPicker').hidden===false && node('pluginBrowser').hidden===true,
  'Clicking Add FX opens quick effect picker; Plugin Manager remains CLOSED');
check(node('fastResults').innerHTML.includes('Parametric Equalizer'),
  'Quick picker shows effect database, not plugin scan/path settings');
node('fastSearch').value='reverb';node('fastSearch').dispatch('input');
check(node('fastResults').innerHTML.includes('Studio Reverb') &&
 !node('fastResults').innerHTML.includes('Parametric Equalizer'),
  'Fast picker search narrows effect list while typing');
node('fastResults').handlers.click({target:{closest:k=>k==='[data-fast-id]'?{dataset:{fastId:'v5-reverb'}}:null}});
filter('all');check(node('fastPicker').hidden===true && strip(1).includes('Studio Reverb'),
  'One click inserts selected FX and immediately closes fast picker');
check(html.includes('style="--fader-fill:') && html.includes('--fader-y:') &&
 html.includes("box.style.setProperty('--fader-y'"),
 'Fader cap initial paint and live pointer movement use same gain position');
check(html.includes("addEventListener('pointerdown'") &&
 html.includes("addEventListener('pointermove'") &&
 html.includes('setFaderFromPointer'),
 'Real pointer drag movement and release are implemented for the full fader well');
check(!strip(1).includes('trim-section') && !html.includes('<div class="rail-cell trim"'),
 'TRIM UI completely removed');
check(html.includes("Math.abs(value)<=8") && html.includes('panStyle(value)'),
 'PAN uses an eight-point center-snap deadzone and center-origin fill');
check(html.includes('background-image:none!important') &&
 html.includes('width:26px!important;height:55px!important'),
 'V7 compact 26x55 silver/rose fader on deeply recessed graphite slot');
check(html.includes('VST3 SEARCH LOCATIONS') &&
 html.includes('window.ArSonMixNativePlugins?.scan'),
 'Floating Plugin Manager exposes VST3 paths and native-only scan capability gate');


// Actual pointer trajectory: top=max +10, mid≈−25, bottom=min −60, no fake visual-only cap.
node('reset').dispatch('click');
const faderBoxPaint={style:{setProperty(){}}};
const volumeReadout={textContent:''};
const stripEl={dataset:{id:1},querySelector:s=>s==='.fader-readout'?volumeReadout:s==='.fader-box'?faderBoxPaint:s==='.db-value'?{textContent:''}:null};
const rangeInput={dataset:{field:'level'},value:'0',min:'-60',max:'10',closest:s=>s==='.strip'?stripEl:null};
const travel={getBoundingClientRect(){return {top:100,height:300}}};
const pointerWell={querySelector:s=>s==='.fader'?rangeInput:s==='.fader-travel'?travel:null,
 setPointerCapture(){},releasePointerCapture(){}};
node('body').handlers.pointerdown({button:0,pointerId:11,clientY:100,altKey:false,
 target:{closest:s=>s==='.fader-box'?pointerWell:null},preventDefault(){}});
check(rangeInput.value==='10' && volumeReadout.textContent==='+10.0 dB',
 'Physical top of old fader well maps to +10 dB and updates numeric LCD');
fakeWindow.handlers.pointermove({pointerId:11,clientY:250,altKey:false});
check(rangeInput.value==='-25' && volumeReadout.textContent==='-25.0 dB',
 '50% drag maps to -25 dB without rebuilding the full strip');
fakeWindow.handlers.pointerup({pointerId:11,clientY:400,altKey:false});
check(rangeInput.value==='-60' && volumeReadout.textContent==='-∞ dB',
 'Physical bottom maps to -60 dB and releases pointer gesture');
filter('all');
check(strip(1).includes('value="-60"') && strip(1).includes('--fader-y:100%'),
 'Rendered cap follows the same bottom-of-well gain value after pointer drag');
node('reset').dispatch('click');


// V6 end-to-end channel identity and header physical-material contracts.
node('reset').dispatch('click');
check(html.includes('M14,17L9,12l5-5V17Z') && html.includes('M10,17V7l5,5l-5,5Z'),
 'Owner-provided solid SVG bank arrows are used unchanged');
check((html.match(/width="24" height="24" fill="#1f1f1f" aria-hidden="true"/g)||[]).length===2,
 'Both arrows reproduce the exact owner supplied 24x24 SVG fill and physical dimensions');
check(html.includes('class="tool bank-arrow"') &&
 html.includes('class="tool" id="sofToggle"') &&
 !html.includes('class="tool btn-180') &&
 html.includes('background:linear-gradient(#405460,#2c3f4c'),
 'V7 main header uses satin metal, with no mismatching plastic BTN-180');
check(html.includes('.console-body .switch-section .switch{') &&
 html.includes('background:linear-gradient(#52616a,#334751') &&
 !strip(1).includes('btn-180'),
 'V7 strip MUTE SOLO PFL use quiet satin-metal state buttons');
check(html.includes('.command-bar>*,.command-bar>label,.command-bar>.sep') &&
 html.includes('align-self:center!important') && html.includes('min-height:29px!important'),
 'Header labels, arrows, selectors and button caps align vertically to common centerline');
check(html.includes('--identity-h:29px') &&
 html.includes('.rail-cell.identity') &&
 html.includes('.strip-identity-top') &&
 html.includes('.footer-label .footer-icon'),
 'Channel top/bottom identity banners and rail use matching height and color tokens');
check(html.includes('const CHANNEL_ICONS=') &&
 html.includes('const CHANNEL_PALETTE=') &&
 html.includes("['♩','♫','♬','🎙','🎸','🎹','🥁','🎧','▥','◉','◎','⊞']"),
 'Original legacy GUI 12 channel icons and original color palette are reused');
check(strip(1).includes('class="strip-identity-top"') &&
 strip(1).includes('class="identity-symbol"') &&
 strip(1).includes('class="footer-icon"') &&
 strip(1).includes('--ch:#1699cd'),
 'Host channel top/bottom matches cyan identity and microphone icon');
check(node('master').innerHTML.includes('class="strip-identity-top"') &&
 node('master').innerHTML.includes('class="footer-icon"') &&
 node('master').innerHTML.includes('--ch:#a04b65'),
 'Pinned Master has same top/bottom identity hierarchy in rose metal');
node('bankSelect').value='1';node('bankSelect').dispatch('change');
const editBus={dataset:{action:'editIdentity'},closest:k=>k==='.strip'?{dataset:{id:11}}:null};
node('body').handlers.dblclick({target:{closest:k=>k==='.strip'?{dataset:{id:11}}:k==='.footer-label,.strip-identity-top'?{}:null,classList:{contains:()=>false}}});
check(node('channelIdentity').hidden===false &&
 node('identityName').value==='Bus A' &&
 node('identityIcons').innerHTML.includes('data-identity-icon="🎹"'),
 'Double-clicking identity opens editable legacy name/icon/color palette');
node('identityColors').handlers.click({target:{closest:k=>k==='[data-identity-color]'?{dataset:{identityColor:'#c32974'}}:null}});
node('identityIcons').handlers.click({target:{closest:k=>k==='[data-identity-icon]'?{dataset:{identityIcon:'🎹'}}:null}});
node('identityName').value='Vocal Bus';
node('identitySave').dispatch('click');
check(node('channelIdentity').hidden===true &&
 node('tracks').innerHTML.includes('Vocal Bus') &&
 node('tracks').innerHTML.includes('--ch:#c32974') &&
 node('tracks').innerHTML.includes('🎹'),
 'Applying identity updates BOTH nameplates, icon and original legacy color');
node('bankSelect').value='0';node('bankSelect').dispatch('change');
check(strip(1).includes('<option value="Vocal Bus" selected>'),
 'Renaming Bus A preserves live routing in existing Host send');
const editMaster={dataset:{action:'editIdentity'},closest:k=>k==='.strip'?{dataset:{id:12}}:null};
node('body').handlers.dblclick({target:{closest:k=>k==='.strip'?{dataset:{id:12}}:k==='.footer-label,.strip-identity-top'?{}:null,classList:{contains:()=>false}}});
node('identityName').value='Main Stereo';
node('identitySave').dispatch('click');
check(node('master').innerHTML.includes('Main Stereo') &&
 strip(1).includes('<option value="Main Stereo" selected>'),
 'Renaming Master updates all channel OUT choices and live routing references');
node('reset').dispatch('click');


// V7 professionally proportioned mixer + independent floating manager contract.
check(html.includes('--fx-h:clamp(111px,13.5vh,137px)') &&
 html.includes('.console-body .strip .fader-section') &&
 html.includes('background:linear-gradient(90deg,#253640,#30434e'),
 'FX rack shortened 30%-40%; remaining space goes to lighter tactile fader body');
check(html.includes('.console-body .fader-box::before') &&
 html.includes('background:linear-gradient(90deg,#050b10 0%,#0d1a22') &&
 html.includes('width:min(36px,100%)'),
 'Fader cutout black graphite recess contrasts against brighter metal chassis');
check(html.includes('width:15px!important;height:32px!important') &&
 html.includes('margin-top:-12px!important') &&
 html.includes('margin-top:3px!important'),
 'PAN/send horizontal thumbs smaller, preserving asset ratio and sitting lower');
check(html.includes('inset 0 0 0 1px var(--ch)') &&
 html.includes('.console-body .strip.selected'),
 'Selected channel receives 1px identity-colored border instead of universal cyan');
check(html.includes('id="pluginDragHandle"') &&
 html.includes('id="pluginMinimize"') &&
 html.includes('resize:both!important') &&
 html.includes("pluginDragHandle').addEventListener('pointermove'"),
 'Plugin Manager is modeless, draggable, resizable, minimizable');
check(html.includes("if(a==='rename'||a==='editIdentity')return;") &&
 html.includes("e.target.closest('.footer-label,.strip-identity-top')") &&
 html.includes("if(e.key!=='F2'&&e.key!=='Enter')"),
 'Single click only selects; double-click or keyboard F2 edits channel identity');

check(html.includes('.console-body .section-rail .rail-cell.switch{') &&
 html.includes('flex:0 0 var(--switch-h)!important') &&
 html.includes('max-height:var(--switch-h)!important'),
 'V7 Chromium-caught M/S/C rail row cannot stretch from generic .switch flex:1');
// V8 responsive geometry tests simulate actual element widths, not Chromium pixel QA.
node('reset').dispatch('click');
node('body').clientWidth=1366;node('rail').clientWidth=32;
node('densitySelect').value='comfort';node('densitySelect').dispatch('change');
check(count()===7,'Comfort 1366px reserves at least 150px per source and pinned MAIN');
check(Math.abs(parseFloat(node('body').style.getPropertyValue('--channel-w'))-166.75)<0.02,
 'MAIN and source channel use one rail-adjusted width budget');
node('densitySelect').value='standard';node('densitySelect').dispatch('change');
check(count()===9,'Standard 1366px shows nine readable 125px+ strips');
node('densitySelect').value='compact';node('densitySelect').dispatch('change');
check(count()===10,'Compact 1366px shows ten 105px+ strips');
node('densitySelect').value='comfort';node('densitySelect').dispatch('change');
node('addTrack').dispatch('click');
check(node('tracks').innerHTML.includes('Track 11') &&
 node('bankSelect').value==='1','Add Audio reveals its new channel even when bank must change');
check(html.includes('.console-body.collapsed-route .strip .route-section .route-field{display:none!important}') &&
 html.includes('.console-body.collapsed-send .strip .send-section .sendlevel{display:none!important}') &&
 html.includes('.console-body.collapsed-fx .strip .fxslot-list{display:none!important}'),
 'Collapse hides route/FX/Send contents despite high-specificity expanded CSS');
check(html.includes('.console-body .section-rail .rail-cell.route{height:var(--route-h)!important') &&
 html.includes('.console-body .section-rail .rail-cell.fx{height:var(--fx-h)!important') &&
 html.includes('.console-body .section-rail .rail-cell.send{height:var(--send-h)!important'),
 'Route/FX/Send section rail and strips use identical physical height tokens');
console.log('PASS '+cases.length+' reference mixer DOM checks');
cases.forEach(x=>console.log('  ✓ '+x));
