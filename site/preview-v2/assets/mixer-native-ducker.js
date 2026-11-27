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
#mxNativeDucker{position:fixed;z-index:3500;width:min(720px,calc(100vw - 12px));max-height:calc(100vh - 12px);overflow-x:hidden;overflow-y:auto;color:#d8e0e3;background:#172026;border:1px solid #47565d;border-radius:8px;box-shadow:0 12px 36px #0009;font:12px/1.4 system-ui,sans-serif;text-align:left;box-sizing:border-box}
#mxNativeDucker[hidden],#mxNativeDucker [hidden]{display:none!important}
#mxNativeDucker *{box-sizing:border-box}#mxNativeDucker button:not(.armix-builtin-fx-power):not(.armix-builtin-fx-close),#mxNativeDucker select,#mxNativeDucker input[type=number],#mxNativeDucker input[type=search]{color:inherit;background:#253138;border:1px solid #45545b;border-radius:4px;font:inherit;min-height:30px;padding:4px 8px} #mxNativeDucker button:not(.armix-builtin-fx-power):not(.armix-builtin-fx-close){cursor:pointer}#mxNativeDucker button:not(.armix-builtin-fx-power):not(.armix-builtin-fx-close):disabled,#mxNativeDucker input:disabled{opacity:.45;cursor:default}#mxNativeDucker :focus-visible:not(.armix-builtin-fx-power):not(.armix-builtin-fx-close){outline:2px solid #59d2c3;outline-offset:2px}
.mx-ducker-head{display:flex;align-items:center;gap:8px;padding:8px 12px;background:#202b31;cursor:move;position:sticky;top:0;z-index:2}.mx-ducker-head strong{font-size:15px;font-weight:600;white-space:nowrap}.mx-ducker-channel{color:#9caeb7;flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.mx-ducker-content{padding:12px;display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr);gap:12px}.mx-ducker-box{background:#11191e;border:1px solid #34434a;border-radius:5px;padding:10px;min-width:0}.mx-ducker-title{display:flex;align-items:center;justify-content:space-between;gap:8px;margin-bottom:8px;font-size:11px;letter-spacing:.05em;color:#a8b8c0}.mx-ducker-source{display:grid;grid-template-columns:minmax(0,1fr) 84px 66px;align-items:center;gap:8px;padding:5px 0;border-bottom:1px solid #26343a}.mx-ducker-source span:first-child{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.mx-ducker-list{max-height:160px;overflow:auto}.mx-ducker-rail{height:6px;background:#28363c;border-radius:2px;overflow:hidden}.mx-ducker-fill{height:100%;width:0;background:#58cbb7;transition:width 100ms linear}.mx-ducker-read{font-variant-numeric:tabular-nums;text-align:right;color:#a8bbc4;font-size:11px}.mx-ducker-gr{font-size:27px;font-weight:500;color:#6bdac9;font-variant-numeric:tabular-nums}.mx-ducker-ticks{display:flex;justify-content:space-between;color:#91a4ae;font-size:10px}.mx-ducker-controls{display:grid;grid-template-columns:1fr 1fr;gap:9px}.mx-ducker-timing{grid-column:1/-1;display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px}.mx-ducker-control{display:grid;grid-template-columns:minmax(0,1fr) 63px;align-items:center;gap:4px}.mx-ducker-control input[type=range]{grid-column:1/-1;width:100%;accent-color:#59c9b7;margin:3px 0}.mx-ducker-control input[type=number]{width:63px;text-align:right;padding:3px}.mx-ducker-advanced{grid-column:1/-1}.mx-ducker-advanced summary{cursor:pointer;color:#aebfc6}.mx-ducker-advanced .mx-ducker-timing{margin-top:10px}.mx-ducker-note{color:#a5b5bd;font-size:11px}.mx-ducker-picker{grid-column:1/-1}.mx-ducker-picker input[type=search]{width:100%;margin-bottom:6px}.mx-ducker-options{max-height:172px;overflow:auto}.mx-ducker-option{display:flex;align-items:center;gap:8px;min-height:30px;border-bottom:1px solid #29373e}.mx-ducker-option input{accent-color:#59c9b7}.mx-ducker-option small{margin-left:auto;color:#a5b5bd}.mx-ducker-actions{display:flex;gap:8px;align-items:center;justify-content:flex-end;margin-top:8px}.mx-ducker-actions output{margin-right:auto;color:#a6b8bf}.mx-ducker-apply{border-color:#59b6a5!important;color:#6be2ca!important}
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


.mx-ducker-dial svg{position:absolute;inset:0;width:100%;height:100%;overflow:visible;pointer-events:none}
.mx-ducker-arc-track,.mx-ducker-arc-progress{fill:none;stroke-linecap:round;stroke-width:4}.mx-ducker-arc-track{stroke:#20383e}.mx-ducker-arc-progress{stroke:#6ce7c5}
#mxNativeDucker .mx-ducker-control input[type=range]{position:absolute;top:18px;left:50%;transform:translateX(-50%);width:65px;height:65px;opacity:0;margin:0;cursor:ns-resize;touch-action:none;z-index:1}
#mxNativeDucker .mx-ducker-control input[type=number]{grid-row:3;width:73px;min-height:23px;background:#20323b;color:#c9efe7;border:1px solid #718894;box-shadow:inset 0 1px 3px #0008;font:11px ui-monospace,monospace;text-align:center;padding:2px 4px}
.mx-ducker-control:has(input[type=range]:focus-visible) input[type=number]{border-color:#8de4d2}
.mx-ducker-advanced{padding:7px 10px}.mx-ducker-advanced summary{font-size:11px;color:#d5e3e6}.mx-ducker-advanced .mx-ducker-timing{grid-template-columns:repeat(3,minmax(0,1fr));padding:4px}
.mx-ducker-note{font-size:10px;color:#9db7bf}.mx-ducker-advanced .mx-ducker-note{color:#d0dde2}
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


/* V6 owner-approved hardware finish. Static light, separate rotating pointer.
   Shared FX chrome is deliberately excluded. */
#mxNativeDucker{background-image:var(--ducker-brushed),linear-gradient(180deg,#556671,#34454f 45%,#2b3942);background-size:384px 192px,100% 100%;border-color:#778b95}
#mxNativeDucker .mx-ducker-head{background-image:var(--ducker-brushed),linear-gradient(#303d46,#1d2931);background-size:384px 192px,100% 100%}
#mxNativeDucker .mx-ducker-content>.mx-ducker-timing,#mxNativeDucker .mx-ducker-advanced{background-image:var(--ducker-brushed),linear-gradient(180deg,#526570,#435660 70%,#40525d);background-size:384px 192px,100% 100%}
#mxNativeDucker .mx-ducker-channel{padding-left:7px;border-left:1px solid #4d606b}
#mxNativeDucker .mx-ducker-content>.lcd-glass{position:relative;isolation:isolate;overflow:hidden;border:1px solid #09151c;border-radius:6px;padding:11px 10px 10px;min-height:132px;background:radial-gradient(ellipse at 48% 115%,#1a363d 0%,transparent 75%),linear-gradient(160deg,#10262e,#0a171d 85%);box-shadow:inset 0 0 0 3px #071318,inset 0 3px 11px #000c,0 1px 0 #99aeb970,0 -1px 0 #1a282e}
#mxNativeDucker .lcd-glass::before{content:"";position:absolute;z-index:2;inset:3px;border-radius:3px;pointer-events:none;background:linear-gradient(155deg,#c6e9f50e 0%,#acd5e809 29%,#b8e2f506 44%,transparent 44.4%,transparent 77%,#bad9ec06 100%);box-shadow:inset 0 0 0 1px #a9d6e022,inset 0 1px 0 #e2f5ff32,inset 1px 0 0 #9bc9dc26,inset -1px -1px 0 #8fb6c42b,inset 0 -3px 6px #c9f3ff07}
#mxNativeDucker .lcd-glass::after{content:"";position:absolute;z-index:3;inset:3px;border-radius:3px;pointer-events:none;background:radial-gradient(ellipse 34% 1.4px at 28% 0%,#d5f5ff92 0%,#d5f5ff22 65%,transparent 100%),radial-gradient(ellipse 27% 1.4px at 76% 100%,#c5e9f06e,transparent 100%)}
#mxNativeDucker .lcd-glass .mx-ducker-title{color:#b5d0d6}
#mxNativeDucker .lcd-glass .mx-ducker-lcd-note{color:#9bbbc4}
#mxNativeDucker .lcd-glass .mx-ducker-gr{width:max-content;min-width:110px;padding:2px 10px;font-size:23px;border-color:#526f78;box-shadow:inset 0 0 0 1px #b1d3d01a,inset 0 2px 6px #000b,0 1px 0 #a7c5c12b;background:linear-gradient(145deg,#153239,#0b1d24 52%,#122c30);text-shadow:0 0 6px #8bf0d52a}
#mxNativeDucker .mx-ducker-control input[type=number]{border-radius:2px;border-color:#718894;background:linear-gradient(155deg,#142c32,#08191f 48%,#10292b);box-shadow:inset 0 0 0 1px #cae9e311,inset 0 2px 4px #000a,0 1px 0 #a7c5c12a}
#mxNativeDucker .mx-ducker-body{inset:7%;background:var(--ducker-body) center/100% 100% no-repeat,radial-gradient(circle,#243039 55%,#101a21 65%,#6a7980 69%,#152029 73%);background-blend-mode:normal;border:none;box-shadow:none;filter:brightness(.69) contrast(1.14) drop-shadow(0 3px 2px #0009);transform:none}
#mxNativeDucker .mx-ducker-pointer{inset:11%;background:none;transform:rotate(var(--angle,-135deg));transform-origin:50% 50%}
#mxNativeDucker .mx-ducker-pointer::after{content:"";position:absolute;box-sizing:border-box;width:3px;height:12px;left:calc(50% - 1.5px);top:4px;background:#70f3d4;border:1px solid #123e3e;box-shadow:0 0 2px #0c1b20,0 0 3px #56ffce60;border-radius:1px}
#mxNativeDucker .mx-ducker-source{grid-template-columns:minmax(0,1fr) minmax(0,168px) 78px;gap:6px;min-height:28px;min-width:0}
#mxNativeDucker .mx-ducker-list{overflow-x:hidden;overflow-y:auto;min-width:0}
#mxNativeDucker .mx-ducker-read{min-width:0;white-space:nowrap;font-family:system-ui,sans-serif}
#mxNativeDucker .mx-ducker-control input[type=range]:focus-visible{outline:none}
#mxNativeDucker .mx-ducker-control input[type=number]{max-width:100%;min-width:0}
#mxNativeDucker .mx-ducker-control input[type=number]::-webkit-inner-spin-button,#mxNativeDucker .mx-ducker-control input[type=number]::-webkit-outer-spin-button{appearance:none;margin:0}
#mxNativeDucker .mx-ducker-control input[type=number]{appearance:textfield}
#mxNativeDucker .mx-ducker-phase{min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
#mxNativeDucker .mx-ducker-phase[data-error=true]{color:#efc58f}
#mxNativeDucker .mx-ducker-rail{padding:3px;border-color:#52717d;border-radius:2px;background:#081319;box-shadow:inset 0 1px 3px #000,0 1px 0 #7292a329}
#mxNativeDucker .mx-ducker-rail[data-kind=gr]{margin-top:4px}
#mxNativeDucker .mx-ducker-rail{container-type:inline-size;min-width:0}
#mxNativeDucker .mx-ducker-led-bed{aspect-ratio:auto;height:calc(100cqw / var(--cells) / 2 + 1px);min-height:3px}
#mxNativeDucker .mx-ducker-led-grid{grid-template-rows:minmax(0,1fr)}
#mxNativeDucker .mx-ducker-led-cell{min-height:0}
#mxNativeDucker .mx-ducker-picker{min-width:0;overflow-anchor:none}
#mxNativeDucker .mx-ducker-options{overscroll-behavior:contain;scrollbar-gutter:stable}
#mxNativeDucker .mx-ducker-option{height:30px;flex-shrink:0}
#mxNativeDucker .mx-ducker-option input{flex:0 0 14px;width:14px;height:14px;margin:0;transition:none;animation:none}
#mxNativeDucker .mx-ducker-option span{min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
#mxNativeDucker .mx-ducker-fill{background:linear-gradient(90deg,#119c49 0%,#22cc40 25%,#8bea25 60%,#e6ee3b 75%,#ffc338 88%,#ff5144 100%);background-size:var(--full-width,140px) 100%}
#mxNativeDucker .mx-ducker-rail[data-kind=gr] .mx-ducker-fill{background:linear-gradient(90deg,#e8b951,#ffd36e 75%,#f1ad3f);background-size:var(--full-width,140px) 100%}
@media(max-width:600px){#mxNativeDucker .mx-ducker-content{grid-template-columns:1fr}#mxNativeDucker .mx-ducker-timing{grid-template-columns:repeat(3,minmax(0,1fr))}#mxNativeDucker .mx-ducker-source{grid-template-columns:minmax(0,1fr) minmax(0,126px) 78px}}
@media(max-width:400px){#mxNativeDucker .mx-ducker-source{grid-template-columns:minmax(0,1fr) minmax(0,100px) 72px}#mxNativeDucker .mx-ducker-control>span:first-child{font-size:9px;letter-spacing:0}}
`;document.head.append(style);
const panel=dom("section");panel.id="mxNativeDucker";panel.hidden=true;panel.setAttribute("role","dialog");panel.setAttribute("aria-modal","false");panel.setAttribute("aria-label","Auto Ducker");
const assetRoot=new URL(".",document.currentScript.src);
panel.style.setProperty("--ducker-body",`url("${new URL("mixer-gain-knob-brushed-steel.png",assetRoot)}")`);
panel.style.setProperty("--ducker-brushed",`url("${new URL("auto-ducker-brushed-v6.png",assetRoot)}")`);
const head=dom("header","mx-ducker-head"),target=dom("span","mx-ducker-channel"),preset=dom("select");preset.setAttribute("aria-label","Ducking preset");
for(const name of ["Custom",...Object.keys(presets)]){const op=dom("option","",name);op.value=name;preset.append(op);}
const power=button("⏻",()=>{if(current&&context())api()?.consumeAction?.(current.track,"slotPower",{dataset:{slot:String(current.slot)}});});power.classList.add("armix-builtin-fx-power");power.dataset.fxState="unknown";power.disabled=true;power.setAttribute("aria-pressed","false");power.setAttribute("aria-label","Auto Ducker power");
const close=button("×",hide);close.classList.add("armix-builtin-fx-close");close.title="Close Auto Ducker";close.setAttribute("aria-label","Close Auto Ducker");
head.append(dom("span","mx-ducker-brand","ARSONMIX"),dom("strong","","Auto Ducker"),target,preset,power,close);panel.append(head);
const content=dom("div","mx-ducker-content"),src=dom("div","mx-ducker-box lcd-glass"),srcTitle=dom("div","mx-ducker-title"),srcCount=dom("span","","TRIGGERS · 0"),edit=button("Edit sources",openPicker),sourceList=dom("div","mx-ducker-list");srcTitle.append(srcCount,edit);src.append(srcTitle,sourceList,dom("div","mx-ducker-note","Any selected source above threshold triggers one shared reduction."));
const grBox=dom("div","mx-ducker-box lcd-glass"),phase=dom("span","mx-ducker-phase","Waiting"),grTitle=dom("div","mx-ducker-title");grTitle.append(dom("span","","GAIN REDUCTION"),phase);
const meterObserver=new ResizeObserver(entries=>{
 for(const entry of entries){
  const w=entry.contentRect.width;
  if(w<=0)continue;
  const f=entry.target.firstElementChild;
  if(f)f.style.setProperty("--full-width",w+"px");
 }
});
const meterSteps=new WeakMap();
function makeMeter(rail,kind,cells){
 rail.dataset.kind=kind;
 const bed=dom("div","mx-ducker-led-bed"),fill=dom("div","mx-ducker-fill"),grid=dom("div","mx-ducker-led-grid");
 bed.style.setProperty("--cells",cells);bed.style.setProperty("--bed-ratio",2*cells);
 for(let i=0;i<cells;i++)grid.append(dom("span","mx-ducker-led-cell"));
 bed.append(fill,grid);rail.append(bed);
 rail.dataset.cells=String(cells);
 // Geometry only; no layout read in native meter refresh.
 meterObserver.observe(bed);
 return fill;
}
function meterLevel(fill,fraction){
 const bed=fill.parentElement,cells=Number(bed.parentElement.dataset.cells);
 const step=Math.floor(clamp(fraction,0,1)*cells+1e-8);
 if(meterSteps.get(fill)===step)return;
 meterSteps.set(fill,step);fill.style.width=(step/cells*100)+"%";
 
}
const gr=dom("output","mx-ducker-gr","— dB"),rail=dom("div","mx-ducker-rail"),fill=makeMeter(rail,"gr",21);const ticks=dom("div","mx-ducker-ticks");for(const v of ["0","12","24","36 dB"])ticks.append(dom("span","",v));
grBox.append(grTitle,gr,rail,ticks,dom("div","mx-ducker-lcd-note","STEREO LINK  ·  POST FADER"));content.append(src,grBox);
const timing=dom("div","mx-ducker-box mx-ducker-timing"),advanced=dom("details","mx-ducker-box mx-ducker-advanced"),adv=dom("div","mx-ducker-timing");advanced.append(dom("summary","","Advanced detector"),adv,dom("p","mx-ducker-note","Post-fader key · linked stereo · independent detectors · zero added latency."));content.append(timing,advanced);
const picker=dom("section","mx-ducker-box mx-ducker-picker");picker.hidden=true;const search=dom("input");search.type="search";search.placeholder="Find input, group or bus";search.setAttribute("aria-label","Search trigger sources");const options=dom("div","mx-ducker-options"),actions=dom("div","mx-ducker-actions"),count=dom("output"),apply=button("Apply",applySources),cancel=button("Cancel",()=>{picker.hidden=true;edit.focus();});apply.classList.add("mx-ducker-apply");cancel.classList.add("mx-ducker-cancel");actions.append(count,cancel,apply);picker.append(dom("div","mx-ducker-title","SELECT TRIGGER SOURCES"),search,options,actions);content.append(picker);panel.append(content);document.body.append(panel);
let current=null,accepted=null,draft={...radio},sources=[],selection=new Set(),pending=null,read=null,seq=0,dirty=false,sendTimer=0,lastSend=0,lastReadRevision="",lastMeter="",meterTime=0,meters=new Map(),meterFrame=null,sourceSignature="",error="";
const controls={},sourceRows=new Map();const EDIT_NOTICE_MS=8000;const clamp=(v,a,b)=>Math.min(b,Math.max(a,v));
function context(){const n=view(),id=current?.id,row=n?.graph?.channels?.find(c=>c.id===id),insert=row?.inserts?.[current?.slot];if(!current||!(n?.admitted||n?.logical)||!insert||insert.catalogId!==ID||insert.id!==current.instance||String(n.logical?"0":n.device?.epoch)!==current.epoch||(n.logical?n.workspaceToken:"")!==current.workspace)return null;
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
function change(input){if(!accepted||!context())return;if(input.value===""||!Number.isFinite(Number(input.value))){putValue(input,String(draft[k]));return;}const value=clamp(Number((lo+Math.round((Number(input.value)-lo)/step)*step).toFixed(8)),lo,hi);if(draft[k]===value){putValue(input,String(value));return;}draft={...draft,[k]:value};dirty=true;num.value=value;range.value=value;paintControls();preset.value="Custom";error="";if(!pending&&Date.now()-lastSend>=65)send();else if(!sendTimer&&!pending)sendTimer=setTimeout(send,Math.max(0,65-(Date.now()-lastSend)));paint();}
range.addEventListener("input",()=>change(range));num.addEventListener("change",()=>change(num));num.addEventListener("blur",()=>putValue(num,String(draft[k])));
 window.ArMixFxGesture.bindRange(range,{get:()=>draft[k],enabled:()=>!!accepted&&!!context(),onChange:()=>change(range)});
 range.title="Drag or hover + wheel · Shift fine · double-click reset";
let drag=null;
range.addEventListener("pointerdown",e=>{if(e.button!==0||range.disabled)return;drag={id:e.pointerId,y:e.clientY,start:draft[k],owner:current};range.setPointerCapture(e.pointerId);range.focus();e.preventDefault();});
range.addEventListener("pointermove",e=>{if(!drag||drag.id!==e.pointerId||drag.owner!==current||!context())return;const raw=drag.start+(drag.y-e.clientY)*(hi-lo)/150*(e.shiftKey ? .1 : 1);range.value=clamp(lo+Math.round((raw-lo)/step)*step,lo,hi);change(range);e.preventDefault();});
const stop=e=>{if(!drag||drag.id!==e.pointerId)return;const owner=drag.owner;drag=null;if(owner===current)send();};
range.addEventListener("pointerup",stop);range.addEventListener("pointercancel",stop);range.addEventListener("lostpointercapture",stop);
range.addEventListener("dblclick",()=>{range.value=radio[k];change(range);send();});
}
function paintControls(){for(const [k,{num,range,pointer,progress,lo,hi}] of Object.entries(controls)){if(document.activeElement!==num)putValue(num,String(draft[k]));putValue(range,String(draft[k]));const f=clamp((draft[k]-lo)/(hi-lo),0,1);const angle=(-135+270*f)+"deg",arc=(100*f)+" 100";if(pointer.style.getPropertyValue("--angle")!==angle)pointer.style.setProperty("--angle",angle);if(progress.getAttribute("stroke-dasharray")!==arc)progress.setAttribute("stroke-dasharray",arc);}preset.value=Object.keys(presets).find(k=>sameSettings(presets[k],draft))||"Custom";}
preset.addEventListener("change",()=>{if(!accepted||!presets[preset.value])return;draft={...presets[preset.value]};dirty=true;error="";paintControls();send();});
function channels(){return view()?.graph?.channels||[];}
function downstream(){
 const outgoing=new Map(),seen=new Set(),stack=[current?.id];
 for(const e of view()?.graph?.edges||[]){if(!outgoing.has(e.from))outgoing.set(e.from,[]);outgoing.get(e.from).push(e.to);}
 while(stack.length){const from=stack.pop();if(seen.has(from))continue;seen.add(from);for(const to of outgoing.get(from)||[])stack.push(to);}
 return seen;
}
function forbidden(id,seen){return id===current?.id?"Target itself":seen.has(id)?"Downstream cycle":"";}
function openPicker(){if(!accepted||!context())return;if(!picker.hidden){search.focus({preventScroll:true});return;}selection=new Set(sources);picker.hidden=false;search.value="";renderOptions();search.focus({preventScroll:true});}
function renderOptions(){options.replaceChildren();const seen=downstream(),q=search.value.toLowerCase().trim(),rows=channels().map(c=>({id:c.id,name:c.name||c.id,role:c.role||""}));for(const id of selection)if(!rows.some(x=>x.id===id))rows.push({id,name:"Missing source "+id,role:"Missing"});
for(const row of rows){if(!((row.name+" "+row.role).toLowerCase().includes(q)))continue;const label=dom("label","mx-ducker-option"),check=dom("input");check.type="checkbox";check.checked=selection.has(row.id);label.dataset.sourceId=row.id;check.setAttribute("aria-label",row.name);const reason=forbidden(row.id,seen)||(!check.checked&&selection.size>=32?"32 source limit":"");check.disabled=!!reason&&!check.checked;check.title=reason||row.role;check.addEventListener("change",()=>{if(check.checked)selection.add(row.id);else selection.delete(row.id);refreshOptions();});label.append(check,dom("span","",row.name),dom("small","",reason||row.role));options.append(label);}
if(!options.children.length)options.append(dom("div","mx-ducker-note","No matching channels."));refreshOptions();}
function refreshOptions(){const seen=downstream(),byId=new Map(channels().map(c=>[c.id,c]));for(const label of options.querySelectorAll("[data-source-id]")){const id=label.dataset.sourceId,check=label.querySelector("input"),role=byId.get(id)?.role||"Missing";const reason=forbidden(id,seen)||(!check.checked&&selection.size>=32?"32 source limit":"");check.disabled=!!reason&&!check.checked;check.title=reason||role;putText(label.querySelector("small"),reason||role);}count.value=selection.size+" / 32 selected";apply.disabled=!!pending||dirty||!context()||sameSources([...selection],sources);}
search.addEventListener("input",renderOptions);
function applySources(){if(!context()||pending||dirty)return;const ids=[...selection],seen=downstream();if(ids.some(id=>forbidden(id,seen))){error="Select upstream sources only; remove the cycle";paint();return;}submit("nativeDuckerSources",{sources:ids});}
function submit(action,payload){const c=context();if(!c||pending)return;const id="ducker-"+String(Date.now()*1000+(++seq%1000));pending={id,c,action,payload,ack:false,at:Date.now()};error="";backend()?.emitEvent("arMixAction",{action,requestId:id,...c,...payload});paint();}
function send(){clearTimeout(sendTimer);sendTimer=0;if(!dirty||pending||!accepted||!valid(draft)||!context())return;if(sameSettings(draft,accepted)){dirty=false;paint();return;}dirty=false;lastSend=Date.now();submit("nativeDuckerSet",{settings:{...draft}});}
function poll(){const c=context();if(panel.hidden||!c||!backend())return;if(view()?.logical&&accepted&&!pending&&!dirty&&lastReadRevision===c.graphRevision)return;const now=Date.now();if(read&&now-read.at<800)return;const id="ducker-read-"+now+"-"+(++seq);read={id,c,at:now};backend().emitEvent("arMixAction",{action:"nativeDuckerSnapshot",requestId:id,...c});}
function putText(el,text){if(el.textContent!==text)el.textContent=text;}
function putValue(el,value){if(el.value!==value)el.value=value;}
function paint(){const n=view(),c=context(),byId=new Map((n?.graph?.channels||[]).map(ch=>[ch.id,ch])),row=byId.get(current?.id),insert=row?.inserts?.[current?.slot],live=!!c&&!n?.logical,fresh=live&&Date.now()-meterTime<700,bypassed=!!(n?.raw?.globalFxBypassed||row?.fxRackBypassed||insert?.bypassed);putText(target,row?.name||"Target unavailable");power.dataset.fxState=!c?"unknown":(n?.raw?.globalFxBypassed||row?.fxRackBypassed)?"rack-off":insert?.bypassed?"off":"on";power.disabled=!c||power.dataset.fxState==="rack-off";power.setAttribute("aria-pressed",String(power.dataset.fxState==="on"));power.title=power.dataset.fxState==="on"?"AUTO DUCKER ON · click to bypass":power.dataset.fxState==="off"?"AUTO DUCKER BYPASSED · click to enable":power.dataset.fxState==="rack-off"?"FX RACK BYPASSED · enable rack first":"FX STATE UNAVAILABLE";power.setAttribute("aria-label",power.title);if(preset.disabled!==(!accepted||!c))preset.disabled=!accepted||!c;// Source selection is a local draft. Only Apply is fenced by the native transaction.
const editDisabled=!accepted||!c;if(edit.disabled!==editDisabled)edit.disabled=editDisabled;
for(const {num,range} of Object.values(controls)){if(num.disabled!==editDisabled)num.disabled=editDisabled;if(range.disabled!==editDisabled)range.disabled=editDisabled;}
const signature=JSON.stringify(sources.map(id=>[id,byId.get(id)?.name]));if(signature!==sourceSignature){sourceSignature=signature;for(const node of sourceRows.values())meterObserver.unobserve(node.fill.parentElement);sourceRows.clear();sourceList.replaceChildren();for(const id of sources){const lane=byId.get(id),item=dom("div","mx-ducker-source"),name=dom("span","",lane?.name||"Missing "+id),r=dom("div","mx-ducker-rail"),o=dom("output","mx-ducker-read","—");const sourceFill=makeMeter(r,"source",16);sourceRows.set(id,{item,fill:sourceFill,output:o});item.append(name,r,o);item.dataset.sourceId=id;sourceList.append(item);}if(!sources.length)sourceList.append(dom("div","mx-ducker-note","No source selected · target stays unchanged."));}
putText(srcCount,"TRIGGERS · "+sources.length);for(const [id,{item,fill:sourceFill,output}] of sourceRows){const m=meters.get(id),lane=byId.get(id),on=fresh&&m?.available&&Number.isFinite(m.dbfs)&&!!lane&&!lane.controls?.mute;const active=String(!!on&&m.dbfs>=(accepted?.thresholdDb??-34));if(item.dataset.active!==active)item.dataset.active=active;meterLevel(sourceFill,on?clamp((m.dbfs+72)/72,0,1):0);putValue(output,!lane?"Missing":lane.controls?.mute?"Muted":!fresh?"—":on?m.dbfs.toFixed(0)+" dB":"Unavailable");}
const value=fresh&&!bypassed&&Number.isFinite(meterFrame?.gainReductionDb)?meterFrame.gainReductionDb:null;putValue(gr,value===null?"— dB":(-value).toFixed(1)+" dB");meterLevel(fill,value===null?0:clamp(value/36,0,1));const phaseText=!c?"Unavailable":n.logical?"Offline":bypassed?"Bypassed":!fresh?"Waiting for audio":!sources.length?"No triggers":["Ready","Attack","Ducking","Hold","Release"][meterFrame?.phase]||"Ready";
// No transient transaction footer: only actionable errors use the existing LCD phase.
const delayed=!!pending&&Date.now()-pending.at>=EDIT_NOTICE_MS;const notice=error||(delayed?"Native edit unconfirmed; waiting for authoritative audio readback.":"");const errorState=String(!!notice);if(phase.dataset.error!==errorState)phase.dataset.error=errorState;if(phase.title!==notice)phase.title=notice;
putText(phase,error?"Check edit":delayed?"Edit delayed":phaseText);
if(!picker.hidden){const blocked=!!pending||dirty||!c||sameSources([...selection],sources);if(apply.disabled!==blocked)apply.disabled=blocked;}}
function hide(){if(dirty&&pending&&Date.now()-pending.at<EDIT_NOTICE_MS){error="Finish pending edit before closing";paint();return;}if(dirty)send();panel.hidden=true;picker.hidden=true;clearTimeout(sendTimer);sendTimer=0;current=null;read=null;pending=null;dirty=false;}
const b=backend();b?.addEventListener("arMixBuiltinDuckerAck",a=>{if(!pending||a?.requestId!==pending.id||!keyMatches(a,pending.c))return;if(a.status==="PENDING")return;if(a.status!=="APPLIED"){error="Not applied · "+(a.errorCode||"rejected");pending=null;dirty=false;lastReadRevision="";if(accepted)draft={...accepted};paintControls();paint();return;}pending.ack=true;read=null;poll();paint();});
b?.addEventListener("arMixBuiltinDuckerSnapshot",a=>{if(!read||a?.requestId!==read.id)return;const c=context();read=null;if(!keyMatches(a,c)||String(a.graphRevision)!==String(c?.graphRevision)||a.status!=="ACCEPTED"||!valid(a.settings)||!Array.isArray(a.sources))return;accepted={...a.settings};lastReadRevision=c.graphRevision;sources=a.sources.map(String);if(a.meterSerial&&a.meterSerial!==lastMeter){lastMeter=a.meterSerial;meterTime=Date.now();meterFrame=a;meters=new Map((a.meters||[]).map(m=>[String(m.id),m]));}
if(pending?.ack){const done=pending.action==="nativeDuckerSources"?sameSources(sources,pending.payload.sources):sameSettings(accepted,pending.payload.settings);if(done){if(pending.action==="nativeDuckerSources"){picker.hidden=true;edit.focus();}pending=null;error="";}}
// A native quiescent snapshot after the deadline may recover a lost ACK.
 // Do not infer success, resend an ambiguous transaction or replay a queued draft.
 if(pending&&Date.now()-pending.at>=EDIT_NOTICE_MS&&a.pending===false){
   pending=null;dirty=false;clearTimeout(sendTimer);sendTimer=0;
   error="Edit confirmation lost; showing current native values. Check before editing again.";
 }
 if(!pending&&!dirty){if(!sameSettings(draft,accepted)){draft={...accepted};paintControls();}}if(!pending&&dirty)send();paint();});
window.addEventListener("keydown",e=>{if(e.key!=="Escape"||panel.hidden||window.ArSonMixFxWorkspace)return;if(!picker.hidden){picker.hidden=true;edit.focus();}else hide();e.stopPropagation();});
setInterval(()=>{if(panel.hidden)return;if(!context()){if(accepted||pending||dirty){accepted=null;pending=null;dirty=false;read=null;clearTimeout(sendTimer);sendTimer=0;meterTime=0;error="FX instance or device changed; reopen from rack";}paint();return;}poll();paint();},160);
window.ArSonMixNativeDucker=Object.freeze({open(track,slot){const n=view(),id=api()?.nativeIdFor?.(track),row=n?.graph?.channels?.find(x=>x.id===id),insert=row?.inserts?.[slot];if(!backend()||!insert||insert.catalogId!==ID)return false;
clearTimeout(sendTimer);sendTimer=0;lastSend=0;
current={track,id,slot,instance:insert.id,epoch:String(n.logical?"0":n.device?.epoch),workspace:n.logical?n.workspaceToken:""};panel.dataset.fxTrackId=String(track.id);panel.dataset.fxSlot=String(slot);accepted=null;draft={...radio};sources=[];pending=null;read=null;dirty=false;error="";lastReadRevision="";lastMeter="";meterTime=0;meters.clear();meterFrame=null;sourceSignature="!";picker.hidden=true;panel.hidden=false;window.ArSonMixFxWorkspace?.raise(panel);paintControls();paint();poll();return true;},close:hide});
})();
