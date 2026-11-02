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
field(1,'send','FX 1');const noticeFx=node('info').textContent.includes('send → FX 1');filter('all');check(noticeFx && !strip(1).includes('class="send-select"'),'Legacy send state accepts FX Return but main strip has no dropdown');
field(1,'activeSend','S1');const noticeSlot=node('info').textContent.includes('Send 1');filter('all');check(noticeSlot && !strip(1).includes('class="send-slot"'),'16 legacy send slots preserved without visible picker');
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
check(strip(1).includes('wire-deck') && !strip(1).includes('send-select') &&
 !strip(1).includes('sendlevel') && !strip(1).includes('send-slot'),
 'V13 wire deck replaces Bus dropdown and horizontal send slider');
check(html.includes('.console-body.collapsed-fx{--fx-h:23px}') &&
  html.includes('.console-body.wire-routing-v13.collapsed-send{--send-h:80px!important}') &&
  html.includes('.console-body.wire-routing-v13:not(.collapsed-send){--send-h:160px!important}'),
  'FX can fold; wire patch bay uses compact 80px and expanded 160px');
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
check(strip(1).includes('class="button switch mute') &&
      strip(1).includes('class="button switch solo') &&
      strip(1).includes('buttons-wrapper codepen-buttons-wrapper') &&
      strip(1).includes('class="switch cue'),
      'CodePen MUTE/SOLO are mounted as a two-button group; PFL remains separate');
check(html.includes('https://codepen.io/praktikdan/pen/jbqBWm') &&
      html.includes('background-image:linear-gradient(#333,#222)!important') &&
      html.includes('background-image:linear-gradient(#202020,#151515)!important') &&
      html.includes('inset 0 1px 6px rgba(0,0,0,.5),0 10px 20px rgba(255,255,255,.06)') &&
      html.includes('rgba(2,165,238,.4)') && html.includes('rgba(2,165,238,.75)'),
      'Original Dan Andersson CodePen CSS values are retained for MUTE/SOLO');
