// SPDX-License-Identifier: AGPL-3.0-only
// Actual JUCE native bridge. This script is loaded only inside the desktop host.
// Never claims the other 11 concept tracks or mock presets are live channels.
(() => {
  const backend = window.__JUCE__?.backend;
  if (!backend?.emitEvent || !backend?.addEventListener) return;

  const send = (action, more = {}) => backend.emitEvent("arMixAction", {action, ...more});
  const state = {catalog: [], chain: [], scanning: false, message: "Connecting to native host...",
    device: {}, filter: "", selectedCategory: "All", monitor: false, error: "", progress: {active:false,phase:"done",done:0,total:0,found:0,current:""}, adding:false};

  const escapeHtml = text => String(text ?? "").replace(/[&<>"']/g, c => (
    {"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
  const percent = peak => Math.max(1,Math.min(100,(20*Math.log10(Math.max(1e-6,Number(peak)||0))+60)*100/60));
  const toStatus = text => {
    const e = document.querySelector("#nativeDeviceStatus");
    if(e) e.textContent = text;
    const footer = document.querySelector("#audioStatus");
    if(footer) footer.textContent = text;
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
    #nativeP2{display:flex;gap:5px;align-items:center;font-size:10px;white-space:nowrap}
    #nativeP2 button{padding:4px 8px;font-size:10px}
    #nativeDeviceStatus{color:#a8c4d0;max-width:360px;overflow:hidden;text-overflow:ellipsis}
    .status-help{flex:1;min-width:0}
    @media(max-width:1150px){.native-grid{grid-template-columns:1fr}.native-hdr{flex-direction:column}}
  `;
  css.textContent += "\n    .native-modal{position:fixed;inset:0;z-index:700;display:flex;align-items:center;justify-content:center;\n      background:rgba(3,11,18,.78);backdrop-filter:blur(5px);padding:22px;box-sizing:border-box}\n    .native-modal[hidden]{display:none}\n    .native-dialog{width:min(860px,96vw);max-height:min(840px,92vh);overflow:hidden;\n      display:flex;flex-direction:column;background:linear-gradient(145deg,#243945 0%,#101e29 62%,#0c1821);\n      border:1px solid #638293;border-radius:11px;box-shadow:0 18px 80px #000c,inset 0 1px #b2d9e449}\n    .native-dialog-header{display:flex;align-items:center;justify-content:space-between;gap:15px;\n      border-bottom:1px solid #3c5360;padding:14px 17px}\n    .native-dialog-header h2{font-size:18px;font-weight:500;color:#e4f4f9;margin:4px 0}\n    .native-dialog-body{padding:14px 17px;overflow:auto;min-height:60px}\n    .native-dialog-footer{border-top:1px solid #334b58;padding:9px 17px;color:#91abb8;font-size:11px}\n    .native-dialog.small{max-width:520px}\n    .native-bar{height:12px;border:1px solid #527282;background:#0a1621;border-radius:7px;overflow:hidden;position:relative;\n      box-shadow:inset 0 2px 5px #000c}\n    .native-bar-fill{height:100%;width:0%;background:linear-gradient(180deg,#8ef6ff,#17a8da 43%,#08688e);\n      box-shadow:0 0 8px #1ed5fcbb;border-right:1px solid #ddffff}\n    .native-bar.unknown .native-bar-fill{width:34%;position:absolute;animation:native-indeterminate 1.65s ease-in-out infinite}\n    @keyframes native-indeterminate{0%{left:-36%}50%{left:58%}100%{left:105%}}\n    .native-progress-info{font:11px monospace;color:#a5d5e2;margin:8px 0;display:flex;justify-content:space-between;gap:9px}\n    .native-progress-info strong{font-weight:500;color:#d8f9ff}\n    #nativeScanHud{position:fixed;left:50%;transform:translateX(-50%);bottom:37px;z-index:550;\n      background:#152c3b;border:1px solid #537588;border-radius:8px;box-shadow:0 6px 20px #000a;\n      padding:9px 13px;width:min(470px,90vw)}\n    #nativeScanHud[hidden]{display:none}\n    .native-result{display:flex;align-items:center;gap:7px;background:#10222c;border:1px solid #314d5c;\n      border-radius:6px;padding:9px 11px;margin:5px 0}\n    .native-result span{flex:1;min-width:0}\n    .native-result b{display:block;font-weight:500;color:#d6edf7;font-size:12px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}\n    .native-result small{display:block;color:#88aaba;font-size:10px;margin-top:4px}\n    .native-search{font-family:inherit!important}\n";
  document.head.appendChild(css);
  const footer = document.querySelector(".status-help");
  if(footer) footer.innerHTML = `<div id="nativeP2">
    <button class="native-btn" data-native-action="audioSetup">AUDIO SETUP</button>
    <button class="native-btn primary" data-go-native-rack="1">LIVE FX RACK</button>
    <span class="native-sub" title="Live audio routes automatically to the chosen hardware output">LIVE AUDIO AUTO-ROUTED</span>
    <span id="nativeDeviceStatus">Starting native audio...</span>
  </div>`;

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
          <button class="native-btn ${p.bypass?"":"on"}" data-native-fx="bypass" data-slot="${i}">${p.bypass?"BYPASS":"ACTIVE"}</button>
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
  function pluginRows(items,limit=180){
  return items.length?items.slice(0,limit).map(p=>`<div class="native-result">
    <span><b title="${escapeHtml(p.path)}">${escapeHtml(p.name)}</b>
      <small>${escapeHtml(p.vendor||"Unknown vendor")} · ${escapeHtml(p.category||"Audio effect")}</small></span>
    <span class="native-tag" style="flex:0">VST3</span>
    <button class="native-btn primary" data-native-add="${escapeHtml(p.id)}"
      ${state.chain.length>=8?"disabled":""}>＋ ADD</button></div>`).join("")
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
      <button class="native-btn primary" data-native-action="scanVst3" ${state.scanning?"disabled":""}>SCAN STANDARD FOLDERS</button>
      <button class="native-btn" data-native-action="addScanFolder" ${state.scanning?"disabled":""}>ADD FOLDER</button>
      <button class="native-btn" data-native-action="importVst3" ${state.scanning?"disabled":""}>IMPORT INDIVIDUAL .VST3</button>
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
  function addPluginManagerToolbar(){
    const nav=document.querySelector(".nav");
    if(nav&&!nav.querySelector("[data-plugin-manager]")){
      const b=document.createElement("button");
      b.dataset.pluginManager="1";
      b.textContent="PLUGINS";
      b.title="Plugin Manager · VST3 scan and catalog administration";
      nav.appendChild(b);
    }
  }
  function initModals(){
  createDialog("nativeFxSelector","Add VST3 Effect");
  createDialog("nativePluginManager","Plugin Manager");
  createDialog("nativeAudioSetup","Opening Audio Setup",true);
  const setup=document.getElementById("nativeAudioSetup");
  setup.querySelector(".native-dialog-body").innerHTML=`<p class="native-sub">Preparing ASIO/WASAPI driver configuration…</p>
    <div class="native-bar unknown"><div class="native-bar-fill"></div></div>
    <p class="native-sub" style="margin-top:12px">Third-party ASIO drivers may require additional enumeration time.</p>`;
  setup.querySelector(".native-dialog-footer").textContent="Audio processing remains independent of this window.";
  const hud=document.createElement("div");hud.id="nativeScanHud";hud.hidden=true;document.body.appendChild(hud);
}
  function renderSetup() {
    if(!workspace||currentTab()!=="SETUP") return;
    workspace.dataset.nativeSetup="1";
    const d=state.device;
    workspace.innerHTML = `<div class="native-panel" data-native-setup="1">
      ${cardHeader("Windows Audio Setup","Select your ASIO or WASAPI input and output using the native JUCE device selector. Settings are retained after restart.")}
      <section class="native-card" style="max-width:700px">
        <h3>ACTIVE AUDIO DEVICE</h3>
        <p class="native-status">Driver: ${escapeHtml(d.driver||"Not connected")}<br>Device: ${escapeHtml(d.device||"Not selected")}<br>Sample rate: ${Number(d.rate||0).toLocaleString()} Hz · Buffer: ${d.buffer||0} samples</p>
        <div class="native-toolbar" style="margin-top:15px">
          <button class="native-btn primary" data-native-action="audioSetup">OPEN ASIO / WASAPI DEVICE SETTINGS</button>
          <button class="native-btn" data-plugin-manager="1">OPEN PLUGIN MANAGER</button><button class="native-btn" data-go-native-rack="1">OPEN LIVE FX RACK</button>
        </div>
        <p class="native-sub" style="margin-top:14px">For first test: wired headphones, 48 kHz and 128/256 samples. Start MONITOR OFF. ASIO and WASAPI are not interchangeable with virtual streaming outputs; loopback routing is a separate feature.</p>
      </section>
    </div>`;
  }
  function patchMixer(){
  if(!mixer)return;
  const tracks=mixer.querySelectorAll(".strip");
  tracks.forEach((track,i)=>{
    const rack=track.querySelector(".rack");
    if(!rack)return;
    if(i!==0){
      rack.dataset.preview="1";
      track.title=`${track.querySelector(".channel-name")?.textContent||"Channel"} — visual preview only; native I/O not wired`;
      return;
    }
    const signature=state.chain.map(p=>p.id+"|"+p.bypass).join(",");
    if(rack.dataset.nativeSig===signature)return;
    rack.dataset.nativeSig=signature;
    rack.innerHTML=state.chain.slice(0,4).map((p,i)=>
      `<button class="insert" data-native-fx="editor" data-slot="${i}" title="Open this VST3 plugin editor">
        <span class="insert-led">◉</span>${escapeHtml(p.name)}</button>`).join("")+
      `<button class="insert empty" data-plugin-picker="1" aria-label="Add VST3 to Channel 1">
        <span class="insert-led">＋</span>ADD FX</button>`;
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
      } else if(currentTab()==="SETUP"){
        if(workspace.firstElementChild?.dataset.nativeSetup!=="1")renderSetup();
      }
      patchMixer();
    });
  }
  new MutationObserver(()=>syncView()).observe(workspace,{childList:true});
  new MutationObserver(()=>syncView()).observe(mixer,{childList:true});
  const repaint=()=>{
    if(currentTab()==="FX RACK") renderRack();
    if(currentTab()==="SETUP") renderSetup();
    patchMixer();
  };

  // Capture native controls before the legacy v0.3 demo handlers.
  document.addEventListener("click", function(e){
  const el=e.target.closest("[data-native-action],[data-native-add],[data-native-fx],[data-native-category],[data-go-native-rack],[data-insert],[data-plugin-picker],[data-plugin-manager],[data-close-modal]");
  if(!el)return;
  e.preventDefault();e.stopImmediatePropagation();

  if(el.dataset.closeModal){hideDialog(el.dataset.closeModal);return;}
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
    if(action==="audioSetup"){
      const overlay=document.getElementById("nativeAudioSetup");
      if(overlay){
        overlay.querySelector(".native-dialog-body").innerHTML=
          `<p class="native-sub">Preparing ASIO/WASAPI device selection…</p>
           <div class="native-bar unknown"><div class="native-bar-fill"></div></div>
           <p class="native-sub" style="margin-top:12px">Loading device options. Third-party driver enumeration may be slow.</p>`;
        showDialog("nativeAudioSetup");
      }
      // Allow WebView to paint before synchronous JUCE/ASIO enumeration begins.
      requestAnimationFrame(()=>requestAnimationFrame(()=>send("audioSetup")));
      return;
    }
    send(action);return;
  }
  if(el.dataset.nativeAdd){
    state.adding=true;
    send("addCatalog",{id:el.dataset.nativeAdd});return;
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
    const before=state.chain.length;
    state.chain=Array.isArray(nodes)?nodes:[];
    if(state.adding && state.chain.length>before){hideDialog("nativeFxSelector");state.adding=false;}
    repaint();
    const picker=document.getElementById("nativeFxSelector");
    if(picker&&!picker.hidden)renderPicker();
  });
  backend.addEventListener("arMixNotice",data=>{
    const text=data.text||"";
    const toast=document.querySelector("#toast");
    if(toast){
      toast.textContent=text;
      toast.classList.add("show");
      setTimeout(()=>toast.classList.remove("show"),3500);
    }
    if(data.error){state.adding=false;console.warn("AR MIX native:",text);}
  });
  let lastSetupSignature="";
  backend.addEventListener("arMixStatus",d=>{
    state.device=d;
    state.monitor=!!d.monitor;
    const b=document.querySelector('[data-native-action="monitor"]');
    if(b){b.textContent=state.monitor?"MUTE OUTPUT":"UNMUTE OUTPUT";b.classList.toggle("on",!state.monitor);}
    const text=d.error?"AUDIO ERROR: "+d.error:
      d.connected?`${d.driver} · ${Math.round(d.rate)} Hz / ${d.buffer} · ${state.chain.length} LIVE VST3 · ${d.latencySamples||0} samples FX latency`
      :"NO AUDIO DEVICE · SELECT AUDIO SETUP";
    toStatus(text);
    // Only CH1 and master are sourced from a real native audio callback.
    const src=percent(d.inputPeak), out=percent(d.peak);
    document.querySelectorAll("[data-meter]").forEach(x=>
      x.style.setProperty("--level",x.dataset.meter==="0"?src+"%":"1%"));
    ["masterMeterL","masterMeterR","masterMeterAux"].forEach(id=>{
      const x=document.getElementById(id);
      if(x)x.style.setProperty("--level",out+"%");
    });
    const cpu=document.querySelector(".cpu strong");
    if(cpu)cpu.textContent="NATIVE";
    const statusSetup=document.querySelector("[data-native-setup]");
    const setupSignature=[d.connected,d.driver,d.device,d.rate,d.buffer,d.error||""].join("|");
    if(statusSetup && setupSignature!==lastSetupSignature && !document.activeElement?.matches("input")){
      lastSetupSignature=setupSignature;
      renderSetup();
    }
  });
  backend.addEventListener("arMixScanProgress",p=>{
    state.progress=p;
    updateProgress();
  });
  backend.addEventListener("arMixSetupProgress",p=>{
    const dlg=document.getElementById("nativeAudioSetup");
    if(!dlg)return;
    if(p.phase==="opened"){
      const body=dlg.querySelector(".native-dialog-body");
      body.innerHTML=`<p class="native-sub">Audio Device Settings opened in a separate window.</p><p class="native-sub">Setup initialization: ${Number(p.elapsedMs)||0} ms.</p>`;
      setTimeout(()=>hideDialog("nativeAudioSetup"),700);
    }else{
      const info=dlg.querySelector(".native-dialog-body p");
      if(info) info.textContent=p.phase==="enumerating"?"Enumerating ASIO/WASAPI devices and drivers…":"Preparing audio device selector…";
    }
  });
  // Create real dialogs and synchronize cached state after WebView startup.
  initModals();
  addPluginManagerToolbar();
  send("refreshState");
  syncView();
})();
