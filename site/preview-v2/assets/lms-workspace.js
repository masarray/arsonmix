/* ArSonMix LMS: five focused pages; native offline design inspection/ACK
 * only. One JUCE engine owns device identity and accepted PCM; this view
 * NEVER writes audio or turns a draft into a speaker hardware Apply.
 */
(function () {
"use strict";
const M=window.ArSonMixLmsModel, root=document.getElementById("lmsWorkspace");
const app=document.querySelector(".app"), launch=document.getElementById("lmsOpen");
if (!M||!root||!app||!launch||root.dataset.ready) return;
root.dataset.ready="1";
let state=M.createSystem({ways:3,stereo:true,ports:M.demoPorts(6)});
let page="system",selectedPort=state.ports[0],pending=null,message="";
let dspTab="eq",selectedBand=0;
let dragging=null;
// Browser preview has NO native bridge, demo ports or pretend live ACK.
// Desktop requests actual ASIO port inventory from the sole JUCE engine.
const backend=window.__JUCE__?.backend;
const nativeConnected=!!(backend?.emitEvent&&backend?.addEventListener);
let nativeInventory=null,nativeStatus="Inspection required";
let nativeInspectRequest="",nativeSerial=0,nativePending=null;
let validatedForRevision=-1,validatedGraph="",validatedDevice="";
function nativeBound() {
 return !!(nativeInventory?.ready && Array.isArray(nativeInventory.ports) &&
   state.ports.length &&
   state.ports.every(p=>nativeInventory.ports.some(x=>x.identity===p&&!x.occupied)));
}
function nativeAvailable() {
 return nativeInventory?.ready?nativeInventory.ports.filter(x=>!x.occupied):[];
}
function portLabel(id) {
 const actual=nativeInventory?.ports?.find(x=>x.identity===id);
 return actual?"OUT "+(Number(actual.callbackOrdinal)+1):
  id.replace("DEMO-","D");
}
function inspectNative() {
 if(!nativeConnected)return;
 nativeInspectRequest="lms-inspect-"+(++nativeSerial);
 backend.emitEvent("arMixLmsInspect",{requestId:nativeInspectRequest});
}
function proposedOutputs() {
 return state.ports.filter(p=>state.routes[p]).map(port=>{
  const section=state.sections.find(s=>s.id===state.routes[port]);
  const dsp=state.dsp[port];
  if(!section||!dsp)throw new Error("Incomplete speaker output draft");
  return {
   outputId:port,portIdentity:port,
   sourceSide:section.side==="R"?1:0,section:section.index,
   gainDb:dsp.gainDb,delayMs:dsp.delayMs,
   muted:dsp.muted,polarityInverted:dsp.polarity==="invert",
   limiterEnabled:dsp.limiterEnabled,
   limiterThresholdDb:dsp.limiterThresholdDb,
   peq:(dsp.peq||[]).map(b=>({
    frequencyHz:b.frequencyHz,q:b.q,gainDb:b.gainDb,enabled:b.enabled
   }))
  };
 });
}
function validateNativeDraft() {
 if(!nativeConnected||!nativeBound()||!nativeInventory.outputDisarmed ||
    nativePending||!Object.keys(state.routes).length)return;
 try {
  M.validateSystem(state);
  const outputs=proposedOutputs();
  const id="lms-validate-"+(++nativeSerial);
  const document={
   schema:state.schema,applied:false,ways:state.ways,
   stereo:state.stereo,cutoffsHz:state.cutoffsHz.slice(),outputs
  };
  nativePending={requestId:id,uiRevision:state.revision,
   deviceEpoch:nativeInventory.deviceEpoch,graphRevision:nativeInventory.graphRevision,
   outputs,cutoffsHz:document.cutoffsHz,ackedRevision:null};
  nativeStatus="Validating muted design against native ASIO graph";
  backend.emitEvent("arMixLmsValidate",{
   schema:"arsonmix.lms.native-design-intent.v1",requestId:id,
   deviceEpoch:nativeInventory.deviceEpoch,
   graphRevision:nativeInventory.graphRevision,
   expectedDesignRevision:nativeInventory.designRevision,
   sampleRate:nativeInventory.sampleRate,
   maxFrames:nativeInventory.maxFrames,
   activeOutputChannels:nativeInventory.activeOutputChannels,
   design:document
  });
  render();
 }catch(error){nativeStatus="Rejected: "+error.message;nativePending=null;render();}
}
function sameNativeReadback(snapshot,pending) {
 const actual=snapshot.validatedDesign;
 if(!actual || snapshot.hardwareApplied!==false ||
    snapshot.deviceEpoch!==pending.deviceEpoch ||
    snapshot.graphRevision!==pending.graphRevision ||
    snapshot.designRevision!==pending.ackedRevision ||
    actual.ways!==state.ways || actual.stereo!==state.stereo ||
    JSON.stringify(actual.cutoffsHz)!==JSON.stringify(pending.cutoffsHz))
    return false;
 if(!Array.isArray(actual.outputs)||actual.outputs.length!==pending.outputs.length)
    return false;
 // Complete native readback parity is mandatory. Reading only route/mute
 // permits an old EQ/delay/polarity to be falsely advertised as validated.
 // All values are bounded and numerically decoded by the JUCE intent parser.
 if(snapshot.outputDisarmed!==true)return false;
 const bandEqual=(x,p)=>x&&p&&
   x.frequencyHz===p.frequencyHz&&x.q===p.q&&
   x.gainDb===p.gainDb&&x.enabled===p.enabled;
 return pending.outputs.every(p=>actual.outputs.some(x=>
   x.outputId===p.outputId&&x.portIdentity===p.portIdentity&&
   x.sourceSide===p.sourceSide&&x.section===p.section&&x.muted===p.muted&&
   x.gainDb===p.gainDb&&x.delayMs===p.delayMs&&
   x.polarityInverted===p.polarityInverted&&
   x.limiterEnabled===p.limiterEnabled&&
   x.limiterThresholdDb===(p.limiterThresholdDb===null?-6:p.limiterThresholdDb)&&
   x.voltageModelPresent===false&&
   Array.isArray(x.peq)&&x.peq.length===p.peq.length&&
   p.peq.every((band,i)=>bandEqual(x.peq[i],band))));
}
if(nativeConnected){
 backend.addEventListener("arMixLmsInspection",response=>{
  if(!response||response.schema!=="arsonmix.lms.native-inspection.v1"||
     response.requestId!==nativeInspectRequest)return;
  if(!response.ready){
   if(pending?.kind==="reassign")pending=null;
   nativeInventory=null;
   nativeStatus="Native ASIO graph unavailable · output disconnected";
   validatedForRevision=-1;
   nativePending=null;
   render();return;
  }
  if(!Array.isArray(response.ports) ||
     response.ports.some(x=>typeof x.identity!=="string"||
       !Number.isInteger(x.callbackOrdinal)||typeof x.occupied!=="boolean") ||
     typeof response.deviceEpoch!=="string" ||
     typeof response.graphRevision!=="string" ||
     typeof response.designRevision!=="string" ||
     !Number.isFinite(response.sampleRate) ||
     !Number.isInteger(response.maxFrames)){
   nativeInventory=null;nativePending=null;
   nativeStatus="Invalid native port readback";render();return;
  }
  if(pending?.kind==="reassign" &&
      (pending.deviceEpoch!==response.deviceEpoch ||
       pending.graphRevision!==response.graphRevision))pending=null;
  nativeInventory=response;
  if(nativePending?.ackedRevision!==null&&nativePending &&
     state.revision===nativePending.uiRevision &&
     sameNativeReadback(response,nativePending)){
   validatedForRevision=state.revision;
   validatedGraph=response.graphRevision;
   validatedDevice=response.deviceEpoch;
   nativeStatus="Native validated draft · hardware NOT applied";
   nativePending=null;
  }else if(nativePending?.ackedRevision!==null&&nativePending){
   nativeStatus="Native readback mismatch · draft NOT validated";
   nativePending=null;
   validatedForRevision=-1;
  }else if(validatedForRevision!==state.revision ||
           validatedGraph!==response.graphRevision ||
           validatedDevice!==response.deviceEpoch){
   validatedForRevision=-1;
   nativeStatus=response.outputDisarmed?
      "Verified ASIO ports · select and validate a muted design":
      "Verified ASIO ports · mute ALL outputs before validation";
  }
  render();
 });
 backend.addEventListener("arMixLmsDesignAck",ack=>{
  if(!nativePending || !ack ||
     ack.schema!=="arsonmix.lms.native-design-ack.v1" ||
     ack.requestId!==nativePending.requestId)return;
  if(ack.status!=="VALIDATED_PENDING_READBACK" || ack.hardwareApplied!==false){
   nativeStatus="Native REJECTED: "+String(ack.errorCode||"INVALID_DESIGN");
   validatedForRevision=-1;
   nativePending=null;
   inspectNative();
   render();return;
  }
  nativePending.ackedRevision=ack.designRevision;
  nativeStatus="Native ACK received · checking subsequent readback";
  inspectNative();render();
 });
}
function nativeBar() {
 if(!nativeConnected)return "";
 const ports=nativeAvailable();
 const canBind=nativeInventory?.ready && ports.length>0&&ports.length<=64;
 const ready=nativeBound()&&nativeInventory.outputDisarmed &&
   Object.keys(state.routes).length>0&&!nativePending;
 const verified=nativeInventory?.ready===true&&
   nativeInventory.outputDisarmed===true&&nativeInventory.hardwareApplied===false&&
   nativeBound()&&validatedForRevision===state.revision&&
   validatedGraph===nativeInventory?.graphRevision&&
   validatedDevice===nativeInventory?.deviceEpoch;
 return '<div class="lms-native-bar" role="status">'+
  '<span><strong>'+safe(verified?'VALIDATED DRAFT':'NATIVE INSPECTION')+
  '</strong> · '+safe(nativeStatus)+
  '<small> · '+ports.length+' free ASIO pins · No speaker output authorization</small></span>'+
  '<div class="lms-control-row">'+
  btn("Inspect ASIO","nativeInspect")+
  (canBind?btn("Use verified ports","nativePorts"):'')+
  (canBind&&Object.keys(state.routes).length?
    btn("Reassign + keep DSP","nativeReassign"):'')+
  (nativeBound()?'<button type="button" class="lms-btn" data-action="nativeValidate"'+
   (ready?'':' disabled')+'>Validate design</button>':'')+
  '</div></div>';
}

const pageNames={system:"System",crossover:"Crossover",matrix:"Matrix",
 processing:"Processing",alignment:"Alignment"};
const pages=Object.keys(pageNames);
const safe=x=>String(x).replace(/[&<>"']/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
const number=x=>Number(x).toFixed(1);
const unit=x=>Number(x).toFixed(2);
const color=i=>["#59bfd0","#8cb7dd","#d9b77e","#88c6ae","#b5a8dc","#cb9fab"][i%6];
function selectOptions(items,value) {
 return items.map(o=>'<option value="'+safe(o[0])+'"'+(String(o[0])===String(value)?' selected':'')+'>'+safe(o[1])+'</option>').join("");
}
function btn(label,act,active=false) {
 return '<button type="button" class="lms-btn'+(active?' active':'')+'" data-action="'+safe(act)+'">'+safe(label)+'</button>';
}
function stats() {
 return '<div class="lms-summary"><div class="lms-stat"><b>'+state.ways+'</b><span>Ways / source</span></div>'+
 '<div class="lms-stat"><b>'+state.sections.length+'</b><span>Section signals</span></div>'+
 '<div class="lms-stat"><b>'+Object.keys(state.routes).length+'</b><span>Draft patches</span></div>'+
 '<div class="lms-stat"><b>'+state.ports.length+'</b><span>'+(nativeBound()?"Verified ASIO ports":"Demo output ports")+'</span></div></div>';
}
function overviewMap() {
 return '<div class="lms-speaker-map" aria-label="Illustration of stage and speaker positions">'+
 '<div class="stage">STAGE / MAIN PA</div>'+
 '<span class="lms-map-spk" style="left:18%;top:32%">LEFT '+state.ways+'-WAY</span>'+
 '<span class="lms-map-spk" style="right:18%;top:32%">RIGHT '+state.ways+'-WAY</span>'+
 '<span class="lms-listener">LISTENER</span></div>';
}
function systemPage() {
 return '<div class="lms-grid">'+
 '<section class="lms-panel"><h3>Speaker system <span>Signal flow</span></h3>'+
 '<div class="lms-steps"><span class="lms-step"><strong>MAIN / BUS</strong><small>Logical source</small></span><span class="lms-arrow">›</span>'+
 '<span class="lms-step"><strong>'+state.ways+'-Way XOVER</strong><small>'+state.sections.length+' sections</small></span><span class="lms-arrow">›</span>'+
 '<span class="lms-step"><strong>MATRIX</strong><small>Explicit patch</small></span><span class="lms-arrow">›</span>'+
 '<span class="lms-step"><strong>OUTPUT DSP</strong><small>Per hardware pin</small></span></div>'+
 '<div class="lms-line"></div>'+stats()+
 '<p class="lms-tip">Choose the number of ways in Crossover first, then route sections to outputs in Matrix. All changes remain offline drafts.</p>'+
 '<div class="lms-control-row">'+btn("Edit Crossover","go:crossover")+
 btn("Open Matrix","go:matrix")+'</div></section>'+
 '<section class="lms-panel"><h3>Speaker placement <span>Top view</span></h3>'+
 overviewMap()+'<div class="lms-line"></div><div class="lms-control-row">'+
 btn("Visual alignment","go:alignment")+'<span class="lms-caption">No measured acoustic data</span></div></section></div>';
}
function freqX(hz) {return 24+(Math.log10(hz)-Math.log10(20))/(Math.log10(20000)-Math.log10(20))*650;}
function frequencyPage() {
 // Numeric response is the exact same two cascaded Butterworth magnitude
 // model as the native PreparedSpeakerSystem LR4 kernel, at assumed 48 kHz.
 const freqPoints=Array.from({length:116},(_,i)=>20*Math.pow(1000,i/115));
 const dbToY=db=>Math.max(30,Math.min(196,30+(3-db)*3.75));
 const guides=[0,-6,-12,-24,-36].map(db=>
  '<line x1="24" x2="674" y1="'+dbToY(db)+'" y2="'+dbToY(db)+
  '" stroke="#45606b" stroke-opacity=".45" stroke-dasharray="'+(db===0?'0':'2 5')+'"/>'+
  '<text x="20" y="'+(dbToY(db)+3)+'" text-anchor="end" fill="#8caeba" font-size="9">'+db+'</text>').join("");
 let curves="",rows="";
 for(let i=0;i<state.ways;i++){
  const path=freqPoints.map((hz,j)=>{
   const mag=M.crossoverMagnitude(state.cutoffsHz,i,hz,48000);
   const db=20*Math.log10(Math.max(1e-7,mag));
   return (j?"L":"M")+freqX(hz).toFixed(2)+" "+dbToY(db).toFixed(2);
  }).join(" ");
  const shade=color(i);
  curves+='<path d="'+path+'" stroke="'+shade+'" stroke-width="2.5" fill="none" stroke-linecap="round"/>';
  const edges=[20].concat(state.cutoffsHz,[20000]);
  rows+='<div class="lms-band-row"><span class="lms-band-mark" style="background:'+shade+'"></span>'+
    '<span class="lms-band-name">'+safe(state.sections[i].band)+'</span><span class="lms-muted">'+
    safe(Math.round(edges[i])+'–'+Math.round(edges[i+1])+' Hz')+'</span></div>';
 }
 const crossoverMarks=state.cutoffsHz.map(hz=>
   '<line x1="'+freqX(hz)+'" x2="'+freqX(hz)+'" y1="30" y2="196" stroke="#8eafb8" stroke-opacity=".55" stroke-dasharray="4 4"/>').join("");
 const ticks=[20,100,1000,10000,20000].map(hz=>
   '<text x="'+freqX(hz)+'" y="219" text-anchor="middle" fill="#9db8c1" font-size="11">'+
   (hz===20000?"20k":hz===10000?"10k":hz===1000?"1k":hz)+'</text>').join("");
 const splits=state.cutoffsHz.map((v,i)=>
  '<div class="lms-split-row"><label for="lms-x'+i+'">Split '+(i+1)+'</label>'+
  '<input id="lms-x'+i+'" class="lms-number" type="number" min="20" max="20000" step="1" value="'+v+
  '" data-cutoff="'+i+'"/><span class="lms-muted">Hz</span></div>').join("");
 return '<div class="lms-grid"><section class="lms-panel">'+
  '<h3>Crossover frequency response <span>LR4 · 24 dB/oct · calculated at 48 kHz</span></h3>'+
  '<svg class="lms-frequency" viewBox="0 0 700 242" role="img" aria-label="Calculated LR4 crossover magnitude responses with gain in decibels by frequency">'+
  '<rect x="24" y="30" width="650" height="166" fill="#0b1720"/>'+guides+
  [50,100,500,1000,5000,10000].map(hz=>'<line x1="'+freqX(hz)+'" x2="'+freqX(hz)+'" y1="30" y2="196" stroke="#405461" stroke-opacity=".35"/>').join("")+
  crossoverMarks+curves+ticks+'</svg>'+
  '<p class="lms-caption">Calculated electrical LR4 magnitude. No microphone measurement, speaker response, acoustic summation or phase correction. Output remains disconnected.</p>'+
  '<div class="lms-line"></div><div class="lms-control-row">'+
  '<label class="lms-label" for="lms-way">WAYS</label>'+
  '<select class="lms-select" id="lms-way" data-ways>'+selectOptions(Array.from({length:16},(_,i)=>[String(i+1),(i+1)+" Way"]),state.ways)+'</select>'+
  '<label class="lms-label" for="lms-stereo">SOURCE</label>'+
  '<select class="lms-select" id="lms-stereo" data-stereo>'+selectOptions([["1","Stereo L/R"],["0","Mono"]],state.stereo?"1":"0")+'</select></div>'+
  '<p class="lms-warn">A structural change clears every matrix patch and output protection draft. Never reconnect an HF speaker automatically.</p>'+
  '</section><section class="lms-panel">'+
  '<h3>Section boundaries <span>Linked '+(state.stereo?"L/R":"mono")+' design</span></h3>'+
  rows+'<div class="lms-line"></div>'+splits+
  '<p class="lms-tip">Start with section count, then patch sections to destinations in Matrix. Split frequencies must be checked against speaker specifications and actual device sample rate.</p>'+
  '<div class="lms-control-row">'+btn("Route sections → Matrix","go:matrix")+'</div>'+
  '</section></div>';
}
function matrixPage() {
 const headers=state.ports.map(p=>'<th scope="col" title="'+safe(p)+'">'+safe(portLabel(p))+'</th>').join("");
 const rows=state.sections.map(s=>'<tr><th scope="row">'+safe(s.name)+'</th>'+
 state.ports.map(p=>'<td><button type="button" class="lms-cross'+(state.routes[p]===s.id?' active':'')+
 '" data-route-port="'+safe(p)+'" data-route-section="'+safe(s.id)+'" aria-pressed="'+
 (state.routes[p]===s.id?"true":"false")+'" aria-label="Assign '+safe(s.name)+' to '+safe(p)+'">'+
 (state.routes[p]===s.id?'✓':'·')+'</button></td>').join("")+'</tr>').join("");
 return '<section class="lms-panel"><h3>Section → Output Matrix <span>Exclusive destination · source fan-out allowed</span></h3>'+
 '<div class="lms-control-row">'+(nativeBound()?
 '<span class="lms-caption">Verified native ASIO roster · output pins remain DISCONNECTED</span>':
 '<label class="lms-label" for="lms-count">DEMO PORTS</label>'+
 '<select id="lms-count" data-ports class="lms-select">'+
 selectOptions([2,4,6,8,12,16,24,32].map(n=>[String(n),n+" ports"]),state.ports.length)+
 '</select><span class="lms-caption">Hardware pin identity will come from ASIO discovery; these IDs are placeholders.</span>')+'</div>'+
 '<div class="lms-line"></div><div class="lms-table-scroll" style="max-height:430px"><table class="lms-matrix"><thead><tr><th scope="col">CROSSOVER</th>'+
 headers+'</tr></thead><tbody>'+rows+'</tbody></table></div>'+
 '<p class="lms-tip">Click a crosspoint to assign/unassign that section. A destination can have only one section; several destinations may share a source. A different hardware device must be explicitly mapped later.</p>'+
 '</section>';
}
function outputList() {
 return '<div class="lms-output-list">'+state.ports.map(p=>
  '<button type="button" class="lms-output'+(selectedPort===p?' active':'')+'" data-select-port="'+safe(p)+'">'+
  '<strong title="'+safe(p)+'">'+safe(portLabel(p))+'</strong><small>'+safe(state.sections.find(s=>s.id===state.routes[p])?.name||"Unassigned")+'</small>'+
  '</button>').join("")+'</div>';
}
function dspPage() {
 const p=state.dsp[selectedPort]||{},section=state.sections.find(s=>s.id===state.routes[selectedPort]);
 const bands=p.peq||[];
 const selector='<div class="lms-control-row lms-subtabs" role="group" aria-label="Output processing editor">'+
 ["eq","level","protection"].map(id=>
  '<button type="button" class="lms-btn'+(dspTab===id?' active':'')+
  '" data-dsp-tab="'+id+'" aria-pressed="'+(dspTab===id)+'">'+
  ({eq:"Equalizer",level:"Level & Timing",protection:"Protection"}[id])+'</button>').join("")+'</div>';
 let editor="";
 if(dspTab==="eq"){
   const pointX=i=>24+650*i/95,yDb=db=>30+(18-Math.max(-18,Math.min(18,db)))*4.5;
   const pth=Array.from({length:96},(_,i)=>{
     const hz=20*Math.pow(1000,i/95);
     const db=M.peqMagnitude(bands,hz);
     return (i?'L':'M')+pointX(i).toFixed(2)+' '+yDb(db).toFixed(2);
   }).join(' ');
   const eqRows=bands.map((b,i)=>
    '<button type="button" data-select-band="'+i+'" class="lms-output'+(selectedBand===i?' active':'')+'">'+
    '<strong>EQ '+(i+1)+'</strong><small>'+(b.enabled?'ON':'OFF')+
    ' · '+Math.round(b.frequencyHz)+' Hz · '+number(b.gainDb)+' dB</small></button>').join("");
   const current=bands[selectedBand];
   editor='<h3>Parametric EQ <span>8-band capacity · design only</span></h3>'+
    '<svg class="lms-frequency" viewBox="0 0 700 242" role="img" aria-label="Calculated parametric equalizer electrical magnitude response">'+
    '<rect x="24" y="30" width="650" height="166" fill="#0b1720"/>'+
    [-18,-9,0,9,18].map(db=>'<line x1="24" x2="674" y1="'+yDb(db)+'" y2="'+yDb(db)+
     '" stroke="#49616b" stroke-dasharray="'+(db===0?'0':'3 5')+
     '" opacity=".55"/><text x="19" y="'+(yDb(db)+3)+'" text-anchor="end" fill="#a1b8bf" font-size="9">'+
     (db>0?'+':'')+db+'</text>').join('')+
    '<path d="'+pth+'" fill="none" stroke="#5bc5d1" stroke-width="2.6"/>'+
    [20,100,1000,10000,20000].map(hz=>'<text x="'+freqX(hz)+'" y="218" text-anchor="middle" fill="#9bb4be" font-size="10">'+
     (hz===20000?'20k':hz===10000?'10k':hz===1000?'1k':hz)+'</text>').join('')+
    '</svg><p class="lms-caption">Calculated peaking EQ response at 48 kHz. No acoustic measurement or output PCM.</p>'+
    '<div class="lms-line"></div><div class="lms-output-list">'+eqRows+'</div>'+
    '<div class="lms-control-row" style="margin-top:10px">'+
    '<button class="lms-btn" type="button" data-action="addPeq" '+(bands.length>=8?'disabled':'')+'>+ Add band</button>'+
    (current?btn("Remove selected","removePeq"):'')+'</div>'+
    (current?'<div class="lms-line"></div><div class="lms-form-grid">'+
     '<div class="lms-field"><label for="lms-eq-f">Frequency (Hz)</label><input class="lms-number" id="lms-eq-f" type="number" min="20" max="20000" data-peq-field="frequencyHz" step="1" value="'+current.frequencyHz+'"/></div>'+
     '<div class="lms-field"><label for="lms-eq-g">Gain (dB)</label><input class="lms-number" id="lms-eq-g" type="number" min="-18" max="18" data-peq-field="gainDb" step=".1" value="'+current.gainDb+'"/></div>'+
     '<div class="lms-field"><label for="lms-eq-q">Q</label><input class="lms-number" id="lms-eq-q" type="number" min=".1" max="18" data-peq-field="q" step=".1" value="'+current.q+'"/></div>'+
     '<div class="lms-field"><label for="lms-eq-en">Band state</label><select class="lms-select" id="lms-eq-en" data-peq-field="enabled">'+
     selectOptions([["1","Enabled"],["0","Bypassed"]],current.enabled?"1":"0")+'</select></div></div>':
     '<p class="lms-caption">No EQ bands. Add a band to start shaping this output.</p>');
 }else if(dspTab==="level"){
   editor='<h3>Gain / polarity / delay <span>Selected destination only</span></h3>'+
    '<div class="lms-form-grid">'+
    '<div class="lms-field"><label for="lms-gain">Output gain (dB)</label>'+
    '<input class="lms-number" id="lms-gain" data-dsp="gainDb" type="number" min="-60" max="6" step=".1" value="'+p.gainDb+'"/></div>'+
    '<div class="lms-field"><label for="lms-delay">Manual delay (ms)</label>'+
    '<input class="lms-number" id="lms-delay" data-dsp="delayMs" type="number" min="0" max="500" step=".01" value="'+p.delayMs+'"/></div>'+
    '<div class="lms-field"><label for="lms-pol">Polarity</label><select class="lms-select" id="lms-pol" data-dsp="polarity">'+
    selectOptions([["normal","Normal"],["invert","Invert"]],p.polarity)+'</select></div>'+
    '<div class="lms-field"><label for="lms-mute">Output gate (draft)</label><select class="lms-select" id="lms-mute" data-dsp="muted">'+
    selectOptions([["1","Muted"],["0","Unmuted draft"]],p.muted?"1":"0")+'</select></div></div>'+
    '<p class="lms-tip">Need distance-based timing? Use Alignment and copy its calculated offsets to output delay drafts.</p>'+
    btn("Open visual alignment","go:alignment");
 }else{
   editor='<h3>Speaker protection <span>NOT CALIBRATED</span></h3>'+
    '<div class="lms-form-grid">'+
    '<div class="lms-field"><label for="lms-limiter">Limiter intention</label><select class="lms-select" id="lms-limiter" data-dsp="limiterEnabled">'+
    selectOptions([["0","Off / uncalibrated"],["1","On (draft only)"]],p.limiterEnabled?"1":"0")+'</select></div>'+
    '<div class="lms-field"><label for="lms-thr">Digital threshold (dBFS)</label>'+
    '<input class="lms-number" id="lms-thr" data-dsp="limiterThresholdDb" type="number" min="-60" max="0" step=".1" value="'+
    (p.limiterThresholdDb===null?'':p.limiterThresholdDb)+'" placeholder="Unset"/></div></div>'+
    '<p class="lms-warn">Digital dBFS is not speaker power or excursion. Native sample-peak/RMS voltage modelling runs only in isolated diagnostics; measured DAC, amplifier and driver calibration plus physical FAT are still required. This page cannot arm loudspeakers.</p>';
 }
 return '<div class="lms-grid">'+
  '<section class="lms-panel"><h3>Section destinations <span>Hardware port unavailable</span></h3>'+
  outputList()+'</section>'+
  '<section class="lms-panel"><h3>'+safe(selectedPort||"No output")+
  ' <span>'+safe(section?.name||"Not patched")+'</span></h3>'+
  selector+'<div class="lms-line"></div>'+editor+'</section></div>';
}
function alignPage() {
 const all=M.alignment(state);
 const result=all.find(r=>r.port===selectedPort);
 const xSvg=x=>320+x*12,ySvg=y=>70+y*10;
 const lines=all.map(r=>{
   const s=state.positions[r.port],x=xSvg(s.x),y=ySvg(s.y);
   const refX=xSvg(state.listener.x),refY=ySvg(state.listener.y);
   const midX=(x+refX)/2,midY=(y+refY)/2;
   return '<line x1="'+x+'" y1="'+y+'" x2="'+refX+'" y2="'+refY+
     '" stroke="'+(r.port===selectedPort?'#5ad0de':'#406675')+'" stroke-dasharray="5 5"/>'+
     '<text x="'+midX+'" y="'+(midY-6)+'" text-anchor="middle" fill="#a1c2ca">'+unit(r.distanceM)+' m</text>'+
     '<g class="spk" data-speaker="'+safe(r.port)+'"><circle cx="'+x+'" cy="'+y+
     '" r="13" fill="'+(r.port===selectedPort?'#2b91a3':'#285463')+'" stroke="#8cd7e3" stroke-width="2"/>'+
     '<text x="'+x+'" y="'+(y-22)+'" fill="#d8eef4" text-anchor="middle">'+safe(r.port.replace("DEMO-","D"))+'</text></g>';
 }).join("");
 const sx=state.positions[selectedPort]?.x??0,sy=state.positions[selectedPort]?.y??0;
 const table=all.map(r=>'<tr class="'+(r.port===selectedPort?'current':'')+'">'+
  '<td>'+safe(r.port)+'</td><td>'+unit(r.distanceM)+' m</td><td>'+unit(r.additionalMs)+' ms</td>'+
  '<td>'+unit(state.dsp[r.port]?.delayMs??0)+' ms</td></tr>').join("");
 return '<div class="lms-grid"><section class="lms-panel">'+
  '<h3>Distance alignment <span>Top view · metres</span></h3>'+
  '<svg id="lms-room-svg" class="lms-room" viewBox="0 0 640 380" role="img" aria-label="Draggable speaker positions and distances to listening reference">'+
  '<rect x="194" y="14" width="252" height="37" rx="5" fill="#344955"/>'+
  '<text x="320" y="37" fill="#dceef3" text-anchor="middle">STAGE</text>'+
  [100,160,220,280,340].map(y=>'<line x1="30" x2="610" y1="'+y+'" y2="'+y+'" stroke="#2c4651"/>').join('')+
  lines+'<circle class="ref" data-listener-point="1" cx="'+xSvg(state.listener.x)+'" cy="'+ySvg(state.listener.y)+
  '" r="10" fill="#e2e9ef" stroke="#60baca" stroke-width="3"/>'+
  '<text x="'+xSvg(state.listener.x)+'" y="'+(ySvg(state.listener.y)+28)+'" text-anchor="middle" fill="#c6d8de">REFERENCE</text>'+
  '</svg><p class="lms-caption">Drag speakers or the reference marker. This calculates direct-path distances and arrival-time offsets, NOT phase or measured acoustic alignment.</p>'+
  '<div class="lms-line"></div><div class="lms-table-scroll"><table class="lms-mini-table">'+
  '<thead><tr><th>Output</th><th>Distance</th><th>Suggested</th><th>Draft</th></tr></thead>'+
  '<tbody>'+table+'</tbody></table></div></section>'+
  '<section class="lms-panel"><h3>Geometry inspector <span>Not applied to audio</span></h3>'+
  '<label class="lms-label" for="lms-align-port">SPEAKER</label>'+
  '<select class="lms-select" id="lms-align-port" data-align-port>'+
  selectOptions(state.ports.map(p=>[p,p]),selectedPort)+'</select>'+
  '<div class="lms-line"></div><div class="lms-form-grid">'+
  '<div class="lms-field"><label for="lms-sx">X (metres)</label><input id="lms-sx" type="number" step=".1" class="lms-number" data-position="x" value="'+number(sx)+'"/></div>'+
  '<div class="lms-field"><label for="lms-sy">Y (metres)</label><input id="lms-sy" type="number" step=".1" class="lms-number" data-position="y" value="'+number(sy)+'"/></div>'+
  '<div class="lms-field"><label for="lms-lx">Reference X</label><input id="lms-lx" type="number" step=".1" class="lms-number" data-listener="x" value="'+number(state.listener.x)+'"/></div>'+
  '<div class="lms-field"><label for="lms-ly">Reference Y</label><input id="lms-ly" type="number" step=".1" class="lms-number" data-listener="y" value="'+number(state.listener.y)+'"/></div></div>'+
  '<div class="lms-line"></div><div class="lms-stat"><b>'+unit(result?.additionalMs||0)+' ms</b>'+
  '<span>Suggested offset for '+safe(selectedPort)+'</span></div>'+
  '<p class="lms-caption">Farthest speaker receives zero relative delay. Nearer speakers receive an additional delay (343 m/s assumption).</p>'+
  '<div class="lms-line"></div>'+btn("Copy offsets into delay drafts","planDelay")+
  '<p class="lms-warn">No geometry-only estimate compensates crossover phase, horn depth, temperature, device latency or measured impulse response. Draft changes never arm hardware or modify the JUCE callback.</p>'+
  '</section></div>';
}
function reviewBox() {
 if (!pending) return "";
 if(pending.kind==="reassign") {
  const rows=state.ports.map(source=>'<div class="lms-field">'+
    '<label>'+safe(source)+' · '+safe(state.routes[source]||"Unrouted")+'</label>'+
    '<select class="lms-select" data-rebind-source="'+safe(source)+'">'+
    selectOptions([["","Not assigned"],...pending.ports.map(p=>[p,portLabel(p)])],
                  pending.assignments[source]||"")+'</select></div>').join("");
  return '<div class="lms-panel" role="group" aria-label="Review physical output reassignment">'+
    '<strong>Review explicit output reassignment</strong>'+
    '<p class="lms-caption">'+safe(pending.description)+'</p>'+
    '<div class="lms-form-grid">'+rows+'</div>'+
    '<p class="lms-warn">Every routed, tuned or positioned output needs a unique verified port. All reassigned outputs become MUTED. Only the offline design changes; fresh native ACK and subsequent readback are still required.</p>'+
    '<div class="lms-control-row">'+btn("Confirm muted reassignment","confirmReassign")+
    btn("Cancel","cancel")+'</div></div>';
 }
 return '<div class="lms-panel" role="status" style="margin-bottom:12px;border-color:#99794e">'+
 '<strong>Review structural change</strong><p class="lms-caption">'+safe(pending.description)+
 '</p><div class="lms-control-row">'+btn("Confirm, clear routes","confirm")+
 btn("Cancel","cancel")+'</div></div>';
}
function render() {
 const open=!root.hidden;
 if (!open) return;
 root.innerHTML='<aside class="lms-sidebar"><div class="lms-name">LOUDSPEAKERS</div>'+
 '<nav class="lms-nav" aria-label="Speaker management pages">'+pages.map((id,i)=>
 '<button type="button" data-page="'+id+'" class="'+(page===id?'active':'')+'" aria-current="'+
 (page===id?'page':'false')+'"><span class="lms-index">'+String(i+1).padStart(2,"0")+'</span>'+
 pageNames[id]+'</button>').join("")+'</nav>'+
 '<div class="lms-sidebar-foot">ArSonMix LMS<br/>Disconnected speaker design<br/>'+(nativeConnected?'Native validation only':'No native PCM ACK')+'</div></aside>'+
 '<div class="lms-stage"><div class="lms-heading"><div><h2>'+pageNames[page]+
 '</h2><small>'+safe(state.name)+' · '+(state.stereo?"Stereo":"Mono")+
 ' · '+state.ways+' Way · revision '+state.revision+'</small></div>'+
 '<div class="lms-offline">DESIGN ONLY · OUTPUT DISCONNECTED</div></div>'+
 '<div class="lms-content">'+nativeBar()+reviewBox()+
 (message?'<p role="alert" class="lms-warn">'+safe(message)+'</p>':'')+
 ({system:systemPage,crossover:frequencyPage,matrix:matrixPage,
 processing:dspPage,alignment:alignPage}[page]())+'</div></div>';
}
function update(next) {state=next;message="";pending=null;validatedForRevision=-1;nativeStatus=nativeConnected?"Draft edited · native validation required":nativeStatus;if(!state.ports.includes(selectedPort)){selectedPort=state.ports[0]||null;selectedBand=0;}render();}
function openLms(open) {
 root.hidden=!open;app.classList.toggle("lms-active",open);
 launch.setAttribute("aria-pressed",String(open));
 launch.textContent=open?"Back to Mixer":"Speakers";
 if (open){render();inspectNative();}
}
launch.addEventListener("click",()=>openLms(root.hidden));
root.addEventListener("click",event=>{
 const t=event.target.closest("button");if(!t)return;
 const id=t.dataset.page;
 if(id){page=id;pending=null;message="";render();return;}
 const act=t.dataset.action;
 if(act?.startsWith("go:")){page=act.slice(3);pending=null;render();return;}
 if(act==="cancel"){pending=null;render();return;}
 if(act==="nativeInspect"){inspectNative();return;}
 if(act==="nativePorts"){
  const actual=nativeAvailable().map(p=>p.identity);
  if(!actual.length||actual.length>64)return;
  pending={description:"Replace DEMO ports with the CURRENT verified ASIO output mask? This intentionally clears all patches and DSP settings; it does NOT route or arm speakers.",
    run:()=>M.withPorts(state,actual)};
  render();return;
 }
 if(act==="nativeValidate"){validateNativeDraft();return;}
 if(act==="nativeReassign"){
  if(!nativeInventory?.ready||!nativeInventory.outputDisarmed||
     nativePending||!Object.keys(state.routes).length)return;
  const ports=nativeAvailable().map(p=>p.identity);
  if(!ports.length||ports.length>64)return;
  pending={kind:"reassign",revision:state.revision,ports,
    deviceEpoch:nativeInventory.deviceEpoch,
    graphRevision:nativeInventory.graphRevision,assignments:{},
    description:"Choose exact ASIO destinations. No automatic ordinal mapping. Routing, DSP and alignment are preserved only for explicitly selected outputs."};
  message="";render();return;
 }
 if(act==="confirmReassign"){
  if(pending?.kind!=="reassign"||pending.revision!==state.revision||
     !nativeInventory?.ready||!nativeInventory.outputDisarmed||
     nativeInventory.deviceEpoch!==pending.deviceEpoch||
     nativeInventory.graphRevision!==pending.graphRevision||
     JSON.stringify(nativeAvailable().map(p=>p.identity))!==
       JSON.stringify(pending.ports)){
    pending=null;message="ASIO device inventory changed · inspect again";
    validatedForRevision=-1;render();return;
  }
  try{
   const next=M.reassignVerifiedPorts(state,pending.ports,pending.assignments);
   update(next);
   nativeStatus="Explicit ASIO reassignment saved as MUTED draft · native validation required";
  }catch(e){message=e.message;render();}
  return;
 }
 if(act==="planDelay"){
  pending={description:"Copy calculated geometric delays to all output DSP DRAFTS. No audio, device, native PCM ACK, or hardware output will be changed.",
    run:()=>M.withSuggestedDelays(state)};
  render();return;
 }
 if(act==="addPeq"){
  try{selectedBand=state.dsp[selectedPort].peq.length;
      update(M.withPeqBand(state,selectedPort,selectedBand,{}));}
  catch(e){message=e.message;render();}
  return;
 }
 if(act==="removePeq"){
  try{const next=M.withoutPeqBand(state,selectedPort,selectedBand);
      selectedBand=Math.max(0,selectedBand-1);update(next);}
  catch(e){message=e.message;render();}
  return;
 }
 if(t.dataset.dspTab){dspTab=t.dataset.dspTab;render();return;}
 if(t.dataset.selectBand!==undefined){selectedBand=Number(t.dataset.selectBand);render();return;}

 if(act==="confirm"&&pending){const p=pending;update(p.run());return;}
 if(t.dataset.routePort){
  try{update(M.withRoute(state,t.dataset.routePort,t.dataset.routeSection));}
  catch(e){message=e.message;render();}
  return;
 }
 if(t.dataset.selectPort){selectedPort=t.dataset.selectPort;selectedBand=0;render();}
});
root.addEventListener("change",event=>{
 const t=event.target;
 try {
  if(t.dataset.rebindSource!==undefined){
   if(pending?.kind!=="reassign")return;
   const source=t.dataset.rebindSource;
   if(!state.ports.includes(source))return;
   if(t.value)pending.assignments[source]=t.value;
   else delete pending.assignments[source];
   message="";render();return;
  }
  if(t.dataset.ways!==undefined){
   const ways=Number(t.value);
   if(ways!==state.ways) pending={description:"Changing the number of ways clears ALL crosspoints and resets output DSP drafts. The current mixer/audio remains untouched.",run:()=>M.withWays(state,ways)};
   render();return;
  }
  if(t.dataset.stereo!==undefined){
   const stereo=t.value==="1";
   if(stereo!==state.stereo)pending={description:"Changing mono/stereo layout clears ALL patches and resets output DSP drafts.",run:()=>M.withStereo(state,stereo)};
   render();return;
  }
  if(t.dataset.ports!==undefined){
   const n=Number(t.value);
   if(n!==state.ports.length)pending={description:"Changing the demo port roster clears ALL patches. Real ASIO pins are never guessed.",run:()=>M.withPorts(state,M.demoPorts(n))};
   render();return;
  }
  if(t.dataset.cutoff!==undefined){update(M.withCutoff(state,Number(t.dataset.cutoff),Number(t.value)));return;}
  if(t.dataset.selectPort!==undefined||t.dataset.alignPort!==undefined){selectedPort=t.value;selectedBand=0;render();return;}
  if(t.dataset.peqField!==undefined){
   const field=t.dataset.peqField;
   const value=field==="enabled"?t.value==="1":Number(t.value);
   update(M.withPeqBand(state,selectedPort,selectedBand,{[field]:value}));
   return;
  }
  if(t.dataset.dsp!==undefined){
   const field=t.dataset.dsp;
   const v=["muted","limiterEnabled"].includes(field)?t.value==="1":
    field==="limiterThresholdDb"?(t.value===""?null:Number(t.value)):
    ["delayMs","gainDb"].includes(field)?Number(t.value):t.value;
   update(M.withDsp(state,selectedPort,{[field]:v}));return;
  }
  if(t.dataset.position!==undefined){
   const pos=state.positions[selectedPort];
   update(M.withPosition(state,selectedPort,
    t.dataset.position==="x"?Number(t.value):pos.x,
    t.dataset.position==="y"?Number(t.value):pos.y));return;
  }
  if(t.dataset.listener!==undefined){
   update(M.withListener(state,
    t.dataset.listener==="x"?Number(t.value):state.listener.x,
    t.dataset.listener==="y"?Number(t.value):state.listener.y));return;
  }
 }catch(e){message=e.message;render();}
});
function point(svg,e) {
 const r=svg.getBoundingClientRect();
 return {x:(e.clientX-r.left)*640/r.width,y:(e.clientY-r.top)*380/r.height};
}
root.addEventListener("pointerdown",e=>{
 const t=e.target.closest("[data-speaker],[data-listener-point]");
 if(!t||e.button!==0)return;
 const svg=t.closest("svg");if(!svg)return;
 dragging={svg,id:e.pointerId,port:t.dataset.speaker||null};
 svg.setPointerCapture(e.pointerId);
 e.preventDefault();
});
root.addEventListener("pointerup",e=>{
 if(!dragging||dragging.id!==e.pointerId)return;
 const drag=dragging;dragging=null;
 const q=point(drag.svg,e),x=Math.round(((q.x-320)/12)*10)/10,
 y=Math.round(((q.y-70)/10)*10)/10;
 try{
  if(drag.port){selectedPort=drag.port;update(M.withPosition(state,drag.port,x,y));}
  else update(M.withListener(state,x,y));
 }catch(err){message=err.message;render();}
});
root.addEventListener("pointercancel",()=>{dragging=null;});
window.addEventListener("keydown",e=>{
 if(e.key==="Escape"&&!root.hidden&&
 !e.target.closest("input,select,textarea"))openLms(false);
});
launch.setAttribute("aria-pressed","false");
})();
