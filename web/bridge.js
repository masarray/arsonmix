// SPDX-License-Identifier: AGPL-3.0-only
// Actual JUCE native bridge. This script is loaded only inside the desktop host.
// Never claims the other 11 concept tracks or mock presets are live channels.
(() => {
  const backend = window.__JUCE__?.backend;
  if (!backend?.emitEvent || !backend?.addEventListener) return;
  document.documentElement.classList.add("native-engine");
  // Both the historical --level contract and the compositor-backed meter cap.
  // This bridge runs only inside JUCE; native endpoint telemetry is unchanged.
  function paintMeter(el,value){
    if(!el)return;
    const level=Math.max(0,Math.min(100,Number(value)||0));
    if(typeof window.__arMixSetNativeMeterTarget==="function")
      window.__arMixSetNativeMeterTarget(el,level);
    else if(typeof window.__arMixSetLedMeterLevel==="function")
      window.__arMixSetLedMeterLevel(el,level);
    else el.style.setProperty("--level",level+"%");
  }
  // Clear preview levels before first real device callback/status.
  // One-time native boot clear includes unrouted preview channels. Keep it
  // direct so the live 30fps animator owns ONLY CH1 and real Master meters.
  document.querySelectorAll("[data-meter],#masterMeterL,#masterMeterR,#masterMeterAux")
    .forEach(el=>{
      if(typeof window.__arMixSetLedMeterLevel==="function")
        window.__arMixSetLedMeterLevel(el,0);
      else el.style.setProperty("--level","0%");
    });

  const send = (action, more = {}) => backend.emitEvent("arMixAction", {action, ...more});
  const state = {catalog: [], chain: [], scanning: false, message: "Connecting to native host...",
    device: {}, presets: [], addRequestId:0, addError:"", pendingPlugin:"", filter: "", selectedCategory: "All", monitor: false, error: "", progress: {active:false,phase:"done",done:0,total:0,found:0,current:""}, adding:false};

  const orbit = {slot:-1, plugin:"", parameters:[], active:false, activeSlot:-1, activeParameter:-1, periodSeconds:20, reverse:false, preferredParameter:-1, selection:-1, revision:null,nearSetpoint:20,farSetpoint:70,distanceUnit:"%",distanceMin:0,distanceMax:100,selectedShape:0,appliedPeriod:20,appliedReverse:false,initialized:false,shape:0,distanceSelection:-1,elevationSelection:-1,distanceIndex:-1,elevationIndex:-1,preferredDistance:-1,preferredElevation:-1,motionPath:[],visualPhase:0,visualTime:0};
  const escapeHtml = text => String(text ?? "").replace(/[&<>"']/g, c => (
    {"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
  const percent = peak => Math.max(1,Math.min(100,(20*Math.log10(Math.max(1e-6,Number(peak)||0))+60)*100/60));
  const toStatus = text => {
    const footer = document.querySelector("#audioStatus");
    if(footer && footer.textContent!==text) footer.textContent = text;
  };

  // The v0.3 cosmetic demo catalog and FX editor are replaced with the REAL
  // catalog/chain when running in JUCE. Keep HTML trial standalone unchanged.
  const css = document.createElement("style");
  css.textContent = `
    .native-toolbar{display:flex;flex-wrap:wrap;gap:7px;align-items:center}
    .native-btn{font:inherit;color:#d6e9ef;padding:8px 12px;border:1px solid #48677a;border-radius:6px;
       background:linear-gradient(180deg,#405c70 0%,#243846 47%,#0d1e29 100%);cursor:pointer;
       box-shadow:inset 0 1px 1px #93badc70,0 2px 3px #0008}
    .native-btn:hover{border-color:#62b8dc}
    .native-add-feedback{margin:9px 0 11px;padding:10px 12px;border-radius:5px;font-size:12px;
      line-height:1.55;border:1px solid #4e7789;background:#153545;color:#b9eaff}
    .native-add-feedback.error{background:#39252a;border-color:#b76b68;color:#ffe0d4}
    .native-add-feedback.waiting{background:#203849;border-color:#669abc;color:#c4ecff}
    .native-add-feedback strong{font-weight:600}
    .native-add-feedback .native-busy{display:inline-block;width:10px;height:10px;
      margin-right:7px;border:2px solid #477a97;border-top-color:#80eaff;
      border-radius:100%;animation:nativeBusySpin 1s linear infinite}
    @keyframes nativeBusySpin{to{transform:rotate(360deg)}}
    .native-btn.primary{border-color:#2dbee9;background:linear-gradient(180deg,#4cddff 0%,#0f9fd1 42%,#08688c 100%);color:#042231}
    .native-btn.warn{color:#ffd0c9;border-color:#915552}
    .native-btn.on{border-color:#4ae3fd;color:#9ceaff;background:linear-gradient(#165b79,#123142)}
    .native-panel{padding:16px 18px;max-width:1160px;margin:0 auto;color:#dcecf2;box-sizing:border-box}
    .native-hdr{display:flex;justify-content:space-between;gap:15px;align-items:start;padding:6px 0 16px}
    .native-hdr h2{font-size:19px;font-weight:500;margin:5px 0}
    .native-sub{font-size:12px;color:#8da9b9;line-height:1.6}
    .native-grid{display:grid;grid-template-columns:minmax(300px,1fr) minmax(280px,.95fr);gap:14px}
    .native-card{background:linear-gradient(160deg,#1e303c,#13222c 75%);border:1px solid #3b5361;border-radius:9px;padding:12px;
      box-shadow:inset 0 1px #7fb0ca27,0 7px 18px #0004;min-height:180px}
    .native-card h3{font-size:13px;font-weight:550;color:#7fe4fb;margin:2px 0 12px}
    .native-nodes{display:flex;flex-direction:column;gap:7px}
    .native-node{display:flex;align-items:center;gap:7px;padding:7px 8px;border:1px solid #405a69;border-radius:6px;background:#101e28}
    .native-node .node-index{color:#52c9ea;font:12px monospace;width:22px}
    .native-node .node-text{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-size:12px}
    .native-node .node-text small{display:block;color:#7196a8;font-size:10px;margin-top:3px}
    .native-node.is-bypassed{opacity:.55}
    .native-node button{padding:5px 7px;font-size:10px;min-width:28px}
    .native-search{box-sizing:border-box;display:block;width:100%;font:12px inherit;outline:0;color:#dcecf2;
        border:1px solid #4b6b79;background:#0b1923;border-radius:5px;padding:10px;margin-bottom:9px}
    .native-category{display:flex;gap:4px;flex-wrap:wrap;margin:7px 0}
    .native-category button{padding:5px 10px;font-size:11px}
    .native-scroll{max-height:400px;overflow:auto;display:flex;flex-direction:column;gap:6px}
    .native-item{display:flex;gap:8px;align-items:center;border:1px solid #324d5a;padding:8px 9px;background:#10232d;border-radius:6px}
    .native-item div{min-width:0;flex:1}
    .native-item b{display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-size:12px;font-weight:500}
    .native-item small{color:#749aab;font-size:10px}
    .native-tag{font-size:9px;border:1px solid #3f7a90;padding:3px 5px;border-radius:3px;color:#8ad6ed}
    .native-empty{border:1px dashed #3b5361;padding:18px;color:#8fafbc;text-align:center;border-radius:5px;font-size:12px}
    .native-status{padding:8px 10px;background:#10212c;border:1px solid #36546b;border-radius:5px;font-size:11px;color:#97bac8}
    @media(max-width:1150px){.native-grid{grid-template-columns:1fr}.native-hdr{flex-direction:column}}
  `;
  css.textContent += "\n    .native-modal{position:fixed;inset:0;z-index:700;display:flex;align-items:center;justify-content:center;\n      background:rgba(3,11,18,.9);backdrop-filter:none;padding:22px;box-sizing:border-box}\n    .native-modal[hidden]{display:none}\n    .native-dialog{width:min(860px,96vw);max-height:min(840px,92vh);overflow:hidden;\n      display:flex;flex-direction:column;background:linear-gradient(145deg,#243945 0%,#101e29 62%,#0c1821);\n      border:1px solid #638293;border-radius:11px;box-shadow:0 18px 80px #000c,inset 0 1px #b2d9e449}\n    .native-dialog-header{display:flex;align-items:center;justify-content:space-between;gap:15px;\n      border-bottom:1px solid #3c5360;padding:14px 17px}\n    .native-dialog-header h2{font-size:18px;font-weight:500;color:#e4f4f9;margin:4px 0}\n    .native-dialog-body{padding:14px 17px;overflow:auto;min-height:60px}\n    .native-dialog-footer{border-top:1px solid #334b58;padding:9px 17px;color:#91abb8;font-size:11px}\n    .native-dialog.small{max-width:520px}\n    .native-bar{height:12px;border:1px solid #527282;background:#0a1621;border-radius:7px;overflow:hidden;position:relative;\n      box-shadow:inset 0 2px 5px #000c}\n    .native-bar-fill{height:100%;width:0%;background:linear-gradient(180deg,#8ef6ff,#17a8da 43%,#08688e);\n      box-shadow:none;border-right:1px solid #ddffff}\n    .native-bar.unknown .native-bar-fill{width:34%;position:absolute;animation:native-indeterminate 1.65s ease-in-out infinite}\n    @keyframes native-indeterminate{0%{left:-36%}50%{left:58%}100%{left:105%}}\n    .native-progress-info{font:11px monospace;color:#a5d5e2;margin:8px 0;display:flex;justify-content:space-between;gap:9px}\n    .native-progress-info strong{font-weight:500;color:#d8f9ff}\n    #nativeScanHud{position:fixed;left:50%;transform:translateX(-50%);bottom:37px;z-index:550;\n      background:#152c3b;border:1px solid #537588;border-radius:8px;box-shadow:0 6px 20px #000a;\n      padding:9px 13px;width:min(470px,90vw)}\n    #nativeScanHud[hidden]{display:none}\n    .native-result{display:flex;align-items:center;gap:7px;background:#10222c;border:1px solid #314d5c;\n      border-radius:6px;padding:9px 11px;margin:5px 0}\n    .native-result span{flex:1;min-width:0}\n    .native-result b{display:block;font-weight:500;color:#d6edf7;font-size:12px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}\n    .native-result small{display:block;color:#88aaba;font-size:10px;margin-top:4px}\n    .native-search{font-family:inherit!important}\n";
  css.textContent += "\n    .native-insert-row{display:flex;align-items:center;gap:2px;width:100%;min-width:0}\n    .native-insert-row>.insert{flex:1;min-width:0;text-align:left;overflow:hidden;\n      text-overflow:ellipsis;white-space:nowrap;padding-left:4px;padding-right:1px}\n    .native-insert-row>.native-power{flex:0 0 21px;width:21px;height:21px;\n      font-size:14px;line-height:18px;text-align:center;border-radius:3px;\n      border:1px solid #3b6973;color:#8becb3;cursor:pointer;padding:0;\n      background:linear-gradient(#385e65,#102c30);box-shadow:inset 0 1px #91e8bd44}\n    .native-insert-row>.native-power.is-off{color:#8b9ca5;border-color:#53616a;\n      background:linear-gradient(#39414a,#171e27);box-shadow:none}\n    .native-insert-row>.native-power:focus-visible{outline:2px solid #4dd7ff}\n    .native-insert-row.is-bypassed>.insert{opacity:.54;filter:saturate(.55)}\n    .native-engine .strip[data-preview=\"1\"] .meter-fill{opacity:0!important}\n    .native-engine .strip[data-preview=\"1\"] .meter{opacity:.36}\n    .native-engine .meter-fill{transition:none}\n  ";
  // Native-only FX popover: no mixer theme/layout changes and no full-screen dimmer.
  css.textContent += `
    #nativeOrbit{z-index:980;background:transparent!important;backdrop-filter:none!important;
      display:block;padding:0;pointer-events:none}
    #nativeOrbit[hidden]{display:none}
    #nativeOrbit .native-dialog{position:fixed;box-sizing:border-box;width:342px;max-width:calc(100vw - 16px);
      max-height:calc(100vh - 16px);margin:0;pointer-events:auto;overflow:visible;
      border:1px solid #497488;border-radius:8px;
      background:linear-gradient(158deg,#203541,#12242f 75%);
      box-shadow:0 10px 32px #000b,inset 0 1px #b1f4ff20}
    #nativeOrbit .native-dialog::before{content:"";position:absolute;top:-5px;left:var(--orbit-arrow-x,24px);
      width:8px;height:8px;background:#203541;border-left:1px solid #497488;
      border-top:1px solid #497488;transform:rotate(45deg)}
    #nativeOrbit .native-dialog.orbit-above::before{top:auto;bottom:-5px;border:0;
      background:#12242f;border-right:1px solid #497488;border-bottom:1px solid #497488}
    #nativeOrbit .native-dialog-header{padding:10px 12px 7px;border-bottom:0;gap:5px}
    #nativeOrbit .native-dialog-header .eyebrow{font-size:9px;color:#65c3d8;letter-spacing:.07em}
    #nativeOrbit .native-dialog-header h2{font-size:14px;font-weight:550;margin:2px 0}
    #nativeOrbit [data-close-modal]{padding:3px 7px;min-width:27px;font-size:12px}
    #nativeOrbit .native-dialog-body{padding:0 12px 12px;overflow:auto;min-height:0}
    #nativeOrbit .native-dialog-footer{display:none}
    #nativeOrbit .orbit-target{font-size:11px;color:#a4c8d5;white-space:nowrap;overflow:hidden;
      text-overflow:ellipsis;margin-bottom:6px}
    #nativeOrbit .orbit-visual{display:flex;align-items:center;gap:10px;margin-bottom:10px;
      background:#0c1d27;border:1px solid #2a4958;border-radius:6px;padding:7px 10px}
    #nativeOrbit .orbit-graphic{width:68px;height:68px;flex:none}
    #nativeOrbit .orbit-graphic.is-reverse{transform:scaleX(-1)}
    #nativeOrbit .orbit-visual-caption{font-size:10px;color:#85b1c3;line-height:1.5}
    #nativeOrbit .orbit-visual-caption strong{display:block;font-size:12px;font-weight:500;color:#c9edf5}
    #nativeOrbit .orbit-field-label{display:block;font-size:10px;color:#a4c2d0;margin:0 0 4px}
    #nativeOrbit .native-search{height:35px;margin:0 0 9px;font-size:12px;
      padding:6px 9px;border-radius:4px}
    #nativeOrbit .orbit-grid{display:grid;grid-template-columns:1fr 1fr;gap:9px}
    #nativeOrbit .orbit-verify{display:flex;align-items:center;gap:8px;margin:1px 0 10px;
      color:#b5ccd6;font-size:10px;cursor:pointer}
    #nativeOrbit .orbit-verify input{accent-color:#40bed4;margin:0;flex:none}
    #nativeOrbit .orbit-actions{display:flex;align-items:center;gap:8px}
    #nativeOrbit .orbit-actions .native-btn{flex:1;min-height:36px;border-radius:5px;
      padding:7px;font-size:12px;font-weight:550}
    #nativeOrbit .orbit-actions .native-btn:disabled{opacity:.35;cursor:not-allowed}
    #nativeOrbit .orbit-state{display:inline-flex;gap:5px;align-items:center;font-size:10px;color:#9fb9c6;
      margin:0 0 8px}
    #nativeOrbit .orbit-state::before{content:"";width:6px;height:6px;border-radius:50%;background:#556f80}
    #nativeOrbit .orbit-state.running::before{background:#39cfbe;box-shadow:0 0 7px #2cdbc06e}
    #nativeOrbit .orbit-hint{font-size:10px;color:#819fae;margin:0}
    @media(max-height:480px){#nativeOrbit .orbit-visual{display:none}}
  `;

  css.textContent += `
    #nativeFxContext{position:fixed;z-index:950;min-width:180px;padding:4px;background:#13232e;border:1px solid #426175;border-radius:5px;box-shadow:0 9px 26px #000b;color:#dcecf2;font-size:11px}
    #nativeFxContext[hidden]{display:none}
    #nativeFxContext button{display:block;width:100%;text-align:left;background:transparent;color:inherit;border:0;border-radius:3px;padding:7px 10px;cursor:pointer;font:inherit}
    #nativeFxContext button:hover,#nativeFxContext button:focus-visible{background:#234657;color:#b5f2ff}
    #nativeFxContext .native-context-divider{height:1px;background:#334a58;margin:3px 4px}
  `;
  css.textContent += "\n    #nativeOrbit .native-dialog{width:374px;max-width:calc(100vw - 16px)}\n    #nativeOrbit .orbit-motion-view{background:#0b1e2a;border:1px solid #2f5161;\n      border-radius:6px;margin:0 0 10px;padding:7px;display:flex;align-items:center;gap:10px}\n    #nativeOrbit .orbit-map{width:132px;height:132px;flex:none}\n    #nativeOrbit .orbit-mode-help{font-size:11px;color:#9abfce;line-height:1.55}\n    #nativeOrbit .orbit-mode-help strong{display:block;color:#d8f0f7;font-weight:500;margin-bottom:4px}\n    #nativeOrbit .orbit-field-label{font-size:11px}\n    #nativeOrbit .orbit-extra-row{display:grid;grid-template-columns:1fr 1fr;gap:8px}\n    #nativeOrbit .orbit-help-note{font-size:10px;color:#87a8b9;line-height:1.4;margin:0 0 8px}\n    #nativeOrbit .material-icon{width:18px;height:18px;fill:currentColor;vertical-align:middle;flex:none}\n    #nativeOrbit .orbit-actions .native-btn{display:inline-flex;justify-content:center;align-items:center;gap:5px}\n    #nativeOrbit .orbit-map path{vector-effect:non-scaling-stroke}\n";
  document.head.appendChild(css);
  document.getElementById("engineBadge").textContent="NATIVE CH1 · ORBIT";
  const workspace = document.querySelector("#rackWorkspace");
  const mixer = document.querySelector("#mixer");
  const currentTab = () => document.querySelector(".nav button.selected")?.dataset.view;
  const fxCategories = ["All","Dynamics","EQ","Delay","Reverb","Other"];
  const classFor = item => {
    const s = (item.category+" "+item.name).toLowerCase();
    if(/compress|limit|gate|dynam|de.?ess/.test(s)) return "Dynamics";
    if(/eq|equal|filter/.test(s)) return "EQ";
    if(/delay|echo/.test(s)) return "Delay";
    if(/verb|room|hall/.test(s)) return "Reverb";
    return "Other";
  };
  function cardHeader(title,subtitle) {
    return `<div class="native-hdr"><div><span class="eyebrow">AR MIX · NATIVE VST3 ENGINE</span>
      <h2>${escapeHtml(title)}</h2><div class="native-sub">${escapeHtml(subtitle)}</div></div></div>`;
  }
  function renderRack(){
  if(!workspace || currentTab()!=="FX RACK") return;
  workspace.innerHTML=`<div class="native-panel" data-live-native="1">
    ${cardHeader("Live FX Chain · Vocal Input","Arrange your real VST3 effects here. Use ＋ ADD FX to choose from your installed catalog.")}
    <div class="native-toolbar" style="margin-bottom:12px">
      <button class="native-btn primary" data-plugin-picker="1">＋ ADD FX</button>
      <button class="native-btn" data-plugin-manager="1">PLUGIN MANAGER</button>
      <button class="native-btn" data-my-sounds="1">SAVE / RECALL</button>
      <span class="native-sub">${state.chain.length}/8 live effects · Real-time processor chain (Channel 1)</span>
    </div>
    <div class="native-card" style="max-width:920px">
      <h3>MIC INPUT → PROCESSING → MAIN OUTPUT</h3>
      <div class="native-nodes">
        <div class="native-status">INPUT 1 / MICROPHONE</div>
        ${state.chain.map((p,i)=>`<div class="native-node ${p.bypass?"is-bypassed":""}">
          <span class="node-index">${String(i+1).padStart(2,"0")}</span>
          <span class="node-text">${escapeHtml(p.name)}<small>${p.latency||0} samples processing latency</small></span>
          <button class="native-btn" data-native-fx="editor" data-slot="${i}">EDIT</button>
          <button class="native-btn ${p.bypass?"":"on"}" data-native-fx="bypass" data-slot="${i}" aria-pressed="${!p.bypass}" title="Instantly ${p.bypass?"enable":"bypass"} this plugin without stopping audio">⏻ ${p.bypass?"OFF · ENABLE":"ON · BYPASS"}</button>
          <button class="native-btn" data-native-fx="up" data-slot="${i}" ${i===0?"disabled":""}>↑</button>
          <button class="native-btn" data-native-fx="down" data-slot="${i}" ${i===state.chain.length-1?"disabled":""}>↓</button>
          <button class="native-btn warn" data-native-fx="remove" data-slot="${i}">×</button>
        </div>`).join("")}
        ${state.chain.length===0?'<div class="native-empty">No FX loaded yet · Click ＋ ADD FX to insert a real VST3.</div>':""}
        <button class="native-btn" data-plugin-picker="1" style="align-self:start">＋ ADD NEXT EFFECT</button>
        <div class="native-status">MASTER OUTPUT / STREAM</div>
      </div>
    </div>
    <p class="native-sub" style="margin-top:13px">Bypass can change without stopping the audio callback. Live chain edits briefly restart processing; avoid editing FX while performing. Bypass is real-time safe.</p>
  </div>`;
}
  function createDialog(id,title,small=false){
  let overlay=document.getElementById(id);
  if(overlay)return overlay;
  overlay=document.createElement("div");overlay.id=id;overlay.className="native-modal";overlay.hidden=true;
  overlay.innerHTML=`<div class="native-dialog ${small?"small":""}" role="dialog" aria-modal="true">
    <div class="native-dialog-header"><div><span class="eyebrow">AR MIX STUDIO</span><h2>${title}</h2></div>
    <button class="native-btn" data-close-modal="${id}" aria-label="Close">✕</button></div>
    <div class="native-dialog-body"></div><div class="native-dialog-footer"></div>
  </div>`;
  document.body.appendChild(overlay);
  return overlay;
}
  function showDialog(id){const d=document.getElementById(id);if(d){d.hidden=false;d.querySelector("input")?.focus()}}
  function hideDialog(id){const d=document.getElementById(id);if(d)d.hidden=true}
  let orbitAnchor=null;
  function hideOrbit(){
    const d=document.getElementById("nativeOrbit");
    if(d)d.hidden=true;
    orbitAnchor=null;
  }
  function positionOrbit(){
    const overlay=document.getElementById("nativeOrbit");
    if(!overlay||overlay.hidden)return;
    if(!orbitAnchor?.isConnected){hideOrbit();return;}
    const panel=overlay.querySelector(".native-dialog");
    const anchor=orbitAnchor.getBoundingClientRect();
    const margin=8;
    const width=panel.offsetWidth;
    const height=panel.offsetHeight;
    const left=Math.max(margin,Math.min(anchor.left,window.innerWidth-width-margin));
    const below=anchor.bottom+9;
    const above=anchor.top-height-9;
    const flip=below+height+margin>window.innerHeight && above>=margin;
    const top=Math.max(margin,Math.min(flip?above:below,window.innerHeight-height-margin));
    panel.style.left=left+"px";
    panel.style.top=top+"px";
    panel.classList.toggle("orbit-above",flip);
    panel.style.setProperty("--orbit-arrow-x",
      Math.max(12,Math.min(anchor.left+Math.min(25,anchor.width/2)-left,width-22))+"px");
  }
  const motionModes=[
    ["Circle","Same distance in every direction"],
    ["Front–Back ellipse","Far ahead/behind · close left/right"],
    ["Left–Right ellipse","Close ahead/behind · far left/right"],
    ["Circle · Rise/Fall","Round orbit with vertical motion"],
    ["Front–Back · Rise/Fall","Long front/back with vertical motion"],
    ["Left–Right · Rise/Fall","Wide left/right with vertical motion"]
  ];
  const iconSvg=(name)=>{
    // Google Material Icons vector paths (Apache-2.0), self-contained; no font/network.
    const paths={
      close:"M19 6.41 17.59 5 12 10.59 6.41 5 5 6.41 10.59 12 5 17.59 6.41 19 12 13.41 17.59 19 19 17.59 13.41 12z",
      play:"M8 5v14l11-7z",
      stop:"M6 6h12v12H6z"
    };
    return '<svg class="material-icon" viewBox="0 0 24 24" aria-hidden="true"><path d="'+paths[name]+'"/></svg>';
  };
  const needsDistance=mode=>mode===1||mode===2||mode===4||mode===5;
  const needsHeight=mode=>mode>=3&&mode<=5;
  function motionSvgPath(points){
    if(!Array.isArray(points)||points.length<4)return "";
    return points.map((p,i)=>{
      const x=80+53*Math.max(-1.2,Math.min(1.2,Number(p.x)||0));
      const y=80-53*Math.max(-1.2,Math.min(1.2,Number(p.y)||0));
      return (i?"L":"M")+x.toFixed(2)+" "+y.toFixed(2);
    }).join(" ");
  }
  let animationActive=false,lastMotionFrame=0;
  function animateOrbitPreview(now){
    const dlg=document.getElementById("nativeOrbit");
    if(!dlg||dlg.hidden){animationActive=false;return;}
    const animating=orbit.active&&orbit.activeSlot===orbit.slot&&
      orbit.selectedShape===orbit.shape;
    if(!animating){animationActive=false;return;}
    if(now-lastMotionFrame>=32){
      lastMotionFrame=now;
      const points=orbit.motionPath||[];
      if(points.length>=4){
        const elapsed=(now-(orbit.visualTime||now))/1000;
        const period=Math.max(2,Math.min(180,Number(orbit.appliedPeriod)||20));
        const direction=orbit.appliedReverse?-1:1;
        const phase=((Number(orbit.visualPhase||0)+direction*elapsed/period)%1+1)%1;
        const exact=phase*(points.length-1),i=Math.min(points.length-2,Math.floor(exact)),f=exact-i;
        const a=points[i],b=points[i+1];
        const mix=k=>(Number(a[k])||0)*(1-f)+(Number(b[k])||0)*f;
        const dot=dlg.querySelector("#nativeOrbitDot");
        if(dot){dot.setAttribute("cx",String(80+53*mix("x")));
          dot.setAttribute("cy",String(80-53*mix("y")));}
        const height=dlg.querySelector("#nativeOrbitHeightDot");
        if(height)height.setAttribute("cy",String(80-52*mix("z")));
      }
    }
    requestAnimationFrame(animateOrbitPreview);
  }
  function renderOrbit(){
    const dlg=document.getElementById("nativeOrbit");
    if(!dlg)return;
    const options=Array.isArray(orbit.parameters)?orbit.parameters:[];
    const selected=orbit.active&&orbit.activeSlot===orbit.slot?orbit.activeParameter:
      (orbit.selection>=0?orbit.selection:orbit.preferredParameter);
    const shape=Number.isInteger(orbit.selectedShape)?orbit.selectedShape:0;
    const running=orbit.active&&orbit.activeSlot===orbit.slot;
    const needsD=needsDistance(shape),needsE=needsHeight(shape);
    const unit=orbit.distanceUnit==="m"?"m":"%";
    const meter=unit==="m";
    const minDist=Number.isFinite(+orbit.distanceMin)?+orbit.distanceMin:0;
    const maxDist=Number.isFinite(+orbit.distanceMax)?+orbit.distanceMax:100;
    const validSetpoints=Number.isFinite(orbit.nearSetpoint)&&Number.isFinite(orbit.farSetpoint)&&
      orbit.farSetpoint>=minDist&&orbit.farSetpoint<=maxDist&&
      (!needsD||orbit.nearSetpoint<=orbit.farSetpoint)&&
      (!needsD||orbit.nearSetpoint>=minDist);
    const distanceSel=orbit.distanceSelection>=0?orbit.distanceSelection:
      (running&&orbit.shape===shape?orbit.distanceIndex:orbit.preferredDistance);
    const elevationSel=orbit.elevationSelection>=0?orbit.elevationSelection:
      (running&&orbit.shape===shape?orbit.elevationIndex:orbit.preferredElevation);
    const valid=options.some(p=>p.index===selected);
    const distValid=(!needsD&&distanceSel<0)||
      options.some(p=>p.index===distanceSel&&p.index!==selected);
    const elevValid=!needsE||options.some(p=>p.index===elevationSel&&p.index!==selected&&p.index!==distanceSel);
    const allValid=valid&&distValid&&elevValid&&validSetpoints&&orbit.revision!==null;
    const list=(id,sel)=>`<select class="native-search" id="${id}">
      <option value="-1" ${sel<0?"selected":""}>Choose parameter…</option>
      ${options.map(p=>`<option value="${p.index}" ${p.index===sel?"selected":""}>
        ${escapeHtml(p.name)} · ${escapeHtml(p.minText)} → ${escapeHtml(p.maxText)}
      </option>`).join("")}</select>`;
    const modeName=motionModes[shape]?.[0]||motionModes[0][0];
    const modeHelp=motionModes[shape]?.[1]||motionModes[0][1];
    const ready=running&&orbit.shape===shape;
    dlg.querySelector(".native-dialog-header .eyebrow").textContent="FX "+(orbit.slot+1)+" · CHANNEL 1";
    dlg.querySelector(".native-dialog-header h2").textContent="Auto Orbit";
    dlg.querySelector(".native-dialog").setAttribute("aria-modal","false");
    dlg.querySelector(".native-dialog-body").innerHTML=`
      <div class="orbit-target">${escapeHtml(orbit.plugin||"Loading plugin…")}</div>
      <div class="orbit-motion-view">
        <svg class="orbit-map" viewBox="0 0 160 160" role="img" aria-label="Top-down spatial path preview">
          <path d="M80 10V150M10 80H150" stroke="#29424d" fill="none"/>
          <path id="nativeOrbitPath" d="${motionSvgPath(orbit.motionPath)}"
            stroke="#43ccdc" stroke-width="2.5" fill="none" stroke-linecap="round"/>
          <circle cx="80" cy="80" r="9" fill="#2c5964" stroke="#9ed8e6"/>
          <path d="M77 81h6m-3-4v8" stroke="#c4f2f6" stroke-width="1.5"/>
          <circle id="nativeOrbitDot" cx="80" cy="27" r="5.5" fill="#90fcf1"
            stroke="#173641" stroke-width="2"/>
          <path d="M147 59v42" stroke="#547580" stroke-width="2"/>
          <circle id="nativeOrbitHeightDot" cx="147" cy="80" r="4" fill="#fec67f"/>
          <text x="80" y="9" text-anchor="middle" fill="#698fa0" font-size="10">FRONT</text>
        </svg>
        <div class="orbit-mode-help">
          <strong>${escapeHtml(modeName)}</strong>
          ${escapeHtml(modeHelp)}
          <div class="orbit-state ${running?"running":""}">
            ${running?(orbit.shape===shape?"RUNNING":"PREVIEW · UPDATE"):"PREVIEW"}
          </div>
        </div>
      </div>
      <label class="orbit-field-label" for="nativeOrbitShape">Path</label>
      <select class="native-search" id="nativeOrbitShape">
        ${motionModes.map((m,i)=>`<option value="${i}" ${shape===i?"selected":""}>${escapeHtml(m[0])}</option>`).join("")}
      </select>
      <label class="orbit-field-label" for="nativeOrbitParameter">Azimuth · 360°</label>
      ${options.length?list("nativeOrbitParameter",selected):'<div class="native-empty">Reading plugin parameters…</div>'}
      <label class="orbit-field-label" for="nativeOrbitDistance">Distance parameter</label>
      ${list("nativeOrbitDistance",distanceSel)}
      <div class="orbit-grid">
        ${needsD?`<div><label class="orbit-field-label" for="nativeOrbitNear">Near (${unit})</label>
          <input class="native-search" id="nativeOrbitNear" type="number"
            min="${minDist}" max="${maxDist}" step="${meter?0.1:1}"
            value="${orbit.nearSetpoint}"></div>`:""}
        <div><label class="orbit-field-label" for="nativeOrbitFar">${needsD?"Far":"Distance"} (${unit})</label>
          <input class="native-search" id="nativeOrbitFar" type="number"
            min="${minDist}" max="${maxDist}" step="${meter?0.1:1}"
            value="${orbit.farSetpoint}"></div>
      </div>
      <p class="orbit-help-note">${meter?
         "Metres from the VST3 Distance parameter.":
         "VST3 parameter (0–100%). No physical metres inferred."}</p>
      ${needsE?`<label class="orbit-field-label" for="nativeOrbitElevation">Elevation · Down ↔ Up</label>
        ${list("nativeOrbitElevation",elevationSel)}`:""}
      <div class="orbit-grid">
        <div><label class="orbit-field-label" for="nativeOrbitSeconds">Seconds / turn</label>
          <input class="native-search" id="nativeOrbitSeconds" type="number" min="2" max="180" step="1"
          value="${Number(orbit.periodSeconds)||20}"></div>
        <div><label class="orbit-field-label" for="nativeOrbitReverse">Direction</label>
          <select class="native-search" id="nativeOrbitReverse">
            <option value="false" ${orbit.reverse?"":"selected"}>↻ Forward</option>
            <option value="true" ${orbit.reverse?"selected":""}>↺ Reverse</option>
          </select></div>
      </div>
      ${needsE?'<p class="orbit-help-note">Elevation requires an automatable VST3 parameter.</p>':""}
      <div class="orbit-actions">
        <button class="native-btn primary" data-orbit-start="1"
          ${allValid?"":"disabled"}>
          ${iconSvg("play")}${running?"UPDATE":"START"}</button>
        <button class="native-btn ${running?"on":""}" data-orbit-stop="1" ${running?"":"disabled"}>
          ${iconSvg("stop")}STOP</button>
      </div>`;
    if(!dlg.hidden){
      positionOrbit();
      if(ready&&!animationActive){animationActive=true;
        requestAnimationFrame(animateOrbitPreview);}
    }
  }
  function openOrbit(slot,anchor){
    const fx=state.chain[slot];
    if(!fx)return;
    orbitAnchor=anchor?.isConnected?anchor:document.querySelector(
      '.native-insert-row [data-native-fx="editor"][data-slot="'+slot+'"],'+
      '.native-node [data-native-fx="editor"][data-slot="'+slot+'"]');
    if(!orbitAnchor)return;
    orbit.slot=slot;
    orbit.plugin=fx.name||"";
    orbit.parameters=[];
    orbit.selection=-1;
    orbit.revision=null;
    orbit.initialized=false;
    orbit.selectedShape=orbit.active&&orbit.activeSlot===slot?orbit.shape:0;
    orbit.distanceSelection=-1;
    orbit.elevationSelection=-1;
    orbit.motionPath=[];
    const panel=document.getElementById("nativeOrbit");
    panel.hidden=false;
    renderOrbit();
    send("orbitInspect",{slot,shape:orbit.selectedShape});
  }
  function pluginRows(items,limit=180){
  return items.length?items.slice(0,limit).map(p=>`<div class="native-result">
    <span><b title="${escapeHtml(p.path)}">${escapeHtml(p.name)}</b>
      <small>${escapeHtml(p.vendor||"Unknown vendor")} · ${escapeHtml(p.category||"Audio effect")}</small></span>
    <span class="native-tag" style="flex:0">VST3</span>
    <button class="native-btn primary" data-native-add="${escapeHtml(p.id)}"
      ${state.chain.length>=8||state.adding?"disabled":""}>＋ ADD</button></div>`).join("")
    :'<div class="native-empty">No matching installed VST3 effects. Open Plugin Manager to scan your folders.</div>';
}
  function filterPlugins(){
  return state.catalog.filter(p=>state.selectedCategory==="All"||classFor(p)===state.selectedCategory)
    .filter(p=>(p.name+" "+p.vendor+" "+p.category).toLowerCase().includes(state.filter.toLowerCase()));
}
  function renderPicker(){
  const dlg=document.getElementById("nativeFxSelector");
  if(!dlg)return;
  dlg.querySelector(".native-dialog-body").innerHTML=`
    <div class="native-sub" style="margin-bottom:12px">SELECT EFFECT → INSERT INTO LIVE VOCAL FX RACK</div>
    <input class="native-search" id="nativeVstSearch" placeholder="Search by plugin name, developer or category..."
      value="${escapeHtml(state.filter)}">
    <div class="native-category">${fxCategories.map(c=>`<button class="native-btn ${c===state.selectedCategory?"on":""}" data-native-category="${c}">${c}</button>`).join("")}</div>
    <div id="nativeAddFeedback" aria-live="polite" role="status"
      class="native-add-feedback ${state.addError?"error":state.adding?"waiting":""}"
      style="${state.addError||state.adding?"":"display:none"}">
      ${state.adding?`<span class="native-busy"></span>Loading ${escapeHtml(state.pendingPlugin)}…
        Initializing VST3; this may take a moment.`:
      state.addError?`<strong>FX NOT LOADED:</strong> ${escapeHtml(state.addError)}
        <div style="margin-top:4px">Open PLUGINS to rescan or SIGNAL CHECK for diagnostic logs.</div>`:""}
    </div>
    <div style="max-height:430px;overflow:auto">${pluginRows(filterPlugins())}</div>`;
  dlg.querySelector(".native-dialog-footer").innerHTML=`${state.catalog.length} available plugins · <a href="#" data-plugin-manager="1" style="color:#7fdfff">OPEN PLUGIN MANAGER</a> for scanning and import`;
}
  function openPicker(){renderPicker();showDialog("nativeFxSelector")}
  function scanProgressHtml(){
  const p=state.progress||{};
  const count=p.done||0,total=p.total||0,found=p.found||0;
  const known=p.phase==="inspecting"&&total>0;
  const percentage=known?Math.round(count/total*100):null;
  return `<div class="native-progress-info"><span>${p.phase==="discovering"?"Discovering plugin files":
    p.phase==="inspecting"?"Inspecting VST3 metadata":"Scan completed"}</span>
    <strong>${known?`${count} / ${total} · ${percentage}%`:"Working…"}</strong></div>
    <div class="native-bar ${known?"":"unknown"}"><div class="native-bar-fill" style="${known?`width:${percentage}%`:""}"></div></div>
    <div class="native-progress-info"><span>${escapeHtml(p.current||"")}</span><span>${found} effects found</span></div>`;
}
  function renderManager(){
  const dlg=document.getElementById("nativePluginManager");
  if(!dlg)return;
  dlg.querySelector(".native-dialog-body").innerHTML=`
    <div class="native-sub" style="margin-bottom:12px">MANAGE INSTALLED VST3 PLUGINS · SCANNING RUNS OFF THE AUDIO CALLBACK</div>
    <div class="native-toolbar" style="margin-bottom:12px">
      <button class="native-btn primary" data-native-action="importVst3" ${state.scanning?"disabled":""}>＋ ADD VST · KEEP IN STUDIO</button>
      <button class="native-btn" data-native-action="importVst3InPlace" ${state.scanning?"disabled":""}>LOAD IN PLACE</button>
      <button class="native-btn" data-native-action="addScanFolder" ${state.scanning?"disabled":""}>ADD REFERENCE FOLDER</button>
      <button class="native-btn" data-native-action="scanVst3" ${state.scanning?"disabled":""}>SCAN SYSTEM (OPTIONAL)</button>
    </div>
    <div class="native-card" style="min-height:0;margin-bottom:12px">
      <h3>${state.scanning?"SCANNING PLUGINS":"PLUGIN SCANNER"}</h3>
      <div id="nativeManagerProgress">${state.scanning?scanProgressHtml():
        `<div class="native-sub">${escapeHtml(state.message)} · ${state.catalog.length} cached effects</div>`}</div>
    </div>
    <div class="native-sub">Plugin catalog (${state.catalog.length}) — stored between launches</div>
    <div style="max-height:260px;overflow:auto">${state.catalog.length?
      state.catalog.slice(0,120).map(p=>`<div class="native-result"><span><b>${escapeHtml(p.name)}</b>
        <small>${escapeHtml(p.vendor||"Unknown")} · ${escapeHtml(p.path)}</small></span></div>`).join("")
        :'<div class="native-empty">No plugins indexed yet. Scan your standard folders or import a .vst3 file.</div>'}</div>`;
  dlg.querySelector(".native-dialog-footer").textContent=
    "VST2 DLL is not VST3. Scanning third-party binaries is not yet crash-isolated.";
}
  function updateProgress(){
  const p=state.progress||{};
  const hud=document.getElementById("nativeScanHud");
  hud.hidden=!p.active;
  if(p.active)hud.innerHTML=`<div style="font-size:11px;color:#b4e7f6;margin-bottom:4px">VST3 SCANNING · ${state.catalog.length} cached effects</div>${scanProgressHtml()}`;
  const mgr=document.getElementById("nativePluginManager");
  if(mgr&&!mgr.hidden){
    const host=mgr.querySelector("#nativeManagerProgress");
    if(host)host.innerHTML=p.active?scanProgressHtml():`<div class="native-sub">${escapeHtml(state.message)}</div>`;
  }
}
  function renderMySounds(){
    const dlg=document.getElementById("nativeMySounds");
    if(!dlg)return;
    const muted=!state.monitor;
    dlg.querySelector(".native-dialog-body").innerHTML=`
      <div class="native-sub" style="margin-bottom:10px">
        Native .armix presets save the REAL VST3 binary settings, insert order,
        bypass, Channel 1 fader/PAN/mute and master. Other demo channels are NOT included.
        Devices and output routing are never changed by recall.
      </div>
      <div class="native-status" style="margin-bottom:10px;color:${muted?"#8fe5b8":"#f8c38d"}">
        ${muted?"OUTPUT MUTED · Save and recall available."
          :"LIVE OUTPUT ACTIVE · Recall uses a short de-click fade. Mute before SAVE."}
        ${muted?"":'<button class="native-btn" data-preset-mute="1" style="margin-left:7px">MUTE OUTPUT</button>'}
      </div>
      <div style="display:flex;gap:6px;margin-bottom:12px">
        <input id="nativePresetName" class="native-search" style="margin:0;min-width:0;flex:1"
           maxlength="56" placeholder="Name your sound, e.g. Warm Karaoke" aria-label="Native preset name">
        <button class="native-btn primary" data-preset-save="1" ${muted?"":"disabled"}>SAVE SOUND</button>
      </div>
      <div class="native-sub" style="margin-bottom:7px">SAVED NATIVE SOUNDS · ${state.presets.length}</div>
      <div style="max-height:250px;overflow:auto">${state.presets.length?
        state.presets.map(n=>`<div class="native-result">
          <span><b>${escapeHtml(n)}</b><small>.armix · full vendor VST3 state</small></span>
          <button class="native-btn" data-preset-load="${escapeHtml(n)}">RECALL LIVE</button>
        </div>`).join("") :
        '<div class="native-empty">No native sounds saved. Mute output, then save the current live FX chain.</div>'}
      </div>`;
    dlg.querySelector(".native-dialog-footer").textContent=
      "Live recall: prepared VST3 chain switches at a callback boundary with a brief fade-down/up (not dual-graph seamless). SAVE requires mute. Output routing is unchanged.";
  }
  function openMySounds(){
    renderMySounds();
    showDialog("nativeMySounds");
    send("presetList");
  }
  function initModals(){
  createDialog("nativeFxSelector","Add VST3 Effect");
  createDialog("nativeMySounds","My Sounds · Native Presets",true);
  createDialog("nativeOrbit","Auto Orbit",true);
  const orbitClose=document.querySelector('#nativeOrbit [data-close-modal]');
  if(orbitClose)orbitClose.innerHTML=iconSvg("close");
  createDialog("nativePluginManager","Plugin Manager");
  const hud=document.createElement("div");hud.id="nativeScanHud";hud.hidden=true;document.body.appendChild(hud);
}
  function patchMixer(){
  if(!mixer)return;
  const tracks=mixer.querySelectorAll(".strip");
  tracks.forEach((track,i)=>{
    // The v0.8 Master uses .master-fx-rack, not .rack. Handle it first.
    if(track.classList.contains("master-strip")){
      track.dataset.preview="1";
      track.title="Master FX and master PAN are visual previews; Master fader and meters are native";
      track.querySelectorAll("[data-master-insert],[data-master-power]").forEach(x=>{
        x.disabled=true;x.title="Preview only — Master FX DSP not yet implemented";
      });
      const pan=track.querySelector('[data-pan="master"]');
      if(pan){pan.removeAttribute("tabindex");pan.style.pointerEvents="none";pan.title="Master PAN DSP not yet implemented";}
      const marker=track.querySelector(".master-info-kicker span");
      if(marker && marker.textContent!=="NATIVE PEAKS")marker.textContent="NATIVE PEAKS";
      return;
    }
    const rack=track.querySelector(".rack");
    if(!rack)return;
    if(i!==0){
      rack.dataset.preview="1";
      track.dataset.preview="1";
      track.querySelectorAll(".meter-peak").forEach(x=>x.style.display="none");
      track.title=`${track.querySelector(".strip-name")?.textContent?.trim()||"Channel"} — visual preview only; native I/O not wired`;
      return;
    }
    const signature=state.chain.map(p=>p.id+"|"+p.bypass).join(",");
    if(rack.dataset.nativeSig===signature)return;
    rack.dataset.nativeSig=signature;
    rack.innerHTML=state.chain.slice(0,5).map((p,i)=>
      `<div class="insert-slot native-insert-row ${p.bypass?"is-bypassed":""}">
        <button class="insert" data-native-fx="editor" data-slot="${i}"
          title="Open ${escapeHtml(p.name)}" aria-label="Edit ${escapeHtml(p.name)}">
          <span class="insert-name">${escapeHtml(p.name)}</span></button>
        <button type="button" class="fx-power native-power ${p.bypass?"is-bypassed is-off":""}"
          data-native-fx="bypass" data-slot="${i}" aria-pressed="${!p.bypass}"
          title="${p.bypass?"Enable":"Bypass"} ${escapeHtml(p.name)}">⏻</button>
      </div>`).join("")+
      `<div class="insert-slot"><button class="insert empty" data-plugin-picker="1"
          ${state.chain.length>=8?"disabled":""} aria-label="Add VST3 to Channel 1">
          <span class="insert-add-glyph" aria-hidden="true">＋</span>
          <span class="insert-name">${state.chain.length>=8?"FX FULL":"ADD FX"}</span>
      </button></div>`;
  });
}
  let renderPending = false;
  function syncView(){
    if(renderPending) return;
    renderPending=true;
    queueMicrotask(()=>{
      renderPending=false;
      if(currentTab()==="FX RACK"){
        if(workspace.firstElementChild?.dataset.liveNative!=="1")renderRack();
      }
      patchMixer();
    });
  }
  new MutationObserver(()=>syncView()).observe(workspace,{childList:true});
  // renderMaster() replaces descendants without replacing the mixer root.
  // Reapply the disabled native-only controls after each Master redraw.
  new MutationObserver(()=>syncView()).observe(mixer,{childList:true,subtree:true});
  const repaint=()=>{
    if(currentTab()==="FX RACK") renderRack();
    patchMixer();
  };


  // Delegated host-side FX context menu; no extra buttons or DSP work.
  const fxContext=document.createElement("div");
  fxContext.id="nativeFxContext";fxContext.hidden=true;
  fxContext.setAttribute("role","menu");
  fxContext.setAttribute("aria-label","FX actions");
  document.body.appendChild(fxContext);
  let contextSlot=-1, contextAnchor=null;
  function closeFxContext(){fxContext.hidden=true;contextSlot=-1;contextAnchor=null;}
  function openFxContext(slot,x,y,anchor){
    if(!state.chain[slot])return;
    contextSlot=slot;
    contextAnchor=anchor;
    const p=state.chain[slot];
    fxContext.innerHTML='<button role="menuitem" data-context-fx="editor">Open '+escapeHtml(p.name)+'</button>'+
      '<button role="menuitem" data-context-fx="orbit">Auto Orbit</button>'+
      '<div class="native-context-divider" role="separator"></div>'+
      '<button role="menuitem" data-context-fx="bypass">'+(p.bypass?'Enable':'Bypass')+' FX</button>';
    fxContext.hidden=false;
    fxContext.style.left=Math.max(4,Math.min(x,window.innerWidth-fxContext.offsetWidth-4))+'px';
    fxContext.style.top=Math.max(4,Math.min(y,window.innerHeight-fxContext.offsetHeight-4))+'px';
    fxContext.querySelector("button")?.focus({preventScroll:true});
  }
  document.addEventListener("contextmenu",e=>{
    // Suppress browser/IE/WebView2 default menus throughout the ArSonMix canvas.
    // Other native windows (including vendor plugin editors) are unaffected.
    e.preventDefault();
    const el=e.target.closest('[data-native-fx="editor"]');
    if(!el||!el.closest(".native-insert-row,.native-node")){
      closeFxContext();
      return;
    }
    const slot=Number(el.dataset.slot);
    if(!Number.isInteger(slot)||slot<0||slot>=state.chain.length){
      closeFxContext();
      return;
    }
    e.stopImmediatePropagation();
    hideOrbit();
    openFxContext(slot,e.clientX,e.clientY,el);
  },true);
  document.addEventListener("click",e=>{
    const item=e.target.closest("#nativeFxContext [data-context-fx]");
    if(!item){if(!fxContext.hidden)closeFxContext();return;}
    e.preventDefault();e.stopImmediatePropagation();
    const slot=contextSlot,action=item.dataset.contextFx,anchor=contextAnchor;
    closeFxContext();
    if(!state.chain[slot])return;
    if(action==="orbit")openOrbit(slot,anchor);
    else if(action==="editor")send("fxEditor",{slot});
    else if(action==="bypass")send("fxBypass",{slot,value:!state.chain[slot].bypass});
  },true);
  document.addEventListener("pointerdown",e=>{
    const popup=document.getElementById("nativeOrbit");
    if(!popup||popup.hidden)return;
    if(!e.target.closest("#nativeOrbit .native-dialog") &&
       !e.target.closest("#nativeFxContext")) hideOrbit();
  },true);
  document.addEventListener("keydown",e=>{
    if(e.key==="Escape" && !document.getElementById("nativeOrbit").hidden){
      e.preventDefault();hideOrbit();return;
    }
    if(fxContext.hidden)return;
    if(e.key==="Escape"){e.preventDefault();closeFxContext();}
    if(e.key==="ArrowDown"||e.key==="ArrowUp"){
      e.preventDefault();
      const items=[...fxContext.querySelectorAll("button")];
      const i=items.indexOf(document.activeElement);
      items[(i+(e.key==="ArrowDown"?1:items.length-1))%items.length]?.focus();
    }
  },true);
  window.addEventListener("blur",closeFxContext);
  window.addEventListener("resize",()=>{closeFxContext();positionOrbit();});
  document.addEventListener("scroll",e=>{
    closeFxContext();
    if(!e.target?.closest?.("#nativeOrbit .native-dialog"))hideOrbit();
  },true);
  // Capture native controls before the legacy v0.3 demo handlers.
  document.addEventListener("click", function(e){
  const el=e.target.closest("[data-native-action],[data-native-add],[data-native-fx],[data-fxpower],[data-master-insert],[data-master-power],[data-native-category],[data-go-native-rack],[data-insert],[data-plugin-picker],[data-plugin-manager],[data-my-sounds],[data-preset-mute],[data-preset-save],[data-preset-load],[data-close-modal],[data-orbit-start],[data-orbit-stop]");
  if(!el)return;
  e.preventDefault();e.stopImmediatePropagation();

  if(el.dataset.closeModal){if(el.dataset.closeModal==="nativeOrbit")hideOrbit();else hideDialog(el.dataset.closeModal);return;}
  if(el.hasAttribute("data-orbit-start")){
    const raw=document.getElementById("nativeOrbitParameter")?.value;
    const param=raw===""||raw===undefined?NaN:Number(raw);
    const seconds=Number(document.getElementById("nativeOrbitSeconds")?.value);
    const reverse=document.getElementById("nativeOrbitReverse")?.value==="true";
    const shape=orbit.selectedShape;
    const distanceIndex=Number(document.getElementById("nativeOrbitDistance")?.value??-1);
    const elevationIndex=Number(document.getElementById("nativeOrbitElevation")?.value??-1);
    const requiredDistance=needsDistance(shape);
    const requiredHeight=needsHeight(shape);
    const far=orbit.farSetpoint;
    const near=requiredDistance?orbit.nearSetpoint:Math.min(orbit.nearSetpoint,far);
    if(orbit.revision===null||!Number.isInteger(param)||
       !Number.isFinite(seconds)||seconds<2||seconds>180||
       !Number.isFinite(near)||!Number.isFinite(far)||
       near<orbit.distanceMin||far>orbit.distanceMax||near>far||
       (requiredDistance&&distanceIndex<0)||(requiredHeight&&elevationIndex<0)){
      toStatus("Choose a valid VST3 parameter and revolution period (2–180 seconds).");return;
    }
    send("orbitStart",{slot:orbit.slot,revision:orbit.revision,
      pluginId:state.chain[orbit.slot]?.id||"",parameterIndex:param,seconds,reverse,
      shape,distanceIndex,elevationIndex,
      nearDistance:near,farDistance:far,distanceUnit:orbit.distanceUnit});return;
  }
  if(el.hasAttribute("data-orbit-stop")){send("orbitStop",{slot:orbit.slot});return;}
  if(el.hasAttribute("data-master-insert")||el.hasAttribute("data-master-power")){
    toStatus("Master FX is a visual preview; real processing is Channel 1.");return;
  }
  if(el.hasAttribute("data-fxpower")){
    const [channel,slot]=el.dataset.fxpower.split(":").map(Number);
    if(channel===0)send("fxBypass",{slot,value:!state.chain[slot]?.bypass});
    else toStatus("Visual preview only. Native FX available on Channel 1.");
    return;
  }
  if(el.hasAttribute("data-my-sounds")){openMySounds();return;}
  if(el.hasAttribute("data-preset-mute")){
    send("monitor",{value:false});state.monitor=false;renderMySounds();return;
  }
  if(el.hasAttribute("data-preset-save")){
    const input=document.getElementById("nativePresetName");
    const name=(input?.value||"").trim();
    if(!name)return;
    input.disabled=true;
    requestAnimationFrame(()=>requestAnimationFrame(()=>{
      send("presetSave",{name});
      input.disabled=false;
    }));
    return;
  }
  if(el.hasAttribute("data-preset-load")){
    const name=el.dataset.presetLoad;
    if(!name)return;
    if(!confirm('Recall native sound "'+name+'"? The mixer will use a short de-click fade when switching FX chains.'))return;
    const notice=document.querySelector("#nativeMySounds .native-status");
    if(notice)notice.textContent="Preparing new VST3 instances and binary states. Live audio remains on the current graph until the new plan is ready…";
    requestAnimationFrame(()=>requestAnimationFrame(()=>send("presetLoad",{name})));
    return;
  }
  if(el.hasAttribute("data-plugin-manager")){
    renderManager();showDialog("nativePluginManager");return;
  }
  if(el.hasAttribute("data-plugin-picker")){
    openPicker();return;
  }
  if(el.hasAttribute("data-insert")){
    const article=el.closest(".strip");
    const idx=Number(article?.dataset.strip??0);
    if(idx!==0){
      const toast=document.querySelector("#toast");
      if(toast){toast.textContent="Native VST3 is currently available on Channel 1 only.";toast.classList.add("show");
        setTimeout(()=>toast.classList.remove("show"),2200);}
      return;
    }
    openPicker();return;
  }
  if(el.hasAttribute("data-go-native-rack")){
    document.querySelector('.nav button[data-view="FX RACK"]')?.click();return;
  }
  const action=el.dataset.nativeAction;
  if(action){
    if(action==="monitor"){send("monitor",{value:!state.monitor});return;}
    if(action==="audioSetup"){send("audioSetup");return;}
    send(action);return;
  }
  if(el.dataset.nativeAdd){
    if(state.adding)return;
    const id=el.dataset.nativeAdd;
    const chosen=state.catalog.find(p=>p.id===id);
    state.adding=true;
    state.addError="";
    state.pendingPlugin=chosen?.name||"selected effect";
    const requestId=++state.addRequestId;
    const progress=document.getElementById("nativeAddFeedback");
    if(progress){
      progress.style.display="";
      progress.className="native-add-feedback waiting";
      progress.textContent="Loading "+state.pendingPlugin+"… Initializing VST3.";
      progress.setAttribute("aria-busy","true");
    }
    document.querySelectorAll("#nativeFxSelector [data-native-add]")
      .forEach(b=>b.disabled=true);
    // Let the WebView paint before the synchronous plugin factory call.
    requestAnimationFrame(()=>requestAnimationFrame(()=>send("addCatalog",{id,requestId})));
    return;
  }
  if(el.dataset.nativeCategory){
    state.selectedCategory=el.dataset.nativeCategory;
    renderPicker();return;
  }
  const fx=el.dataset.nativeFx,slot=Number(el.dataset.slot);
  if(fx==="editor")send("fxEditor",{slot});
  if(fx==="remove")send("fxRemove",{slot});
  if(fx==="bypass")send("fxBypass",{slot,value:!state.chain[slot]?.bypass});
  if(fx==="up"||fx==="down")send("fxMove",{slot,value:fx==="up"?-1:1});
},true);
  document.addEventListener("input", function(e){
  if(e.target.id!=="nativeVstSearch")return;
  state.filter=e.target.value;
  const pos=e.target.selectionStart;
  renderPicker();
  const input=document.querySelector("#nativeVstSearch");
  input?.focus();
  input?.setSelectionRange(pos,pos);
},true);

  // Persist numeric edits immediately; input[type=number] may not emit change
  // until blur, and a subsequent select change re-renders the popover.
  document.addEventListener("input",e=>{
    if(e.target?.id==="nativeOrbitSeconds")
      orbit.periodSeconds=Number(e.target.value);
    if(e.target?.id==="nativeOrbitNear")orbit.nearSetpoint=Number(e.target.value);
    if(e.target?.id==="nativeOrbitFar")orbit.farSetpoint=Number(e.target.value);
  },true);
  document.addEventListener("change",e=>{
    const t=e.target;
    if(!t||!["nativeOrbitNear","nativeOrbitFar","nativeOrbitParameter","nativeOrbitSeconds",
      "nativeOrbitReverse","nativeOrbitShape","nativeOrbitDistance","nativeOrbitElevation"].includes(t.id))return;
    if(t.id==="nativeOrbitShape"){
      orbit.selectedShape=Number(t.value);
      orbit.motionPath=[];
      send("orbitInspect",{slot:orbit.slot,shape:orbit.selectedShape});
      renderOrbit();return;
    }
    if(t.id==="nativeOrbitNear")orbit.nearSetpoint=Number(t.value);
    if(t.id==="nativeOrbitFar")orbit.farSetpoint=Number(t.value);
    if(t.id==="nativeOrbitParameter")orbit.selection=Number(t.value);
    if(t.id==="nativeOrbitDistance"){
      orbit.distanceSelection=Number(t.value);
      const p=orbit.parameters.find(x=>x.index===orbit.distanceSelection);
      const nextUnit=p?.unit==="m"?"m":"%";
      if(nextUnit!==orbit.distanceUnit){
        orbit.distanceUnit=nextUnit;
        orbit.distanceMin=nextUnit==="m"?p.minMetres:0;
        orbit.distanceMax=nextUnit==="m"?p.maxMetres:100;
        orbit.nearSetpoint=nextUnit==="m"?
          Math.min(orbit.distanceMax,Math.max(orbit.distanceMin,1)):20;
        orbit.farSetpoint=nextUnit==="m"?
          Math.min(orbit.distanceMax,Math.max(orbit.nearSetpoint,3)):70;
      }
    }
    if(t.id==="nativeOrbitElevation")orbit.elevationSelection=Number(t.value);
    if(t.id==="nativeOrbitSeconds")orbit.periodSeconds=Number(t.value);
    if(t.id==="nativeOrbitReverse")orbit.reverse=t.value==="true";
    // Don't destroy focused number fields on blur: a concurrent START/UPDATE
    // click must reach its original button, not a replacement DOM node.
    if(["nativeOrbitNear","nativeOrbitFar","nativeOrbitSeconds"].includes(t.id)){
      const button=document.querySelector("#nativeOrbit [data-orbit-start]");
      if(button){
        const near=orbit.nearSetpoint,far=orbit.farSetpoint;
        const needD=needsDistance(orbit.selectedShape);
        button.disabled=orbit.revision===null||
          !Number.isFinite(far)||far<orbit.distanceMin||far>orbit.distanceMax||
          (needD&&(!Number.isFinite(near)||near<orbit.distanceMin||near>far))||
          !Number.isFinite(orbit.periodSeconds)||orbit.periodSeconds<2||
          orbit.periodSeconds>180;
      }
      return;
    }
    renderOrbit();
  },true);
  backend.addEventListener("arMixOrbitState",data=>{
    if(!data||Number(data.slot)!==orbit.slot)return;
    const wasInitialized=orbit.initialized;
    const preview=data.previewShape!==-1;
    const draft={nearSetpoint:orbit.nearSetpoint,farSetpoint:orbit.farSetpoint,
      periodSeconds:orbit.periodSeconds,reverse:orbit.reverse,
      distanceUnit:orbit.distanceUnit,distanceMin:orbit.distanceMin,
      distanceMax:orbit.distanceMax};
    Object.assign(orbit,data);
    if(preview&&wasInitialized)Object.assign(orbit,draft);
    else if(data.active&&data.activeSlot===orbit.slot){
      orbit.appliedPeriod=data.periodSeconds;
      orbit.appliedReverse=data.reverse;
      orbit.selectedShape=data.shape;
      orbit.distanceSelection=data.distanceIndex;
      orbit.elevationSelection=data.elevationIndex;
    }
    orbit.initialized=true;
    if(data.previewShape===orbit.selectedShape || data.previewShape===-1){
      orbit.motionPath=Array.isArray(data.motionPath)?data.motionPath:[];
      orbit.visualPhase=Number(data.phase)||0;
      orbit.visualTime=performance.now();
    }
    renderOrbit();
    if(currentTab()==="FX RACK")renderRack();
    patchMixer();
  });
  backend.addEventListener("arMixPresets",items=>{
    state.presets=Array.isArray(items)?items:[];
    const dialog=document.getElementById("nativeMySounds");
    if(dialog&&!dialog.hidden){
      const previous=dialog.querySelector("#nativePresetName")?.value||"";
      renderMySounds();
      const input=dialog.querySelector("#nativePresetName");
      if(input)input.value=previous;
    }
  });
  backend.addEventListener("arMixNativePresetLoaded",d=>{
    // Synchronize the real mixer controls. Do not apply the legacy localStorage
    // demo presets as an authoritative source for DSP.
    if(typeof channels!=="undefined" && channels[0]){
      channels[0].fader=Number(d.channelDb)||0;
      channels[0].pan=Math.round(Number(d.pan)||0);
      channels[0].mute=!!d.channelMute;
      if(typeof masterGain!=="undefined")masterGain=Number(d.masterDb)||0;
      if(typeof masterMute!=="undefined")masterMute=!!d.masterMute;
      if(typeof renderMixer==="function")renderMixer();
      if(typeof renderMaster==="function")renderMaster();
      patchMixer();
    }
    hideDialog("nativeMySounds");
  });
  backend.addEventListener("arMixCatalog",data=>{
    state.catalog=Array.isArray(data.items)?data.items:[];
    state.scanning=!!data.scanning;
    state.message=data.message||"Ready";
    repaint();
    const dialog=document.getElementById("nativePluginManager");
    if(dialog&&!dialog.hidden)renderManager();
    const picker=document.getElementById("nativeFxSelector");
    if(picker&&!picker.hidden)renderPicker();
    updateProgress();
  });
  backend.addEventListener("arMixChain",nodes=>{
    state.chain=Array.isArray(nodes)?nodes:[];
    // Structural edits invalidate a previously verified VST3 parameter range.
    const orbitDialog=document.getElementById("nativeOrbit");
    if(orbitDialog&&!orbitDialog.hidden && !orbitAnchor?.isConnected)hideOrbit();
    if(orbitDialog&&!orbitDialog.hidden){
      orbit.revision=null;
      orbit.selection=-1;
      orbit.parameters=[];
      orbit.plugin=state.chain[orbit.slot]?.name||"";
      renderOrbit();
      if(state.chain[orbit.slot])send("orbitInspect",{slot:orbit.slot});
    }
    repaint();
    const picker=document.getElementById("nativeFxSelector");
    if(picker&&!picker.hidden)renderPicker();
  });
  backend.addEventListener("arMixAddResult",result=>{
    if(Number(result.requestId)!==state.addRequestId)return;
    state.adding=false;
    state.pendingPlugin="";
    state.addError=result.ok?"":String(result.error||"Unknown VST3 loading failure");
    if(result.ok)hideDialog("nativeFxSelector");
    else{
      // Keep error in the open modal; toast behind backdrop is insufficient.
      renderPicker();
      document.getElementById("nativeAddFeedback")?.setAttribute("aria-busy","false");
    }
  });
  backend.addEventListener("arMixNotice",data=>{
    const text=data.text||"";
    const toast=document.querySelector("#toast");
    if(toast){
      toast.textContent=text;
      toast.classList.add("show");
      setTimeout(()=>toast.classList.remove("show"),3500);
    }
    if(data.error)console.warn("AR MIX native:",text);
  });
  // The desktop DSP owns Channel 1 and Master gain/mute and Channel 1 PAN.
  // Control events are dispatched asynchronously; never overwrite an in-flight
  // fader/PAN edit with an older audio status frame.
  let controlEditUntil=0;
  function markNativeUiEdit(e){
    if(e.target?.closest?.('[data-fader="0"],[data-fader="master"],[data-pan="0"]'))
      controlEditUntil=Date.now()+350;
    if(typeof drag!=="undefined" && drag &&
       (drag.type==="fader" || drag.type==="pan"))
      controlEditUntil=Date.now()+350;
  }
  // Idle pointer traversal does not inspect DOM. Active drag stays guarded
  // by syncNativeMixerControls() and pointer release renews the grace window.
  for(const event of ["pointerdown","pointerup","pointercancel","wheel","keydown"])
    document.addEventListener(event,markNativeUiEdit,true);
  // A restored GUI-only snapshot must not masquerade as a live native setting.
  function syncNativeMixerControls(d){
    if(typeof channels==="undefined" || !channels[0])return;
    const channelDb=Number(d.channelDb), masterDb=Number(d.masterDb);
    const channelPan=Math.round(Number(d.channelPan)*100);
    if(!Number.isFinite(channelDb)||!Number.isFinite(masterDb)||!Number.isFinite(channelPan))return;
    const channel=channels[0];
    const editing=Date.now()<controlEditUntil ||
      (typeof drag!=="undefined" && drag && (drag.type==="fader" || drag.type==="pan"));
    let changed=false;
    if(!editing && Math.abs(channel.fader-channelDb)>0.05){
      channel.fader=channelDb;changed=true;
      const fader=document.querySelector('[data-fader="0"]');
      if(fader){fader.querySelector(".cap")?.style.setProperty("--pos",levelToTop(channelDb)+"%");fader.setAttribute("aria-valuenow",channelDb.toFixed(1));}
      const read=document.querySelector('[data-read="0"]');
      if(read)read.textContent=fmt(channelDb);
    }
    if(!editing && channel.pan!==channelPan){
      channel.pan=channelPan;changed=true;
      const pan=document.querySelector('[data-pan="0"]');
      if(pan){
        pan.classList.toggle("is-centered",channelPan===0);
        pan.style.setProperty("--pan-frac",(channelPan+100)/200);
        pan.style.setProperty("--pan-left",Math.min(.5,(channelPan+100)/200));
        pan.style.setProperty("--pan-span",Math.abs(channelPan)/200);
        pan.setAttribute("aria-valuenow",String(channelPan));
        pan.setAttribute("aria-valuetext",panText(channelPan));
      }
      const read=document.querySelector('[data-pan-read="0"]');
      if(read)read.textContent=panText(channelPan);
    }
    if(typeof d.channelMute==="boolean" && channel.mute!==d.channelMute){
      channel.mute=d.channelMute;changed=true;
      const mute=document.querySelector('[data-mute="0"]');
      if(mute){mute.classList.toggle("active",channel.mute);mute.setAttribute("aria-pressed",String(channel.mute));}
    }
    if(!editing && typeof masterGain!=="undefined" && Math.abs(masterGain-masterDb)>0.05){
      masterGain=masterDb;changed=true;
      const fader=document.querySelector('[data-fader="master"]');
      if(fader){fader.querySelector(".cap")?.style.setProperty("--pos",levelToTop(masterDb)+"%");fader.setAttribute("aria-valuenow",masterDb.toFixed(1));}
      document.querySelectorAll(".master-db").forEach(e=>e.textContent=fmt(masterDb));
    }
    if(typeof masterMute!=="undefined" && typeof d.masterMute==="boolean" && masterMute!==d.masterMute){
      masterMute=d.masterMute;changed=true;
      const mute=document.querySelector("#masterMute");
      if(mute){mute.classList.toggle("active",masterMute);mute.setAttribute("aria-pressed",String(masterMute));}
    }
    if(typeof masterMono!=="undefined" && typeof d.masterMono==="boolean" && masterMono!==d.masterMono){
      setMasterMono(d.masterMono,false);changed=true;
    }
    if(changed)patchMixer();
  }
  backend.addEventListener("arMixStatus",d=>{
    state.device=d;
    state.monitor=!!d.monitor;
    syncNativeMixerControls(d);
    const b=document.querySelector('[data-native-action="monitor"]');
    if(b){
      const label=state.monitor?"MUTE OUTPUT":"UNMUTE OUTPUT";
      if(b.textContent!==label)b.textContent=label;
      b.classList.toggle("on",!state.monitor);
    }
    const text=d.error?"AUDIO ERROR: "+d.error:
      d.connected?`${d.driver} · ${Math.round(d.rate)} Hz / ${d.buffer} · ${state.chain.length} LIVE VST3 · ${d.latencySamples||0} samples FX latency`
      :"NO AUDIO DEVICE · SELECT AUDIO SETUP";
    toStatus(text);
    // Only CH1 and master are sourced from a real native audio callback.
    // No fixed "-5 per telemetry callback" smoothing: its speed changed
    // with source cadence and fought the shared elapsed-time ballistics.
    // UI animation is capped at 29.4fps; native peaks remain unmodified.
    paintMeter(document.querySelector('[data-meter="0"]'),percent(d.inputPeak));
    paintMeter(document.querySelector('[data-meter="masterL"]')||
      document.getElementById("masterMeterL"),percent(d.peakLeft));
    paintMeter(document.querySelector('[data-meter="masterR"]')||
      document.getElementById("masterMeterR"),percent(d.peakRight));
    paintMeter(document.getElementById("masterMeterAux"),percent(d.peak));
    // Design-only LUFS/true-peak fields have no native measurement yet.
    const lufs=document.getElementById("masterLufs"),tp=document.getElementById("masterDbtp");
    if(lufs&&lufs.textContent!=="—")lufs.textContent="—";
    if(tp&&tp.textContent!=="—")tp.textContent="—";
    // v0.8 Master stream LUFS-I/dBTP remains unimplemented; values stay
    // em dashes rather than falsely claiming measured loudness.
    const pk=document.getElementById("masterPeakDb");
    if(pk)pk.textContent=Number(d.peak)>0
      ?(20*Math.log10(Math.max(1e-8,Number(d.peak)))).toFixed(1)+" dBFS":"−∞";
    const driver=document.getElementById("headerDriver");
    if(driver){
      const label=d.driver||"No device";
      if(driver.textContent!==label)driver.textContent=label;
    }
    // Preserve the latest main native device lamp.
    const deviceButton=document.querySelector(".toolbar .spec-device");
    if(deviceButton){
      const ready=!!d.connected && !d.error;
      deviceButton.classList.toggle("on",ready);
      if(deviceButton.dataset.audioConnected!==String(ready))
        deviceButton.dataset.audioConnected=String(ready);
    }
    const rate=document.getElementById("headerRate");
    if(rate){
      const label=d.rate?(Number(d.rate)/1000).toLocaleString("en-US",{maximumFractionDigits:1}):"--";
      if(rate.textContent!==label)rate.textContent=label;
    }
    const buffer=document.getElementById("headerBuffer");
    if(buffer){
      const label=String(d.buffer||"--");
      if(buffer.textContent!==label)buffer.textContent=label;
    }
  });
  backend.addEventListener("arMixScanProgress",p=>{
    state.progress=p;
    updateProgress();
  });
  // Create real dialogs and synchronize cached state after WebView startup.
  initModals();
  send("refreshState");
  syncView();
})();