check(strip(1).includes('data-action="mute"') && strip(1).includes('data-action="solo"') &&
      strip(1).includes('aria-pressed="false"') && strip(1).includes('<span class="fa">MUTE</span>') &&
      strip(1).includes('<span class="fa">SOLO</span>'),
      'MUTE/SOLO retain independent accessible native click actions');
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
      'Comfort restores FX rack and visible wire patch bay');
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
check(html.includes('class="console-body collapsed-send wire-routing-v13 wire-routing-v15"') && !strip(1).includes('Add effect</button><button class="fx-remove"'),
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
check(html.includes('const range=event.type') && html.includes('panDetent') && html.includes('panStyle(value)'),
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
check(strip(1).includes('<option value="Vocal Bus"'),
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


// V9: elastic rack vs invariant fader and glass LCD-meter material.
check(html.includes('V9: CONSOLE MECHANICAL LAYOUT') &&
 html.includes('--fader-fixed-h:clamp(198px,31vh,300px)') &&
 html.includes('.console-body:not(.collapsed-fx) .strip .fx-section{') &&
 html.includes('flex:1 1 var(--fx-h)!important') &&
 html.includes('.console-body:not(.collapsed-fx) .strip .fader-section{') &&
 html.includes('flex:0 0 var(--fader-fixed-h)!important'),
 'V9 FX rack absorbs Route/Send reclaimed height instead of remapping fader travel');
check(html.includes('.console-body:not(.collapsed-fx) .section-rail .rail-cell.fx{') &&
 html.includes('.console-body:not(.collapsed-fx) .section-rail .rail-cell.fader{'),
 'Rail and all channel strips share stable fader/elastic FX height contracts');
check(html.includes('.console-body .strip .meter-box .vu-bars::after{') &&
 html.includes('linear-gradient(103deg,#e4faff17') &&
 html.includes('.console-body .strip .meter-box .meter-lane::after{') &&
 html.includes('repeating-linear-gradient(to top,transparent 0px,transparent 5px') &&
 html.includes('.console-body .strip .meter-box .peak.clipping{'),
 'Glass LCD dual-lane meter has stationary color zones, 7px sections and distinct clip optics');
// Four independent inputs listen to CH1 using source identity, not four input-to-one summing.
node('reset').dispatch('click');
check(strip(2).includes('<option value="CH01 PRE-FX SPLIT">'),
 'CH2 IN selector exposes CH1 PRE-FX split source as a distinct channel feed');
[2,3,4,5].forEach(id=>field(id,'input','CH01 PRE-FX SPLIT'));
check(strip(1).includes('class="split-count"') &&
 strip(1).includes('⇢4'),'Source CH1 shows FOUR independently routed target channels');
[2,3,4,5].forEach(id=>check(strip(id).includes('<option value="CH01 PRE-FX SPLIT" selected>'),
 'CH'+id+' owns separate selected CH1 split SourceBinding'));
field(1,'input','CH02 PRE-FX SPLIT');
check(node('info').textContent.includes('SPLIT REJECTED') &&
 strip(1).includes('<option value="None" selected>'),
 'Circular channel split CH2→CH1→CH2 rejected and previous source preserved');
field(5,'input','None');
check(strip(1).includes('⇢3'), 'Removing one split receiver keeps remaining three receivers intact');
check(node('info').textContent.includes('demo binding only'), 'Demo split UI remains clearly non-native');
field(1,'output','SOURCE ONLY (No Main)');
const sourceOnlyNotice=node('info').textContent.includes('SOURCE ONLY (no direct Main)');
filter('all');
check(strip(1).includes('<option value="SOURCE ONLY (No Main)" selected>') && sourceOnlyNotice,
 'CH1 may be capture/source-only so four split processing paths do not force a fifth direct MAIN dry feed');
node('reset').dispatch('click');

// V11/V12: source-first wire switching in the persistent middle BUS/SEND row.
// These are deterministic state tests; actual screenshot geometry is gated separately.
node('body').clientWidth=1680;node('reset').dispatch('click');
check(html.includes('id="wireLayer" class="routing-wire-overlay"') &&
 html.includes('id="wireLegend"') &&
 html.includes('.console-body.wire-routing-v13.collapsed-send{--send-h:80px!important}'),
 'Wire layer and 53px middle routing row survive collapsed advanced Send');
check(strip(1).includes('wire-node-out is-source') &&
 strip(3).includes('data-action="wireRoute"') &&
 strip(3).includes('class="wire-socket wire-node-in"') &&
 strip(3).includes('wire-pot is-idle'),
 'Source CH1 is lit and every destination has FL-like up arrow');
function wireTo(id,shift=false){
 const el={dataset:{action:'wireRoute'},closest:k=>k==='.strip'?{dataset:{id}}:null};
 const target={closest:k=>k==='[data-action]'?el:k==='[data-action="wireRoute"]'?el:k==='.strip'?{dataset:{id}}:null};
 node('body').handlers.mousedown({target,ctrlKey:false,metaKey:false,shiftKey:false});
 node('body').handlers.click({target,shiftKey:shift});
}
[3,4,5,6].forEach(id=>wireTo(id));
check(strip(1).includes('⇢4') && strip(3).includes('wire-node-in is-linked') &&
 node('wireLegend').innerHTML.includes('wire-map-group is-out') && node('wireLegend').innerHTML.includes('CH03') && node('wireLegend').innerHTML.includes('CH04'),
 'CH1 click-source then CH3–CH6 socket clicks create four independent visible links');
[3,4,5,6].forEach(id=>check(strip(id).includes('<option value="None" selected>') &&
 strip(id).includes('class="wire-tray linked"') &&
 strip(id).includes('data-field="wireGain"'),
 'Wire to CH'+id+' is an independent FL-style send preserving receiver IN and providing per-send gain'));
wireTo(5);
check(strip(1).includes('⇢3') && strip(5).includes('class="wire-socket wire-node-in"'),
 'Click linked socket a second time disconnects precisely that destination');
field(7,'input','Input 1 (Mono)');
wireTo(7);
check(strip(7).includes('<option value="Input 1 (Mono)" selected>') &&
 strip(7).includes('wire-node-in is-linked'),
 'FL-style send does not replace occupied hardware input; receiver can sum existing input plus source send');
// NEW V12 cases: independent send gains, alternate PRE FX tap, snapshots and feedback.
node('wireMode').value='split';node('wireMode').dispatch('change');
wireTo(8);
check(strip(8).includes('wire-node-in is-linked') &&
 strip(8).includes('data-wire-id') &&
 node('info').textContent.includes('INDEPENDENT PRE FX'),
 'Independent PRE FX tap is explicit and per-edge (not confused with fader send)');
const pot=strip(8).match(/data-wire-id="(\d+)"/);
check(!!pot,'Connected PRE FX wire has a stable edge ID for gain control');
const gainTarget={dataset:{field:'wireGain',wireId:pot[1]},value:'-12',
 style:{setProperty(){}},parentElement:{style:{setProperty(){}}},closest:k=>k==='.strip'?{dataset:{id:8}}:null};
node('body').handlers.change({target:gainTarget,type:'change'});
const gainNotice=node('info').textContent.includes('-12 dB');
filter('all');
check(strip(8).includes('value="-12" data-field="wireGain"') && gainNotice,
 'Each wire has its own -60..+6 dB calibrated hardware-pot gain');
node('saveScene').dispatch('click');
wireTo(8);
node('recallScene').dispatch('click');
filter('all');
check(strip(8).includes('value="-12" data-field="wireGain"'),
 'Snapshot round-trip restores wire graph topology, gain, mode and stable identity');
node('wireMaster').dispatch('click');filter('all');
check(strip(1).includes('<option value="SOURCE ONLY (No Main)" selected>') &&
 node('wireMaster').textContent==='MAIN OFF',
 'Dedicated MAIN ON/OFF removes only direct source feed while retaining wire edges');
node('wireMaster').dispatch('click');filter('all');
check(strip(1).includes('<option value="Master" selected>') &&
 node('wireMaster').textContent==='MAIN ON',
 'MAIN send can be restored without altering channel wire topology');
// Graph guards: CH1→CH3 already connected; now CH3→CH1 must fail.
node('body').handlers.mousedown({target:{closest:k=>k==='.strip'?{dataset:{id:3}}:null},ctrlKey:false,metaKey:false,shiftKey:false});
wireTo(1);
check(node('info').textContent.includes('feedback loop') &&
 strip(1).includes('wire-socket'),
 'Graph cycle guard refuses reverse link CH3→CH1');
node('reset').dispatch('click');

node('body').clientWidth=1680;node('reset').dispatch('click');
wireTo(3);wireTo(6,true);
check(strip(1).includes('⇢4') &&
 [3,4,5,6].every(id=>strip(id).includes('wire-node-in is-linked')),
 'Two-click CH3 then SHIFT+CH6 creates an atomic four-target FL-style route');
check(node('info').textContent.includes('destinations linked with SHIFT'),
 'Operator receives clear one-message bulk routing acceptance');
node('wireMode').value='send';node('wireMode').dispatch('change');
const edge3=strip(3).match(/data-wire-id="(\d+)"/);
check(!!edge3,'Shift-created links each have per-edge IDs and a gain dial');
const gain3={dataset:{field:'wireGain',wireId:edge3[1]},value:'-18',
 style:{setProperty(){}},parentElement:{style:{setProperty(){}}},closest:q=>q==='.strip'?{dataset:{id:3}}:null};
node('body').handlers.change({target:gain3,type:'change'});
filter('all');
check(strip(3).includes('value="-18" data-field="wireGain"') &&
 strip(4).includes('value="0" data-field="wireGain"'),
 'Four-way send gain is independent per target without changing neighbors');
check(html.includes('function wireConnectRange(') &&
 html.includes('if(batch.some(t=>wireIsCycle(source.id,t.id)))') &&
 html.includes('SourceBinding')===false || html.includes('function wireConnectRange('),
 'Wire route bulk changes have explicit cycle preflight');
// V12 usability: direct manipulation + uncluttered hover affordance.
node('body').clientWidth=1680;node('reset').dispatch('click');
wireTo(3);
const edge=strip(3).match(/data-wire-id="(\d+)"/);
check(!!edge,'Wire gain control has unique stable identity for pointer/keyboard events');
const wheelGain={dataset:{field:'wireGain',wireId:edge[1]},value:'0',
 closest:q=>q==='.wire-gain'?wheelGain:q==='.strip'?{dataset:{id:3}}:null,
 style:{setProperty(){}},parentElement:{style:{setProperty(){}}}};
node('body').handlers.wheel({target:wheelGain,deltaY:120,preventDefault(){}});
filter('all');
check(strip(3).includes('value="-1" data-field="wireGain"'),
 'One wheel step on wire knob makes precise -1 dB adjustment');
node('body').handlers.dblclick({target:wheelGain});
filter('all');
check(strip(3).includes('value="0" data-field="wireGain"'),
 'Double-click wire knob restores unity 0 dB without changing route');
check(html.includes('wireHoverId=t.id;renderWireRouting()') &&
 html.includes("edge.preview?'#b5e8d3'") &&
 !html.includes('stroke-dasharray="5 5"') &&
 html.includes('edge.preview'),
 'Hover-only candidate wire is solid and translucent; never dashed');
node('reset').dispatch('click');

// V12 pinned Master socket follows FL Send-switch semantics without extra PCM edge.
node('body').clientWidth=1680;node('reset').dispatch('click');
const masterId=Number(node('master').innerHTML.match(/data-id="(\d+)"/)?.[1]);
check(Number.isInteger(masterId)&&node('master').innerHTML.includes('wire-node-in is-linked'),
 'Pinned MAIN route is a visible green clickable socket for selected source');
wireTo(masterId);
check(node('master').innerHTML.includes('MAIN OFF') &&
 node('wireMaster').textContent==='MAIN OFF',
 'Clicking pinned MASTER wire socket disconnects direct primary Main feed');
wireTo(masterId);
check(node('master').innerHTML.includes('MAIN ON') &&
 node('wireMaster').textContent==='MAIN ON',
 'Second click on pinned MASTER socket restores direct Main without creating send edges');
node('reset').dispatch('click');
// Existing legacy SEND S1 into Bus A must not gain a SECOND identical wire path.
node('body').clientWidth=1680;node('reset').dispatch('click');
node('bankSelect').value='1';node('bankSelect').dispatch('change');
wireTo(11);
check(node('info').textContent.includes('ALREADY CONNECTED') &&
 node('tracks').innerHTML.includes('wire-node-in is-linked') &&
 !node('tracks').innerHTML.includes('data-field="wireGain"'),
 'V32 existing legacy SEND to Bus A is visibly connected but never doubled with a new wire gain');
node('reset').dispatch('click');
node('reset').dispatch('click');

// V13: Clean FL Studio-inspired WIRE PATCH BAY replaces obsolete Send form.
// Full operator row offers direct arrow, multi-destination cable and per-wire knob.
node('body').clientWidth=1680;node('reset').dispatch('click');
check(strip(1).includes('class="section send-section wire-section"') &&
 strip(1).includes('class="wire-deck"') &&
 strip(1).includes('class="wire-deck-label"') &&
 strip(1).includes('class="wire-tray"'),
 'V13 shows continuous hardware wire-deck above destination sockets');
check(!/class="send-slot"|class="send-select"|class="sendlevel"|data-field="sendLevel"|data-action="pre"|data-action="sendOn"/.test(strip(1)),
 'No legacy destination picker, slot selector, Pre/Post button or redundant slider in operator strip');
check(html.includes('.console-body.wire-routing-v13.collapsed-send{--send-h:23px!important}') ||
 html.includes('.console-body.wire-routing-v13.collapsed-send{\n --send-h:23px!important'),
 'V14.1 source folds WIRE bay into a real 23px header');
check(html.includes('.console-body.wire-routing-v13:not(.collapsed-send){--send-h:160px!important}') &&
 html.includes('const sagLimit=wireSagClearance(anchor,well);'),
 'Expanded WIRE retains the existing height rules but bounds hanging cable depth to its own patch bay');
check(html.includes("document.querySelector('.console-body')?.classList.contains('collapsed-send')") &&
 html.includes('wireGesture?.active') &&
 html.includes('.console-body.wire-routing-v15.collapsed-send{--send-h:23px!important}'),
 'V16 hides collapsed cables and tray but keeps the pointer routing model');
 check(html.includes('/* V14: THREE-way collapse') &&
 html.includes('.console-body.collapsed-route.collapsed-fx.collapsed-send .strip .fader-section'),
 'Fader may grow only on exactly three upper sections folded');
wireTo(3);wireTo(6,true);
check(strip(1).includes('⇢4') &&
 [3,4,5,6].every(id=>strip(id).includes('wire-node-in is-linked') &&
 strip(id).includes('data-field="wireGain"') && !strip(id).includes('send-select')),
 'V13 four-way routing requires only destination arrows; level remains inline per edge');
node('reset').dispatch('click');

// V15 solid-to-hollow wire drag affordance and explicit Bus decision.
node('body').clientWidth=1680;node('reset').dispatch('click');
check(strip(1).includes('data-action="wireSource"') &&
 strip(1).includes('wire-node-out is-source') &&
 strip(3).includes('data-action="wireRoute"') &&
 strip(3).includes('wire-node-in') &&
 strip(3).includes('wire-pot is-idle'),
 'Every input strip shows a filled OUT and hollow IN routing jack');
check(html.includes('requestWireConnection(source,target,input)') &&
 html.includes('wire-drag-line') && html.includes('Math.hypot('),
 'Live cable follows pointer with drop-only graph intent (no fake PCM)');
check(html.includes('.route-choice-option.primary') &&
 html.includes('VIA BUS ONLY') && html.includes('PARALLEL SEND'),
 'Bus-only primary route and parallel send are separate explicit operator decisions');
node('reset').dispatch('click');

// V17 invariants: CONNECTED is a value change, not a layout reflow.
// Three stable columns (OUT / IN / gain), no ugly close X beside knob.
check(html.includes('grid-template-columns:24px 28px 22px!important') &&
  strip(1).includes('wire-pot is-idle') && !strip(1).includes('wire-disconnect') &&
  strip(3).includes('wire-pot is-idle') && strip(3).includes('wire-pin'),
  'V24 keeps OUT, larger IN and gain fixed across states without any X slot');
wireTo(3);
check(strip(3).includes('class="wire-tray linked"') &&
  strip(3).includes('data-field="wireGain"') && !strip(3).includes('wire-pot is-idle') &&
  strip(4).includes('wire-pot is-idle'),
  'V17 turns only destination knob active without shifting any other channel layout');
check(html.includes('function wirePathData(') && html.includes('function wireSnapTarget(') &&
  html.includes('WIRE_MAGNET.preview') && html.includes('True magnetic PREVIEW') &&
  html.includes('queueWireGestureFrame();') &&
  html.includes('const input=snap?.element||hit?.closest?.') &&
  !html.slice(html.indexOf('function renderWireRouting(){'),html.indexOf('function syncActiveSend(t){')).includes('stroke-dasharray') &&
  html.includes('source selection changes only on an accepted drop'),
  'V17 draws symmetric solid translucent curved wire, magnetic drop and no source-change flicker');
node('reset').dispatch('click');

// V15 LIVE GESTURE MODEL: drag from CH08 OUT leftward to CH03 IN, then Bus-only route.
node('body').clientWidth=1680;node('reset').dispatch('click');
function v15Drag(sourceId,targetId,pointerId=71){
 const jack={closest:k=>k==='.strip'?{dataset:{id:sourceId}}:null,
  setPointerCapture(){}};
 const out={closest:k=>k==='.wire-node-out'?jack:k==='.strip'?{dataset:{id:sourceId}}:null};
 node('body').handlers.pointerdown({target:out,button:0,pointerId,clientX:740,clientY:430,preventDefault(){}});
 fakeWindow.handlers.pointermove({pointerId,clientX:120,clientY:430});
 const inner={closest:k=>k==='.strip'?{dataset:{id:targetId}}:null};
 const target={closest:k=>k==='.wire-node-in'?inner:k==='.strip'?{dataset:{id:targetId}}:null};
 fakeWindow.handlers.pointerup({target,pointerId,clientX:120,clientY:430});
}
v15Drag(8,3);
check(strip(8).includes('⇢1') && strip(3).includes('wire-node-in is-linked') &&
 strip(8).includes('wire-node-out is-source'),
 'Dragging the OUT node from CH8 RIGHT to CH3 LEFT creates a real direction-agnostic 8→3 reference edge');
check(!strip(3).includes('value="CH08 PRE-FX SPLIT" selected>'),
 'Leftward drag is a parallel send and does not overwrite receiver hardware input');
const previous=strip(8);
const jack2={closest:k=>k==='.strip'?{dataset:{id:8}}:null};
node('body').handlers.pointerdown({target:{closest:k=>k==='.wire-node-out'?jack2:null},
 button:0,pointerId:72,clientX:480,clientY:430,preventDefault(){}});
fakeWindow.handlers.pointermove({pointerId:72,clientX:200,clientY:425});
fakeWindow.handlers.pointercancel({pointerId:72});
check(strip(8)===previous && node('info').textContent.includes('canceled'),
 'Canceled pointer gesture preserves all prior signal connections');
node('reset').dispatch('click');
// CH3 has no hidden legacy Bus A send. Source follows click selection.
field(3,'output','Master');
node('bankSelect').value='1';node('bankSelect').dispatch('change');
wireTo(11);
check(node('routeChoice').hidden===false &&
 node('info').textContent.includes('Bus routing choice required'),
 'Drop/click to Bus opens a nonmodal decision before changing any signal path');
node('routeOnly').dispatch('click');
check(node('routeChoice').hidden===true &&
 node('info').textContent.includes('VIA BUS ONLY') &&
 node('master').innerHTML.includes('wire-node-in'),
 'Explicit Route Only takes a single primary path to BUS (no parallel new Master feed)');
node('bankSelect').value='0';node('bankSelect').dispatch('change');
check(strip(3).includes('<option value="Bus A" selected>') &&
 !strip(3).includes('<option value="Master" selected>'),
 'VIA BUS ONLY makes CH3 primary output Bus A, not direct Master');
// Check return to normal direct-Main mode via unlink and choose parallel again.
node('bankSelect').value='1';node('bankSelect').dispatch('change');
wireTo(11);
node('bankSelect').value='0';node('bankSelect').dispatch('change');
check(strip(3).includes('<option value="SOURCE ONLY (No Main)" selected>'),
 'Unplugging serial Bus wire cannot silently restore direct Main');
field(3,'output','Master');
node('bankSelect').value='1';node('bankSelect').dispatch('change');
wireTo(11);node('routeParallel').dispatch('click');
const parallelNotice=node('info').textContent.includes('PARALLEL SEND');
node('bankSelect').value='0';node('bankSelect').dispatch('change');
check(strip(3).includes('<option value="Master" selected>') && parallelNotice,
 'Explicit parallel Bus send retains direct Master and does not rewrite primary OUT');
node('reset').dispatch('click');
check(html.includes('function moveChannelTo(') &&
 html.includes('application/x-arsonmix-channel') &&
 html.includes("arrangeSurface.addEventListener('dragstart'") &&
 html.includes("arrangeSurface.addEventListener('keydown'") &&
 html.includes('/* V16: When BOTH expandable FX and SEND are closed'),
 'V16 stable ID reorder by drag and Alt+arrows, smart two-panel fader reclaim');

// V18 physical receiver unplug/replug: a linked IN is a draggable cable end.
node('body').clientWidth=1680;node('reset').dispatch('click');
wireTo(3);
const oldEdgeId=Number(strip(3).match(/data-wire-id="(\d+)"/)?.[1]);
check(Number.isInteger(oldEdgeId),'V18 connected IN fixture has stable edge id');
const replugGain={dataset:{field:'wireGain',wireId:String(oldEdgeId)},value:'-17',
 style:{setProperty(){}},parentElement:{style:{setProperty(){}}},closest:k=>k==='.strip'?{dataset:{id:3}}:null};
node('body').handlers.change({target:replugGain,type:'change'});
const oldQs=document.querySelectorAll;
const makeSocket=(id,cx)=>({
 getBoundingClientRect(){return {left:cx-14,top:416,width:28,height:28}},
 closest:k=>k==='.strip'?{dataset:{id}}:
  k==='.wire-section'?{getBoundingClientRect(){return {left:cx-60,right:cx+60,top:360,bottom:450}}}:null,
 classList:{toggle(){}},setPointerCapture(){}
});
const sockets=[makeSocket(3,300),makeSocket(4,480),makeSocket(5,660)];
document.querySelectorAll=k=>k==='.wire-node-in'?sockets:oldQs(k);
function v18GrabTo(pointerId,fromId,toX,toY,finish='up'){
 const socket=sockets.find(s=>s.closest('.strip').dataset.id===fromId);
 const target={closest:k=>k==='.wire-node-in'?socket:null};
 node('body').handlers.pointerdown({target,button:0,pointerId,clientX:300,clientY:430,preventDefault(){}});
 fakeWindow.handlers.pointermove({pointerId,clientX:toX,clientY:toY});
 if(finish==='cancel')fakeWindow.handlers.pointercancel({pointerId});
 else fakeWindow.handlers.pointerup({target,pointerId,clientX:toX,clientY:toY});
}
v18GrabTo(111,3,480,430);
check(!strip(3).includes('wire-node-in is-linked') &&
 strip(4).includes('wire-node-in is-linked') &&
 strip(4).includes('data-wire-id="'+oldEdgeId+'"') &&
 strip(4).includes('value="-17" data-field="wireGain"') &&
 strip(1).includes('⇢1'),
 'V18 dragging linked CH3 IN → CH4 IN reroutes one same-ID wire and preserves gain');
node('reset').dispatch('click');wireTo(3);
v18GrabTo(112,3,300,480);
check(!strip(3).includes('wire-node-in is-linked') && strip(1).includes('wire-pot is-idle'),
 'V18 drop >31px away from old IN releases one cable without other routes');
node('reset').dispatch('click');wireTo(3);
v18GrabTo(113,3,320,430);
check(strip(3).includes('wire-node-in is-linked'),
 'V18 short move back into original socket magnet preserves connection');
v18GrabTo(114,3,480,430,'cancel');
check(strip(3).includes('wire-node-in is-linked') && !strip(4).includes('wire-node-in is-linked'),
 'V18 pointercancel restores existing wire; never changes graph');
wireTo(4);
v18GrabTo(115,3,480,430);
check(strip(3).includes('wire-node-in is-linked')&&strip(4).includes('wire-node-in is-linked') &&
 node('info').textContent.includes('REPLUG REJECTED'),
 'V18 replug into already-linked destination fails closed (no duplicate)');
check(html.includes('wire-floating-plug') &&
 html.includes('replugWireEdge(g.sourceId,g.originDestId') &&
 html.includes("if(wireSuppressClick&&['wireSource','wireRoute'].includes(a))"),
 'V18 floating plug preview and click-after-drag suppression are present');
document.querySelectorAll=oldQs;
node('reset').dispatch('click');


// V19 collapsed WIRE keeps a compact routing summary rather than an empty
// 23px rail. The default direct MAIN feed lives in I/O, not custom patch count.
node('reset').dispatch('click');
check(html.includes('data-section="route" aria-expanded="true" aria-label="Collapse input and output rack"') &&
 html.includes('class="rail-svg-icon io-icon"') && html.includes('class="rail-svg-icon fx-icon"') &&
 html.includes('class="rail-svg-icon wire-icon"') &&
 html.includes('content:""!important;display:block!important;') &&
 html.includes('function compactWireActivity(t){'),
 'V19 DAW rail has concise I/O, FX, W labels and no pseudo-caption OPEN/PATCH');
check(strip(3).includes('wire-fold-summary is-empty') &&
 strip(3).includes('fold-link-icon') && !strip(3).includes('has-in') && !strip(3).includes('has-out') &&
 strip(1).includes('wire-fold-summary has-wire') && strip(1).includes('fold-link-icon has-out'),
 'V24 no-route has both muted contacts and active legacy outgoing route only lights OUT dot');
wireTo(3);
check(strip(3).includes('fold-link-icon has-in') &&
 strip(1).includes('fold-link-icon has-out') &&
 strip(3).includes('fold-link-in-dot') && strip(1).includes('fold-link-out') &&
 !strip(3).includes('fold-link-trace'),
 'V24 collapsed incoming and outgoing contact indicators match expanded OUT-left IN-right');
wireTo(3);
check(strip(3).includes('wire-fold-summary is-empty') && strip(3).includes('fold-link-icon') &&
 !strip(3).includes('has-in') && !strip(3).includes('has-out'),
 'V24 removing a custom incoming edge restores muted dot AND socket');
node('reset').dispatch('click');


// V22 image-derived rail SVGs and zero-latch Smart Wire magnet.
check(html.includes('viewBox="0 0 64 64"') &&
 html.includes('fill-rule="evenodd"') &&
 html.includes('class="rail-svg-icon io-icon"') &&
 html.includes('class="rail-svg-icon fx-icon"') &&
 html.includes('class="rail-svg-icon wire-icon"'),
 'V22 has real reference-derived SVG FX/WIRE and physical I/O glyphs');
check(html.includes('preview:22,release:22,retained:24,origin:31') &&
 !html.includes('dist>42&&!inside') &&
 html.includes("'release'):null;") && html.includes('True magnetic PREVIEW') &&
 html.includes('x1=rect.left-vp.left+rect.width*.5;') &&
 !html.includes('wire-magnet-guide'),
 'V22 hover cannot finalize, preview follows pointer and release uses 22px threshold');
node('body').clientWidth=1680;node('reset').dispatch('click');
document.querySelectorAll=k=>k==='.wire-node-in'?sockets:oldQs(k);
function v22NewDrop(x,y,pid){
 const jack={closest:k=>k==='.strip'?{dataset:{id:6}}:null,setPointerCapture(){}};
 const nodeOut={closest:k=>k==='.wire-node-out'?jack:k==='.strip'?{dataset:{id:6}}:null};
 node('body').handlers.pointerdown({target:nodeOut,button:0,pointerId:pid,clientX:720,clientY:430,preventDefault(){}});
 fakeWindow.handlers.pointermove({pointerId:pid,clientX:x,clientY:y});
 fakeWindow.handlers.pointerup({target:nodeOut,pointerId:pid,clientX:x,clientY:y});
}
v22NewDrop(336,430,201);
check(!strip(3).includes('wire-node-in is-linked'),
 'V22 release 36px from socket must be rejected even in the WIRE bay');
v22NewDrop(320,430,202);
check(strip(3).includes('wire-node-in is-linked'),
 'V22 release 20px from receiving socket must snap and connect');
document.querySelectorAll=oldQs;
node('reset').dispatch('click');


// V23 collapse never renders numbers or visible jacks; no phantom click targets.
check(html.includes('function compactWireSummary(t)') &&
 html.includes("const shape='<svg class=") &&
 !html.includes('class="fold-marker"') &&
 html.includes('.send-section.wire-section .wire-tray.linked') &&
 html.includes('display:none!important;visibility:hidden!important;pointer-events:none!important;'),
 'V23 folded WIRE is a 23px icon-only row with clipped live controls completely hidden');
check(html.includes('preview:22,release:22,retained:24,origin:31') &&
 html.includes('track.id===held?WIRE_MAGNET.retained:WIRE_MAGNET.preview') &&
 html.includes('x1=rect.left-vp.left+rect.width*.5') &&
 html.includes('y1=rect.top-vp.top+rect.height*.5'),
 'V23 lit candidate means endpoint center-snaps and remains valid on pointerup');

// V24 physical compact status remains present and ordered for EVERY channel.
node('reset').dispatch('click');
const idleStrip=strip(3);
check(/fold-link-out" cx="8" cy="8" r="3\.45"/.test(idleStrip) &&
 /fold-link-in" cx="26" cy="8" r="5\.4"/.test(idleStrip) &&
 /fold-link-in-dot" cx="26" cy="8" r="1\.85"/.test(idleStrip) &&
 !idleStrip.includes('wire-disconnect') && !idleStrip.includes('data-action="wireRemove"'),
 'V24 fixed physical DOT left, larger receiving ring right and smaller inner dot; no X');
check(!html.includes("if(a==='wireRemove')") &&
 html.includes('replugWireEdge(g.sourceId,g.originDestId,snap?.track.id??null)') &&
 html.includes('wire-tray .wire-pot'),
 'V24 removes close-button handler and retains drag-to-unplug / gain knob');

// V26 selected-channel Focus: all inbound links are behind outbound sends,
// including legacy Send slots, split inputs and primary Bus outputs.
node('reset').dispatch('click');
wireTo(3); // CH01 -> CH03
const v26source={dataset:{action:'wireSource'},closest:k=>k==='.strip'?{dataset:{id:2}}:null};
node('body').handlers.click({target:{closest:k=>k==='[data-action]'?v26source:null}}); // CH02 selected
wireTo(3); // CH02 -> CH03
const v26ch3={dataset:{action:'wireSource'},closest:k=>k==='.strip'?{dataset:{id:3}}:null};
node('body').handlers.click({target:{closest:k=>k==='[data-action]'?v26ch3:null}});
wireTo(4); // CH03 -> CH04
const legend26=node('wireLegend').innerHTML;
check(legend26.includes('wire-map-focus') && legend26.includes('CH03') &&
 legend26.includes('wire-map-group is-in') && legend26.includes('wire-map-group is-out') &&
 legend26.includes('CH01') && legend26.includes('CH02') && legend26.includes('CH04'),
 'V26 channel focus legend lists both incoming CH01/CH02 and outgoing CH04');
check(html.includes('function wireDisplayRoutes(){') &&
 html.includes("routes.filter(e=>e.destId===source.id") &&
 html.includes("routes.filter(e=>e.sourceId===source.id") &&
 html.includes('wire-incoming-line') && html.includes('wire-outgoing-line') &&
 html.indexOf('const incoming=routes.filter') < html.indexOf('const outgoing=routes.filter'),
 'V26 graph display paints IN behind OUT from one read-only route model');
check(html.includes("const split=parseSplitSource(t.input)") &&
 html.includes("if(dest)add(t.id,dest.id,'legacy')") &&
 html.includes("add(t.id,primary.id,'primary')") &&
 html.includes("if(split)add(split.sourceId,t.id,'split')"),
 'V26 Focus includes existing legacy sends, split and primary Bus instead of wire-edge only');
check(html.includes('wire-map-chip') && html.includes('--wire-hue:') &&
 html.includes("nodes.slice(0,2)") && html.includes("wire-map-more"),
 'V26 header displays compact channel-colored badges, capped with +N overflow');
node('reset').dispatch('click');


// V30: Raised bright graphite must be distinct from dark chassis,
 // while the active key is independently recessed in static housing.
check(html.includes('--am-key30-top:#738692') &&
 html.includes('--am-key30-bottom:#354750') &&
 html.includes('--am-key30-down-top:#27353d') &&
 html.includes('--am-key30-down-bottom:#151e25') &&
 html.includes('--am-key30-hover-top:#8296a0'),
 'V30 physical idle, hover and depressed material tokens are present');
check(html.includes('hw-switch-socket::after') &&
 html.includes('z-index:4!important') &&
 html.includes('pointer-events:none!important') &&
 html.includes('0 2px 0 #060c10') &&
 html.includes('inset 0 2px 1px #000b'),
 'V30 static upper socket bezel, 2px hard extrusion and recessed cap');
check(html.includes('--am-key30-led:#ef6376') &&
 html.includes('--am-key30-led:#f6d16b') &&
 html.includes('--am-key30-led:#78d3e9') &&
 !html.includes('/* V29 — Dan Andersson'),
 'V30 state light is a crisp colored edge, not blurred neon text');
check(html.includes('data-action="mute"') && html.includes('data-action="solo"') &&
 html.includes('data-action="cue"') && !html.includes('cdnjs.cloudflare.com/ajax/libs/jquery'),
 'V30 retains three independent mixer buttons and no external jQuery dependency');

// V30 display modes: never mutate graph; view is an explicit operator choice.
node('reset').dispatch('click');
check(html.includes('id="wireView"') &&
 html.includes('<option value="focus">Focus</option>') &&
 html.includes('<option value="out">OUT only</option>') &&
 html.includes('<option value="all">All wires</option>'),
 'V30 compact user-selectable Focus, OUT only and All wire display modes');
check(node('wireView').value==='focus',
 'V30 Focus is default after reset; no global cable clutter');
const snapshot30=node('tracks').innerHTML;
node('wireView').value='out';node('wireView').dispatch('change');
check(node('wireView').value==='out' && node('tracks').innerHTML===snapshot30 &&
 node('info').textContent.includes('display only'),
 'V30 OUT view changes paint only and does not rebuild or rewrite mixer graph');
node('wireView').value='all';node('wireView').dispatch('change');
check(node('wireView').value==='all' && node('tracks').innerHTML===snapshot30,
 'V30 All view remains display-only and preserves channel geometry');
check(html.includes("if(wireView==='all'&&!dragging)") &&
 html.includes('class="wire-context-line"') &&
 html.includes("const incoming=wireView==='out'?[]:") &&
 html.includes("if(edge.sourceId===source.id||edge.destId===source.id)continue;"),
 'V30 ALL only draws separate other-channel routes behind focused paths');
check(html.indexOf('class="wire-context-line"')<html.indexOf('class="wire-incoming-line"') &&
 html.indexOf('class="wire-incoming-line"')<html.indexOf('class="wire-outgoing-line"'),
 'V30 paint order is background context, incoming, bright outgoing');
node('reset').dispatch('click');
check(node('wireView').value==='focus',
 'V30 reset restores Focus with graph/editor defaults intact');
// V31 top patch strip and gravity; V32 soft per-edge settling replaces global spring.
check(html.includes('/* V31 — FL-style suspended routing:') &&
 html.includes('.wire-tray{\n order:-2!important') &&
 html.includes('.wire-section .wire-deck{\n order:0!important') &&
 html.includes('function wireSagClearance(rect,section)'),
 'V31 OUT/IN patch hardware precedes a clean lower wire stage without moving strip geometry');
check(html.includes('(y0+control).toFixed(1)') &&
 html.includes('(y1+control).toFixed(1)') &&
 !html.includes('(y0-rise).toFixed(1)'),
 'V31 both cubic control points hang BELOW their anchors, including right-to-left routes');
check(html.includes('let wireMotion={key:null,offset:0,token:0}') &&
 html.includes('frame<14') &&
 html.includes('wireMotion.offset=1.7*Math.exp(-frame*.28)') &&
 html.includes('data-wire-destination') &&
 html.includes('wireMotionGeometry.get(key)') &&
 html.includes('path.setAttribute(\'d\',d)') &&
 html.includes("window.matchMedia?.('(prefers-reduced-motion: reduce)')?.matches") &&
 html.includes('kickWireSpring(source.id,dest.id)') &&
 !html.includes('wireSpringVelocity'),
 'V32 bounded settling updates only newly plugged wire SVG path, honors reduced motion');
check(html.includes('wirePathData(x0,y0,x1,y1,sagLimit);') &&
 html.includes('wireSagClearance(fromRect,senderBay)') &&
 html.includes('wirePathFor(edge,xa,ya,xb,yb,rise)'),
 'V31 drag and incoming/outgoing share the new downward gravity geometry');
check(html.includes('wireSourcePinned=true') &&
 html.includes('data-wire-jump-source="1"') &&
 html.includes('wire-offbank-entry') &&
 html.includes('ALREADY CONNECTED:') &&
 html.includes('data-cable-key'),
 'V32 pin source across banks, indicate existing SEND and animate only keyed SVG paths');
console.log('PASS '+cases.length+' reference mixer DOM checks');
cases.forEach(x=>console.log('  ✓ '+x));
