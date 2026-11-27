/* Native Auto Ducker editor: no browser DSP, accepted instance identity only. */
(()=>{
"use strict";
const ID="armix.builtin.auto-ducker.v1",api=()=>window.__ARMIX_NATIVE_M2__,backend=()=>window.__JUCE__?.backend,view=()=>api()?.fxEditorState?.()||api()?.state?.()?.current;
const defs={thresholdDb:["Threshold",-72,0,.5,"dBFS"],depthDb:["Depth",0,36,.5,"dB"],attackMs:["Attack",2,250,1,"ms"],holdMs:["Hold",0,2000,10,"ms"],releaseMs:["Release",20,3000,10,"ms"],hysteresisDb:["Hysteresis",1,18,.5,"dB"],highPassHz:["High pass",30,400,10,"Hz"],lowPassHz:["Low pass",1000,12000,100,"Hz"]};
const radio={thresholdDb:-34,depthDb:12,attackMs:18,holdMs:220,releaseMs:480,hysteresisDb:5,highPassHz:120,lowPassHz:5000};
const presets={Radio:radio,Gentle:{...radio,depthDb:6,attackMs:45,holdMs:300,releaseMs:750},Announcement:{...radio,depthDb:20,attackMs:8,holdMs:350,releaseMs:650}};
const dom=(tag,cls,txt)=>{const e=document.createElement(tag);if(cls)e.className=cls;if(txt!==undefined)e.textContent=txt;return e;};
const button=(txt,fn)=>{const e=dom("button","",txt);e.type="button";e.addEventListener("click",fn);return e;};
const style=dom("style");style.textContent=`
#mxNativeDucker{position:fixed;z-index:3500;width:min(720px,calc(100vw - 12px));max-height:calc(100vh - 12px);overflow:auto;color:#d8e0e3;background:#172026;border:1px solid #47565d;border-radius:8px;box-shadow:0 12px 36px #0009;font:12px/1.4 system-ui,sans-serif;text-align:left;box-sizing:border-box}
#mxNativeDucker[hidden],#mxNativeDucker [hidden]{display:none!important}
#mxNativeDucker *{box-sizing:border-box}#mxNativeDucker button:not(.armix-builtin-fx-power):not(.armix-builtin-fx-close),#mxNativeDucker select,#mxNativeDucker input[type=number],#mxNativeDucker input[type=search]{color:inherit;background:#253138;border:1px solid #45545b;border-radius:4px;font:inherit;min-height:30px;padding:4px 8px}#mxNativeDucker button{cursor:pointer}#mxNativeDucker button:disabled,#mxNativeDucker input:disabled{opacity:.45;cursor:default}#mxNativeDucker :focus-visible{outline:2px solid #59d2c3;outline-offset:2px}
.mx-ducker-head{display:flex;align-items:center;gap:8px;padding:8px 12px;background:#202b31;cursor:move;position:sticky;top:0;z-index:2}.mx-ducker-head strong{font-size:15px;font-weight:600;white-space:nowrap}.mx-ducker-channel{color:#9caeb7;flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.mx-ducker-head button[aria-pressed=true]{color:#6be2ca;border-color:#59b6a5}
.mx-ducker-content{padding:12px;display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr);gap:12px}.mx-ducker-box{background:#11191e;border:1px solid #34434a;border-radius:5px;padding:10px;min-width:0}.mx-ducker-title{display:flex;align-items:center;justify-content:space-between;gap:8px;margin-bottom:8px;font-size:11px;letter-spacing:.05em;color:#a8b8c0}.mx-ducker-source{display:grid;grid-template-columns:minmax(0,1fr) 84px 66px;align-items:center;gap:8px;padding:5px 0;border-bottom:1px solid #26343a}.mx-ducker-source span:first-child{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.mx-ducker-list{max-height:160px;overflow:auto}.mx-ducker-rail{height:6px;background:#28363c;border-radius:2px;overflow:hidden}.mx-ducker-fill{height:100%;width:0;background:#58cbb7;transition:width 100ms linear}.mx-ducker-read{font-variant-numeric:tabular-nums;text-align:right;color:#a8bbc4;font-size:11px}.mx-ducker-gr{font-size:27px;font-weight:500;color:#6bdac9;font-variant-numeric:tabular-nums}.mx-ducker-ticks{display:flex;justify-content:space-between;color:#91a4ae;font-size:10px}.mx-ducker-controls{display:grid;grid-template-columns:1fr 1fr;gap:9px}.mx-ducker-timing{grid-column:1/-1;display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px}.mx-ducker-control{display:grid;grid-template-columns:minmax(0,1fr) 63px;align-items:center;gap:4px}.mx-ducker-control input[type=range]{grid-column:1/-1;width:100%;accent-color:#59c9b7;margin:3px 0}.mx-ducker-control input[type=number]{width:63px;text-align:right;padding:3px}.mx-ducker-advanced{grid-column:1/-1}.mx-ducker-advanced summary{cursor:pointer;color:#aebfc6}.mx-ducker-advanced .mx-ducker-timing{margin-top:10px}.mx-ducker-note{color:#a5b5bd;font-size:11px}.mx-ducker-status{padding:6px 12px 10px;color:#d6bd84;min-height:28px}.mx-ducker-picker{grid-column:1/-1}.mx-ducker-picker input[type=search]{width:100%;margin-bottom:6px}.mx-ducker-options{max-height:172px;overflow:auto}.mx-ducker-option{display:flex;align-items:center;gap:8px;min-height:30px;border-bottom:1px solid #29373e}.mx-ducker-option input{accent-color:#59c9b7}.mx-ducker-option small{margin-left:auto;color:#a5b5bd}.mx-ducker-actions{display:flex;gap:8px;align-items:center;justify-content:flex-end;margin-top:8px}.mx-ducker-actions output{margin-right:auto;color:#a6b8bf}.mx-ducker-apply{border-color:#59b6a5!important;color:#6be2ca!important}
@media(max-width:490px){.mx-ducker-content{grid-template-columns:1fr}.mx-ducker-timing{gap:5px}.mx-ducker-head{flex-wrap:wrap}.mx-ducker-channel{min-width:80px}}

#mxNativeDucker{left:6px;top:6px;width:min(720px,calc(100vw - 12px));background:linear-gradient(180deg,#556671,#34454f 45%,#2b3942);border:1px solid #778a94;border-radius:7px;box-shadow:0 13px 30px #0009,inset 0 1px #ffffff24;font:12px/1.4 system-ui,sans-serif;color:#d9e4e8}
.mx-ducker-head{height:39px;padding:5px 10px;gap:9px;background:linear-gradient(#303d46,#1d2931);border-bottom:1px solid #101c22;box-shadow:0 1px #b5c4c32a}
.mx-ducker-head strong{font-size:14px;font-weight:600;letter-spacing:.025em}.mx-ducker-brand{color:#a5bac3;font-size:10px;letter-spacing:.10em;font-weight:500}
#mxNativeDucker button:not(.armix-builtin-fx-power):not(.armix-builtin-fx-close),#mxNativeDucker select{min-height:25px;background:linear-gradient(#41525d,#25343e);border-color:#647985;border-radius:3px;box-shadow:inset 0 1px #fff1,0 1px 2px #0006;padding:3px 9px}

.mx-ducker-content{padding:10px;gap:8px;grid-template-columns:minmax(0,1.13fr) minmax(0,1fr)}
.mx-ducker-box{border:1px solid #70848f;border-radius:4px;box-shadow:inset 0 1px #ffffff15,0 1px 3px #0004;padding:9px;background:linear-gradient(#4f626d,#42545f)}
.mx-ducker-content>.mx-ducker-box:nth-child(-n+2){background:linear-gradient(145deg,#10242b,#0c171d);border:1px solid #14232c;box-shadow:inset 0 2px 5px #000a,0 1px #9cb0b777;min-height:132px}
.mx-ducker-title{font-size:10px;letter-spacing:.09em;color:#a9c7cd;margin-bottom:7px}
.mx-ducker-source{grid-template-columns:minmax(0,1fr) 86px 53px;gap:7px;padding:6px 0;border-color:#294049;font-size:11px}
.mx-ducker-source span:first-child:before{content:"";display:inline-block;width:6px;height:6px;border-radius:50%;background:#324b51;margin-right:7px;box-shadow:inset 0 1px 2px #000}
.mx-ducker-source[data-active=true] span:first-child:before{background:#60e9bb;box-shadow:0 0 6px #60e9bb88}
.mx-ducker-list{max-height:85px;min-height:53px}.mx-ducker-read{color:#8cd8c4;font:10px ui-monospace,monospace}
.mx-ducker-rail{height:9px;border:1px solid #1f363d;border-radius:1px;background:#15292f;box-shadow:inset 0 1px 3px #0007}
.mx-ducker-fill{background:repeating-linear-gradient(90deg,#62dcb3 0 5px,#132b2c 5px 7px);border-radius:0}
.mx-ducker-gr{display:block;color:#a1f2d8;font:29px/1.3 ui-monospace,monospace;letter-spacing:-.05em;margin:0 0 5px}
.mx-ducker-content>.mx-ducker-box:nth-child(2) .mx-ducker-rail{height:15px}
.mx-ducker-content>.mx-ducker-box:nth-child(2) .mx-ducker-fill{background:repeating-linear-gradient(90deg,#e4c184 0 7px,#332e25 7px 9px)}
.mx-ducker-phase{font:10px ui-monospace,monospace;color:#a1d7c6;text-transform:uppercase}
.mx-ducker-ticks{font:9px ui-monospace,monospace;margin-top:4px;color:#829fa8}
.mx-ducker-lcd-note{font:10px ui-monospace,monospace;color:#8eafb9;margin-top:7px}
.mx-ducker-timing{grid-template-columns:repeat(5,minmax(0,1fr));gap:4px;padding:10px 6px}
.mx-ducker-control{position:relative;display:grid;grid-template-columns:1fr;grid-template-rows:15px 67px 24px;justify-items:center;gap:3px;min-width:0;border-right:1px solid #25394680}
.mx-ducker-control:last-child{border-right:none}.mx-ducker-control>span:first-child{font-size:10px;letter-spacing:.07em;color:#e0e9eb;text-transform:uppercase;white-space:nowrap}
.mx-ducker-dial{position:relative;width:65px;height:65px;display:block;pointer-events:none;border-radius:50%;grid-row:2}
.mx-ducker-body,.mx-ducker-pointer{position:absolute;inset:11%;border-radius:50%;background-size:100% 100%;background-position:center;background-repeat:no-repeat}
.mx-ducker-body{background-image:var(--ducker-body);box-shadow:0 3px 5px #0008}
.mx-ducker-pointer{background-image:var(--ducker-pointer);transform:rotate(var(--angle,-135deg));transform-origin:50% 50%}
.mx-ducker-dial svg{position:absolute;inset:0;width:100%;height:100%;overflow:visible;pointer-events:none}
.mx-ducker-arc-track,.mx-ducker-arc-progress{fill:none;stroke-linecap:round;stroke-width:4}.mx-ducker-arc-track{stroke:#20383e}.mx-ducker-arc-progress{stroke:#6ce7c5}
#mxNativeDucker .mx-ducker-control input[type=range]{position:absolute;top:18px;left:50%;transform:translateX(-50%);width:65px;height:65px;opacity:0;margin:0;cursor:ns-resize;touch-action:none;z-index:1}
#mxNativeDucker .mx-ducker-control input[type=number]{grid-row:3;width:73px;min-height:23px;background:#20323b;color:#c9efe7;border:1px solid #718894;box-shadow:inset 0 1px 3px #0008;font:11px ui-monospace,monospace;text-align:center;padding:2px 4px}
.mx-ducker-control:focus-within .mx-ducker-dial{outline:2px solid #8de4d2;outline-offset:1px}
.mx-ducker-advanced{padding:7px 10px}.mx-ducker-advanced summary{font-size:11px;color:#d5e3e6}.mx-ducker-advanced .mx-ducker-timing{grid-template-columns:repeat(3,minmax(0,1fr));padding:4px}
.mx-ducker-note{font-size:10px;color:#9db7bf}.mx-ducker-advanced .mx-ducker-note{color:#d0dde2}.mx-ducker-status{min-height:23px;padding:2px 10px 6px;font-size:10px;color:#e6d8af}
.mx-ducker-picker{background:linear-gradient(#263b45,#1d2c34)}.mx-ducker-picker .mx-ducker-title{color:#d4e6eb}.mx-ducker-option{border-color:#405761}
@media(max-width:490px){.mx-ducker-content{grid-template-columns:1fr}.mx-ducker-timing{grid-template-columns:repeat(3,minmax(0,1fr))}.mx-ducker-head{height:auto}.mx-ducker-brand{display:none}}

.mx-ducker-source{grid-template-columns:minmax(60px,1fr) minmax(105px,145px) 53px}
#mxNativeDucker .mx-ducker-rail{height:auto;position:relative;padding:2px;border:1px solid #42616c;border-radius:3px;background:#09171d;box-shadow:inset 0 1px 4px #000b}
.mx-ducker-led-bed{position:relative;overflow:hidden;background:#182831;aspect-ratio:var(--bed-ratio,30)}
#mxNativeDucker .mx-ducker-fill{position:absolute;inset:0 auto 0 0;width:0;height:100%;background:linear-gradient(90deg,#0c9f50,#70de30 78%,#f8c136 92%,#ec4540);background-size:var(--full-width,140px) 100%;transition:none}
#mxNativeDucker .mx-ducker-rail[data-kind=gr] .mx-ducker-fill{background:linear-gradient(#ffde78,#be7b10)}
.mx-ducker-led-grid{position:absolute;inset:0;display:grid;grid-template-columns:repeat(var(--cells),minmax(0,1fr));pointer-events:none}
.mx-ducker-led-cell{min-width:0;border:1px solid #071015;box-shadow:inset 0 1px #e8f6f54a,inset 0 -1px #0008;background:linear-gradient(#ffffff22,#ffffff04 48%,#00000044)}
#mxNativeDucker .mx-ducker-control input[type=number],#mxNativeDucker .mx-ducker-gr{background:linear-gradient(145deg,#10242b,#0c171d);border:1px solid #14232c;border-radius:3px;box-shadow:inset 0 2px 5px #000a,0 1px #9cb0b777;color:#a1f2d8}
#mxNativeDucker .mx-ducker-gr{padding:2px 8px;width:max-content;min-width:110px}
.mx-ducker-body{background-image:radial-gradient(circle at 40% 30%,#ffffff18,transparent 50%),conic-gradient(from 25deg,#536169,#202b30,#626d74,#242d32,#536169),var(--ducker-body);background-blend-mode:screen,multiply,normal;border:1px solid #a0aeb2;box-shadow:0 3px 5px #0008,inset 0 0 0 3px #111b20,inset 0 0 0 5px #7d8e9666}
`;document.head.append(style);
const panel=dom("section");panel.id="mxNativeDucker";panel.hidden=true;panel.setAttribute("role","dialog");panel.setAttribute("aria-label","Auto Ducker");
const assetRoot=new URL(".",document.currentScript.src);
panel.style.setProperty("--ducker-body",`url("${new URL("compressor-knob-plastic-body.svg",assetRoot)}")`);
panel.style.setProperty("--ducker-pointer",`url("${new URL("compressor-knob-plastic-pointer.svg",assetRoot)}")`);
const head=dom("header","mx-ducker-head"),target=dom("span","mx-ducker-channel"),preset=dom("select");preset.setAttribute("aria-label","Ducking preset");
for(const name of ["Custom",...Object.keys(presets)]){const op=dom("option","",name);op.value=name;preset.append(op);}
const power=button("⏻",()=>{if(current&&context())api()?.consumeAction?.(current.track,"slotPower",{dataset:{slot:String(current.slot)}});});power.classList.add("armix-builtin-fx-power");power.setAttribute("aria-label","Toggle Auto Ducker");
const close=button("×",hide);close.classList.add("armix-builtin-fx-close");close.setAttribute("aria-label","Close Auto Ducker");
head.append(dom("span","mx-ducker-brand","ARSONMIX"),dom("strong","","Auto Ducker"),target,preset,power,close);panel.append(head);
const content=dom("div","mx-ducker-content"),src=dom("div","mx-ducker-box"),srcTitle=dom("div","mx-ducker-title"),srcCount=dom("span","","TRIGGERS · 0"),edit=button("Edit sources",openPicker),sourceList=dom("div","mx-ducker-list");srcTitle.append(srcCount,edit);src.append(srcTitle,sourceList,dom("div","mx-ducker-note","Any selected source above threshold triggers one shared reduction."));
const grBox=dom("div","mx-ducker-box"),phase=dom("span","mx-ducker-phase","Waiting"),grTitle=dom("div","mx-ducker-title");grTitle.append(dom("span","","GAIN REDUCTION"),phase);
const meterObservers=new WeakMap();
function makeMeter(rail,kind,cells){
 rail.dataset.kind=kind;
 const bed=dom("div","mx-ducker-led-bed"),fill=dom("div","mx-ducker-fill"),grid=dom("div","mx-ducker-led-grid");
 bed.style.setProperty("--cells",cells);bed.style.setProperty("--bed-ratio",2*cells);
 for(let i=0;i<cells;i++)grid.append(dom("span","mx-ducker-led-cell"));
 bed.append(fill,grid);rail.append(bed);
 rail.dataset.cells=String(cells);
 // Geometry only; no layout read in native meter refresh.
 const observer=new ResizeObserver(entries=>{for(const entry of entries)fill.style.setProperty("--full-width",entry.contentRect.width+"px");});
 observer.observe(bed);
 meterObservers.set(rail,observer);
 return fill;
}
function meterLevel(fill,fraction){
 const bed=fill.parentElement,cells=Number(bed.parentElement.dataset.cells);
 const f=clamp(fraction,0,1);fill.style.width=(Math.floor(f*cells+1e-8)/cells*100)+"%";
 
}
const gr=dom("output","mx-ducker-gr","— dB"),rail=dom("div","mx-ducker-rail"),fill=makeMeter(rail,"gr",21);const ticks=dom("div","mx-ducker-ticks");for(const v of ["0","12","24","36 dB"])ticks.append(dom("span","",v));
grBox.append(grTitle,gr,rail,ticks,dom("div","mx-ducker-lcd-note","STEREO LINK  ·  POST FADER"));content.append(src,grBox);
const timing=dom("div","mx-ducker-box mx-ducker-timing"),advanced=dom("details","mx-ducker-box mx-ducker-advanced"),adv=dom("div","mx-ducker-timing");advanced.append(dom("summary","","Advanced detector"),adv,dom("p","mx-ducker-note","Post-fader key · linked stereo · independent detectors · zero added latency."));content.append(timing,advanced);
const picker=dom("section","mx-ducker-box mx-ducker-picker");picker.hidden=true;const search=dom("input");search.type="search";search.placeholder="Find input, group or bus";search.setAttribute("aria-label","Search trigger sources");const options=dom("div","mx-ducker-options"),actions=dom("div","mx-ducker-actions"),count=dom("output"),apply=button("Apply",applySources),cancel=button("Cancel",()=>{picker.hidden=true;edit.focus();});apply.classList.add("mx-ducker-apply");actions.append(count,cancel,apply);picker.append(dom("div","mx-ducker-title","SELECT TRIGGER SOURCES"),search,options,actions);content.append(picker);panel.append(content);const status=dom("div","mx-ducker-status");status.setAttribute("role","status");panel.append(status);document.body.append(panel);
let current=null,accepted=null,draft={...radio},sources=[],selection=new Set(),pending=null,read=null,seq=0,dirty=false,sendTimer=0,lastSend=0,lastMeter="",meterTime=0,meters=new Map(),meterFrame=null,sourceSignature="",error="";
const controls={};const clamp=(v,a,b)=>Math.min(b,Math.max(a,v));
function context(){const n=view(),id=current?.id,row=n?.graph?.channels?.find(c=>c.id===id),insert=row?.inserts?.[current?.slot];if(!current||!(n?.admitted||n?.logical)||!insert||insert.catalogId!==ID||insert.id!==current.instance)return null;
return {channelId:id,deviceEpoch:n.logical?"0":n.device.epoch,graphRevision:n.graph.revision,instanceId:insert.id,slot:current.slot,...(n.logical?{workspaceToken:n.workspaceToken}:{})};}
function keyMatches(a,c){return a&&c&&String(a.channelId)===String(c.channelId)&&String(a.instanceId)===String(c.instanceId)&&String(a.deviceEpoch)===String(c.deviceEpoch);}
function sameSettings(a,b){return !!a&&!!b&&Object.keys(defs).every(k=>Math.abs(a[k]-b[k])<.001);}
function sameSources(a,b){return a.length===b.length&&a.every(x=>b.includes(x));}
function valid(s){return !!s&&Object.entries(defs).every(([k,[,lo,hi]])=>Number.isFinite(s[k])&&s[k]>=lo&&s[k]<=hi);}
for(const [k,[label,lo,hi,step,unit]] of Object.entries(defs)){const wrap=dom("label","mx-ducker-control"),caption=dom("span","",label+" · "+unit),num=dom("input"),range=dom("input");num.type="number";range.type="range";for(const input of [num,range]){input.min=lo;input.max=hi;input.step=step;input.setAttribute("aria-label",label+" "+unit);}caption.textContent=label+" · "+unit;
num.title=unit;
const dial=dom("span","mx-ducker-dial"),body=dom("span","mx-ducker-body"),pointer=dom("span","mx-ducker-pointer");
dial.setAttribute("aria-hidden","true");
const ns="http://www.w3.org/2000/svg",svg=document.createElementNS(ns,"svg");
svg.setAttribute("viewBox","0 0 100 100");
const path="M 17.473 82.527 A 46 46 0 1 1 82.527 82.527";
const track=document.createElementNS(ns,"path"),progress=document.createElementNS(ns,"path");
for(const [el,cls] of [[track,"mx-ducker-arc-track"],[progress,"mx-ducker-arc-progress"]]){
el.setAttribute("class",cls);el.setAttribute("d",path);el.setAttribute("pathLength","100");svg.append(el);}
dial.append(body,pointer,svg);wrap.append(caption,dial,num,range);
controls[k]={num,range,pointer,progress,lo,hi};
(["thresholdDb","depthDb","attackMs","holdMs","releaseMs"].includes(k)?timing:adv).append(wrap);
function change(input){if(!accepted||!context()||input.value==="")return;const value=clamp(Number(input.value),lo,hi);if(!Number.isFinite(value))return;draft={...draft,[k]:value};dirty=true;num.value=value;range.value=value;paintControls();preset.value="Custom";error="";if(!pending&&Date.now()-lastSend>=65)send();else if(!sendTimer&&!pending)sendTimer=setTimeout(send,Math.max(0,65-(Date.now()-lastSend)));paint();}
range.addEventListener("input",()=>change(range));num.addEventListener("change",()=>change(num));
 window.ArMixFxGesture.bindRange(range,{get:()=>draft[k],enabled:()=>!!accepted&&!!context(),onChange:()=>change(range)});
 range.title="Drag or hover + wheel · Shift fine · double-click reset";
let drag=null;
range.addEventListener("pointerdown",e=>{if(e.button!==0||range.disabled)return;drag={id:e.pointerId,y:e.clientY,start:draft[k]};range.setPointerCapture(e.pointerId);range.focus();e.preventDefault();});
range.addEventListener("pointermove",e=>{if(!drag||drag.id!==e.pointerId)return;const raw=drag.start+(drag.y-e.clientY)*(hi-lo)/150*(e.shiftKey ? .1 : 1);range.value=clamp(lo+Math.round((raw-lo)/step)*step,lo,hi);change(range);e.preventDefault();});
const stop=e=>{if(!drag||drag.id!==e.pointerId)return;drag=null;send();};
range.addEventListener("pointerup",stop);range.addEventListener("pointercancel",stop);
range.addEventListener("dblclick",()=>{range.value=radio[k];change(range);send();});
}
function paintControls(){for(const [k,{num,range,pointer,progress,lo,hi}] of Object.entries(controls)){if(document.activeElement!==num)num.value=draft[k];range.value=draft[k];const f=clamp((draft[k]-lo)/(hi-lo),0,1);pointer.style.setProperty("--angle",(-135+270*f)+"deg");progress.setAttribute("stroke-dasharray",(100*f)+" 100");}preset.value=Object.keys(presets).find(k=>sameSettings(presets[k],draft))||"Custom";}
preset.addEventListener("change",()=>{if(!accepted||!presets[preset.value])return;draft={...presets[preset.value]};dirty=true;error="";paintControls();send();});
function channels(){return view()?.graph?.channels||[];}
function forbidden(id){if(id===current?.id)return "Target itself";const stack=[current?.id],seen=new Set();const edges=view()?.graph?.edges||[];while(stack.length){const from=stack.pop();if(seen.has(from))continue;seen.add(from);for(const e of edges)if(e.from===from)stack.push(e.to);}return seen.has(id)?"Downstream cycle":"";}
function openPicker(){if(!accepted||pending||dirty||!context())return;selection=new Set(sources);picker.hidden=false;search.value="";renderOptions();search.focus();}
function renderOptions(){options.replaceChildren();const q=search.value.toLowerCase().trim(),rows=channels().map(c=>({id:c.id,name:c.name||c.id,role:c.role||""}));for(const id of selection)if(!rows.some(x=>x.id===id))rows.push({id,name:"Missing source "+id,role:"Missing"});
for(const row of rows){if(!((row.name+" "+row.role).toLowerCase().includes(q)))continue;const label=dom("label","mx-ducker-option"),check=dom("input");check.type="checkbox";check.checked=selection.has(row.id);label.dataset.sourceId=row.id;check.setAttribute("aria-label",row.name);const reason=forbidden(row.id)||(!check.checked&&selection.size>=32?"32 source limit":"");check.disabled=!!reason&&!check.checked;check.title=reason||row.role;check.addEventListener("change",()=>{if(check.checked)selection.add(row.id);else selection.delete(row.id);renderOptions();});label.append(check,dom("span","",row.name),dom("small","",reason||row.role));options.append(label);}
if(!options.children.length)options.append(dom("div","mx-ducker-note","No matching channels."));count.value=selection.size+" / 32 selected";apply.disabled=!!pending||!context()||sameSources([...selection],sources);}
search.addEventListener("input",renderOptions);
function applySources(){if(!context()||pending||dirty)return;const ids=[...selection];if(ids.some(id=>forbidden(id))){error="Select upstream sources only; remove the cycle";paint();return;}submit("nativeDuckerSources",{sources:ids});}
function submit(action,payload){const c=context();if(!c||pending)return;const id="ducker-"+String(Date.now()*1000+(++seq%1000));pending={id,c,action,payload,ack:false,at:Date.now()};error="";backend()?.emitEvent("arMixAction",{action,requestId:id,...c,...payload});paint();}
function send(){clearTimeout(sendTimer);sendTimer=0;if(!dirty||pending||!accepted||!valid(draft)||!context())return;if(sameSettings(draft,accepted)){dirty=false;paint();return;}dirty=false;lastSend=Date.now();submit("nativeDuckerSet",{settings:{...draft}});}
function poll(){if(panel.hidden||!context()||!backend())return;const now=Date.now();if(read&&now-read.at<800)return;const c=context(),id="ducker-read-"+now+"-"+(++seq);read={id,c,at:now};backend().emitEvent("arMixAction",{action:"nativeDuckerSnapshot",requestId:id,...c});}
function paint(){const n=view(),c=context(),row=channels().find(x=>x.id===current?.id),insert=row?.inserts?.[current?.slot],live=!!c&&!n?.logical,fresh=live&&Date.now()-meterTime<700,bypassed=!!(n?.raw?.globalFxBypassed||row?.fxRackBypassed||insert?.bypassed);target.textContent=row?.name||"Target unavailable";power.dataset.fxState=!c?"unknown":(n?.raw?.globalFxBypassed||row?.fxRackBypassed)?"rack-off":insert?.bypassed?"off":"on";power.disabled=!c||power.dataset.fxState==="rack-off";power.setAttribute("aria-pressed",String(power.dataset.fxState==="on"));power.title=bypassed?"Bypassed (slot, rack or global)":"Enabled";preset.disabled=!accepted||!c;edit.disabled=!accepted||!c||!!pending||dirty;
for(const {num,range} of Object.values(controls)){num.disabled=range.disabled=!accepted||!c;}
const signature=JSON.stringify(sources.map(id=>[id,channels().find(x=>x.id===id)?.name]));if(signature!==sourceSignature){sourceSignature=signature;for(const old of sourceList.querySelectorAll(".mx-ducker-rail"))meterObservers.get(old)?.disconnect();sourceList.replaceChildren();for(const id of sources){const lane=channels().find(x=>x.id===id),item=dom("div","mx-ducker-source"),name=dom("span","",lane?.name||"Missing "+id),r=dom("div","mx-ducker-rail"),o=dom("output","mx-ducker-read","—");makeMeter(r,"source",12);item.append(name,r,o);item.dataset.sourceId=id;sourceList.append(item);}if(!sources.length)sourceList.append(dom("div","mx-ducker-note","No source selected · target stays unchanged."));}
srcCount.textContent="TRIGGERS · "+sources.length;for(const item of sourceList.querySelectorAll("[data-source-id]")){const id=item.dataset.sourceId,m=meters.get(id),lane=channels().find(x=>x.id===id),on=fresh&&m?.available&&Number.isFinite(m.dbfs)&&!!lane;item.dataset.active=String(!!on&&m.dbfs>=(accepted?.thresholdDb??-34));meterLevel(item.querySelector(".mx-ducker-fill"),on?clamp((m.dbfs+72)/72,0,1):0);item.querySelector("output").value=!lane?"Missing":lane.controls?.mute?"Muted":!fresh?"—":on?m.dbfs.toFixed(0)+" dB":"Unavailable";}
const value=fresh&&!bypassed&&Number.isFinite(meterFrame?.gainReductionDb)?meterFrame.gainReductionDb:null;gr.value=value===null?"— dB":(-value).toFixed(1)+" dB";meterLevel(fill,value===null?0:clamp(value/36,0,1));phase.textContent=!c?"Unavailable":n.logical?"Offline":bypassed?"Bypassed":!fresh?"Waiting for audio":!sources.length?"No triggers":["Ready","Attack","Ducking","Hold","Release"][meterFrame?.phase]||"Ready";
status.textContent=error||(!c?"Native instance unavailable — reopen from FX rack.":pending?"Waiting for native "+(pending.ack?"readback":"acknowledgement")+"…":dirty?"Applying settings…":n.logical?"Offline workspace · settings saved without live audio meters.":"");if(!picker.hidden)apply.disabled=!!pending||!c||sameSources([...selection],sources);}
function hide(){if(dirty&&pending){error="Finish pending edit before closing";paint();return;}if(dirty)send();panel.hidden=true;picker.hidden=true;clearTimeout(sendTimer);sendTimer=0;current=null;read=null;pending=null;dirty=false;}
const b=backend();b?.addEventListener("arMixBuiltinDuckerAck",a=>{if(!pending||a?.requestId!==pending.id||!keyMatches(a,pending.c))return;if(a.status==="PENDING"){error="Waiting for audio callback";paint();return;}if(a.status!=="APPLIED"){error="Not applied · "+(a.errorCode||"rejected");pending=null;dirty=false;if(accepted)draft={...accepted};paintControls();paint();return;}pending.ack=true;read=null;poll();paint();});
b?.addEventListener("arMixBuiltinDuckerSnapshot",a=>{if(!read||a?.requestId!==read.id)return;const c=context();read=null;if(!keyMatches(a,c)||String(a.graphRevision)!==String(c?.graphRevision)||a.status!=="ACCEPTED"||!valid(a.settings)||!Array.isArray(a.sources))return;accepted={...a.settings};sources=a.sources.map(String);if(a.meterSerial&&a.meterSerial!==lastMeter){lastMeter=a.meterSerial;meterTime=Date.now();meterFrame=a;meters=new Map((a.meters||[]).map(m=>[String(m.id),m]));}
if(pending?.ack){const done=pending.action==="nativeDuckerSources"?sameSources(sources,pending.payload.sources):sameSettings(accepted,pending.payload.settings);if(done){if(pending.action==="nativeDuckerSources"){picker.hidden=true;edit.focus();}pending=null;error="";}}
if(!pending&&!dirty){draft={...accepted};paintControls();}if(!pending&&dirty)send();paint();});
window.addEventListener("keydown",e=>{if(e.key!=="Escape"||panel.hidden)return;if(!picker.hidden){picker.hidden=true;edit.focus();}else hide();e.stopPropagation();});
setInterval(()=>{if(panel.hidden)return;if(!context()&&pending){pending=null;dirty=false;error="FX instance changed; reopen from rack";}poll();paint();},160);
window.ArSonMixNativeDucker=Object.freeze({open(track,slot){const n=view(),id=api()?.nativeIdFor?.(track),row=n?.graph?.channels?.find(x=>x.id===id),insert=row?.inserts?.[slot];if(!backend()||!insert||insert.catalogId!==ID)return false;
clearTimeout(sendTimer);sendTimer=0;lastSend=0;
current={track,id,slot,instance:insert.id};accepted=null;draft={...radio};sources=[];pending=null;read=null;dirty=false;error="";lastMeter="";meterTime=0;meters.clear();meterFrame=null;sourceSignature="!";picker.hidden=true;panel.hidden=false;paintControls();paint();poll();return true;},close:hide});
})();
