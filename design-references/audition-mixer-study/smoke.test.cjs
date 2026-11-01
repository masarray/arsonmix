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
  style:{setProperty(){}},setAttribute(){},addEventListener(k,cb){handlers[k]=cb;},
  querySelector(){return null;},
  dispatch(k){assert.ok(handlers[k],name+' '+k+' handler missing');handlers[k]({target:x});}};
 elements.set(name,x);return x;
}
const document={getElementById:node,querySelector:k=>k==='.console-body'?node('body'):k==='.section-rail'?node('rail'):null,querySelectorAll:()=>[]};
const fakeWindow={handlers:{},addEventListener(k,fn){this.handlers[k]=fn;}};
vm.runInNewContext(script,{document,window:fakeWindow,requestAnimationFrame(){}},{timeout:3000});
function check(ok,title){assert.ok(ok,title);cases.push(title);}
const count=()=> (node('tracks').innerHTML.match(/<article class="strip/g)||[]).length;
function filter(value){node('viewFilter').value=value;node('viewFilter').dispatch('change');}
function field(id,name,value){node('body').handlers.change({target:{dataset:{field:name},value,closest:k=>k==='.strip'?{dataset:{id}}:null}});}
function strip(id){const html=node('tracks').innerHTML,p=html.indexOf('data-id="'+id+'"');assert.ok(p!==-1,'strip '+id+' exists');return html.slice(p,html.indexOf('</article>',p));}
check(count()===11,'10 audio and 1 Bus');
check(node('master').innerHTML.includes('Master'),'pinned Master');
node('copy').dispatch('click');node('addTrack').dispatch('click');node('paste').dispatch('click');
check(node('info').textContent.includes('Pasted compatible'),'copy/paste');
node('addFx').dispatch('click');check(node('trackCount').textContent.includes('1 FX return'),'FX Return creation');
filter('fx');check(count()===1,'FX Return filter');filter('all');
node('addBus').dispatch('click');check(node('trackCount').textContent.includes('2 bus'),'Bus creation');
field(15,'output','Bus A');check(node('info').textContent.includes('output → Bus A'),'bus route');
field(11,'output','Bus B');check(node('info').textContent.includes('Routing rejected'),'feedback detection');
field(1,'activeSend','S2');check(node('info').textContent.includes('Send 2'),'send slot switch');
field(1,'send','FX 1');filter('all');check(strip(1).includes('<option value="FX 1" selected>'),'send to FX Return');
field(1,'activeSend','S1');filter('all');check(strip(1).includes('<option value="Bus A" selected>'),'16 sends preserve independent slots');
node('saveScene').dispatch('click');check(node('sceneSelect').value==='0','scene save');
node('mode').dispatch('click');check(node('body').classList.contains('narrow'),'narrow console');
filter('audio');check(count()===11,'audio filter');
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
pluginAction('fav','demo-8');
node('pluginView').value='favorites';node('pluginView').dispatch('change');
check(node('pluginResults').innerHTML.includes('Chorus'),'Favorite effect filtering');
node('pluginView').value='all';node('pluginView').dispatch('change');
pluginAction('hide','demo-8');
check(!node('pluginResults').innerHTML.includes('Chorus'),'Hidden effect removed from normal list');
node('pluginView').value='hidden';node('pluginView').dispatch('change');
check(node('pluginResults').innerHTML.includes('Chorus'),'Hidden effect discoverable in Hidden view');
pluginAction('hide','demo-8');
node('pluginView').value='all';node('pluginView').dispatch('change');
pluginAction('choose','demo-2');
check(plug.hidden===true,'Plugin inserts and browser closes');
filter('all');
check(strip(1).includes('Studio Reverb'),'FX insert changes in channel strip');
node('pluginsButton').dispatch('click');
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
check(count()===8,'Bank 1 displays eight channel strips');
node('bankSelect').value='1';node('bankSelect').dispatch('change');
check(count()===3,'Bank 2 displays remaining strips');
node('bankSelect').value='all';node('bankSelect').dispatch('change');
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

console.log('PASS '+cases.length+' reference mixer DOM checks');
cases.forEach(x=>console.log('  ✓ '+x));
