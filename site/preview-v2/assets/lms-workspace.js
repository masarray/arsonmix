/* ArSonMix LMS workspace: GUI-only offline draft.
 * Native publication is deliberately NOT wired; all output ports are DEMO
 * identities and every output DSP begins muted with limiter uncalibrated.
 */
(function () {
"use strict";
const M=window.ArSonMixLmsModel, root=document.getElementById("lmsWorkspace");
const app=document.querySelector(".app"), launch=document.getElementById("lmsOpen");
if (!M||!root||!app||!launch||root.dataset.ready) return;
root.dataset.ready="1";
let state=M.createSystem({ways:3,stereo:true,ports:M.demoPorts(6)});
let page="system",selectedPort=state.ports[0],pending=null,message="";
let dragging=null;
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
 '<div class="lms-stat"><b>'+state.ports.length+'</b><span>Demo output ports</span></div></div>';
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
 const edges=[20].concat(state.cutoffsHz,[20000]);
 let curves="",rows="";
 for (let i=0;i<state.ways;i++) {
  const x1=freqX(edges[i]),x2=freqX(edges[i+1]),c=color(i);
  curves+='<rect x="'+x1.toFixed(1)+'" y="30" width="'+(x2-x1).toFixed(1)+'" height="166" fill="'+c+'" fill-opacity=".19"/>'+
   '<line x1="'+x1.toFixed(1)+'" y1="30" x2="'+x1.toFixed(1)+'" y2="196" stroke="'+c+'" stroke-opacity=".9"/>';
  rows+='<div class="lms-band-row"><span class="lms-band-mark" style="background:'+c+'"></span>'+
   '<span class="lms-band-name">'+safe(state.sections[i].band)+'</span><span class="lms-muted">'+
   safe(Math.round(edges[i])+'–'+Math.round(edges[i+1])+' Hz')+'</span></div>';
 }
 const splits=state.cutoffsHz.map((v,i)=>'<div class="lms-split-row"><label for="lms-x'+i+'">Split '+(i+1)+'</label>'+
 '<input id="lms-x'+i+'" class="lms-number" type="number" min="20" max="20000" step="1" value="'+v+
 '" data-cutoff="'+i+'"/> <span class="lms-muted">Hz</span></div>').join("");
 return '<div class="lms-grid"><section class="lms-panel"><h3>Crossover sections <span>Design frequency map · not acoustic measurement</span></h3>'+
 '<svg class="lms-frequency" viewBox="0 0 700 242" role="img" aria-label="Frequency bands from 20 Hz to 20 kilohertz">'+
 '<rect x="24" y="30" width="650" height="166" fill="#0b1720"/>'+
 [50,100,500,1000,5000,10000].map(h=>'<line x1="'+freqX(h)+'" x2="'+freqX(h)+'" y1="30" y2="196" stroke="#405461" stroke-opacity=".5"/>').join("")+
 curves+'<line x1="24" x2="674" y1="196" y2="196" stroke="#6b8897"/>'+
 [20,100,1000,10000,20000].map(h=>'<text x="'+freqX(h)+'" y="218" fill="#9db8c1" font-size="11" text-anchor="middle">'+(h===20000?'20k':h===10000?'10k':h===1000?'1k':h)+'</text>').join("")+
 '</svg><div class="lms-line"></div><div class="lms-control-row">'+
 '<label class="lms-label" for="lms-way">NUMBER OF WAYS</label>'+
 '<select class="lms-select" id="lms-way" data-ways>'+selectOptions(Array.from({length:16},(_,i)=>[String(i+1),(i+1)+" Way"]),state.ways)+'</select>'+
 '<label class="lms-label" for="lms-stereo">FORMAT</label>'+
 '<select class="lms-select" id="lms-stereo" data-stereo>'+selectOptions([["1","Stereo L/R"],["0","Mono"]],state.stereo?"1":"0")+'</select>'+
 '</div><p class="lms-warn">Changing the number of ways or mono/stereo layout clears all crosspoints and resets output protection drafts. This never changes the real PCM graph.</p></section>'+
 '<section class="lms-panel"><h3>Band boundaries <span>Linked across '+(state.stereo?"L/R":"mono")+'</span></h3>'+
 rows+'<div class="lms-line"></div>'+splits+
 '<p class="lms-tip">These are editable design boundaries, not validated loudspeaker presets. Native crossover filters and measured response are not yet active.</p></section></div>';
}
function matrixPage() {
 const headers=state.ports.map(p=>'<th scope="col">'+safe(p.replace("DEMO-","D"))+'</th>').join("");
 const rows=state.sections.map(s=>'<tr><th scope="row">'+safe(s.name)+'</th>'+
 state.ports.map(p=>'<td><button type="button" class="lms-cross'+(state.routes[p]===s.id?' active':'')+
 '" data-route-port="'+safe(p)+'" data-route-section="'+safe(s.id)+'" aria-pressed="'+
 (state.routes[p]===s.id?"true":"false")+'" aria-label="Assign '+safe(s.name)+' to '+safe(p)+'">'+
 (state.routes[p]===s.id?'✓':'·')+'</button></td>').join("")+'</tr>').join("");
 return '<section class="lms-panel"><h3>Section → Output Matrix <span>Exclusive destination · source fan-out allowed</span></h3>'+
 '<div class="lms-control-row"><label class="lms-label" for="lms-count">DEMO PORTS</label>'+
 '<select id="lms-count" data-ports class="lms-select">'+
 selectOptions([2,4,6,8,12,16,24,32].map(n=>[String(n),n+" ports"]),state.ports.length)+
 '</select><span class="lms-caption">Hardware pin identity will come from ASIO discovery; these IDs are placeholders.</span></div>'+
 '<div class="lms-line"></div><div class="lms-table-scroll" style="max-height:430px"><table class="lms-matrix"><thead><tr><th scope="col">CROSSOVER</th>'+
 headers+'</tr></thead><tbody>'+rows+'</tbody></table></div>'+
 '<p class="lms-tip">Click a crosspoint to assign/unassign that section. A destination can have only one section; several destinations may share a source. A different hardware device must be explicitly mapped later.</p>'+
 '</section>';
}
function outputList() {
 return '<div class="lms-output-list">'+state.ports.map(p=>
  '<button type="button" class="lms-output'+(selectedPort===p?' active':'')+'" data-select-port="'+safe(p)+'">'+
  '<strong>'+safe(p)+'</strong><small>'+safe(state.sections.find(s=>s.id===state.routes[p])?.name||"Unassigned")+'</small>'+
  '</button>').join("")+'</div>';
}
function dspPage() {
 const p=state.dsp[selectedPort]||{},section=state.sections.find(s=>s.id===state.routes[selectedPort]);
 return '<div class="lms-grid"><section class="lms-panel"><h3>Logical outputs <span>Select target</span></h3>'+
 outputList()+'</section><section class="lms-panel"><h3>'+safe(selectedPort||"No output")+
 ' <span>'+safe(section?.name||"Unassigned")+'</span></h3>'+
 '<div class="lms-form-grid">'+
 '<div class="lms-field"><label for="lms-gain">Output gain (dB)</label>'+
 '<input class="lms-number" id="lms-gain" data-dsp="gainDb" type="number" min="-60" max="6" step=".1" value="'+p.gainDb+'"/></div>'+
 '<div class="lms-field"><label for="lms-delay">Manual delay (ms)</label>'+
 '<input class="lms-number" id="lms-delay" data-dsp="delayMs" type="number" min="0" max="500" step=".01" value="'+p.delayMs+'"/></div>'+
 '<div class="lms-field"><label for="lms-pol">Polarity</label><select class="lms-select" id="lms-pol" data-dsp="polarity">'+
 selectOptions([["normal","Normal"],["invert","Invert"]],p.polarity)+'</select></div>'+
 '<div class="lms-field"><label for="lms-mute">Output gate (draft only)</label><select class="lms-select" id="lms-mute" data-dsp="muted">'+
 selectOptions([["1","Muted"],["0","Unmuted draft"]],p.muted?"1":"0")+'</select></div>'+
 '<div class="lms-field"><label for="lms-limiter">Limiter state</label><select class="lms-select" id="lms-limiter" data-dsp="limiterEnabled">'+
 selectOptions([["0","Off / uncalibrated"],["1","On (draft only)"]],p.limiterEnabled?"1":"0")+'</select></div>'+
 '<div class="lms-field"><label for="lms-thr">Limiter threshold (dBFS)</label>'+
 '<input class="lms-number" id="lms-thr" data-dsp="limiterThresholdDb" type="number" min="-60" max="0" step=".1" value="'+
 (p.limiterThresholdDb===null?"":p.limiterThresholdDb)+'" placeholder="Unset"/></div></div>'+
 '<div class="lms-line"></div><p class="lms-warn">Protection values require speaker, amplifier, impedance and gain calibration. An uncalibrated limiter must NEVER be treated as safe for hardware playback. No DSP changes are sent to JUCE.</p>'+
 '</section></div>';
}
function alignPage() {
 const all=M.alignment(state);
 const result=all.find(r=>r.port===selectedPort);
 const xSvg=x=>320+x*12,ySvg=y=>70+y*10;
 const lines=all.map(r=>{
  const s=state.positions[r.port],x=xSvg(s.x),y=ySvg(s.y);
  return '<line x1="'+x+'" y1="'+y+'" x2="'+xSvg(state.listener.x)+'" y2="'+ySvg(state.listener.y)+
   '" stroke="'+(r.port===selectedPort?'#5ad0de':'#406675')+'" stroke-dasharray="5 5"/>'+
   '<g class="spk" data-speaker="'+safe(r.port)+'"><circle cx="'+x+'" cy="'+y+
   '" r="13" fill="'+(r.port===selectedPort?'#2b91a3':'#285463')+'" stroke="#8cd7e3" stroke-width="2"/>'+
   '<text x="'+x+'" y="'+(y-22)+'" fill="#d8eef4" text-anchor="middle">'+safe(r.port.replace("DEMO-","D"))+'</text></g>';
 }).join("");
 const sx=state.positions[selectedPort]?.x??0,sy=state.positions[selectedPort]?.y??0;
 return '<div class="lms-grid"><section class="lms-panel"><h3>Speaker placement <span>Top view · metres</span></h3>'+
 '<svg id="lms-room-svg" class="lms-room" viewBox="0 0 640 380" role="img" aria-label="Draggable speaker layout">'+
 '<rect x="194" y="14" width="252" height="37" rx="5" fill="#344955"/>'+
 '<text x="320" y="37" fill="#dceef3" text-anchor="middle">STAGE</text>'+
 [100,160,220,280,340].map(y=>'<line x1="30" x2="610" y1="'+y+'" y2="'+y+'" stroke="#2c4651"/>').join("")+
 lines+'<circle class="ref" data-listener-point="1" cx="'+xSvg(state.listener.x)+'" cy="'+ySvg(state.listener.y)+
 '" r="10" fill="#e2e9ef" stroke="#60baca" stroke-width="3"/>'+
 '<text x="'+xSvg(state.listener.x)+'" y="'+(ySvg(state.listener.y)+28)+'" text-anchor="middle" fill="#c6d8de">REFERENCE</text>'+
 '</svg><p class="lms-caption">Drag a speaker or the reference marker. This estimates geometric arrival-time differences only, not phase alignment.</p></section>'+
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
 '<span>Extra geometric delay to '+safe(selectedPort)+'</span></div>'+
 '<p class="lms-warn">Delay assumes 343 m/s and one reference point. Driver acoustic centres, all-pass, crossover phase and real measurements remain outside this estimate.</p>'+
 '</section></div>';
}
function reviewBox() {
 if (!pending) return "";
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
 '<div class="lms-sidebar-foot">ArSonMix LMS<br/>Offline speaker design<br/>No native PCM ACK</div></aside>'+
 '<div class="lms-stage"><div class="lms-heading"><div><h2>'+pageNames[page]+
 '</h2><small>'+safe(state.name)+' · '+(state.stereo?"Stereo":"Mono")+
 ' · '+state.ways+' Way · revision '+state.revision+'</small></div>'+
 '<div class="lms-offline">DESIGN ONLY · OUTPUT DISCONNECTED</div></div>'+
 '<div class="lms-content">'+reviewBox()+
 (message?'<p role="alert" class="lms-warn">'+safe(message)+'</p>':'')+
 ({system:systemPage,crossover:frequencyPage,matrix:matrixPage,
 processing:dspPage,alignment:alignPage}[page]())+'</div></div>';
}
function update(next) {state=next;message="";pending=null;render();}
function openLms(open) {
 root.hidden=!open;app.classList.toggle("lms-active",open);
 launch.setAttribute("aria-pressed",String(open));
 launch.textContent=open?"Back to Mixer":"Speakers";
 if (open) render();
}
launch.addEventListener("click",()=>openLms(root.hidden));
root.addEventListener("click",event=>{
 const t=event.target.closest("button");if(!t)return;
 const id=t.dataset.page;
 if(id){page=id;pending=null;message="";render();return;}
 const act=t.dataset.action;
 if(act?.startsWith("go:")){page=act.slice(3);pending=null;render();return;}
 if(act==="cancel"){pending=null;render();return;}
 if(act==="confirm"&&pending){const p=pending;update(p.run());return;}
 if(t.dataset.routePort){
  try{update(M.withRoute(state,t.dataset.routePort,t.dataset.routeSection));}
  catch(e){message=e.message;render();}
  return;
 }
 if(t.dataset.selectPort){selectedPort=t.dataset.selectPort;render();}
});
root.addEventListener("change",event=>{
 const t=event.target;
 try {
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
  if(t.dataset.selectPort!==undefined||t.dataset.alignPort!==undefined){selectedPort=t.value;render();return;}
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
