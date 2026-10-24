// SPDX-License-Identifier: AGPL-3.0-only
// Actual JUCE native bridge. This script is loaded only inside the desktop host.
// Never claims the other 11 concept tracks or mock presets are live channels.
(() => {
  const backend = window.__JUCE__?.backend;
  if (!backend?.emitEvent || !backend?.addEventListener) return;

  const send = (action, more = {}) => backend.emitEvent("arMixAction", {action, ...more});
  const state = {catalog: [], chain: [], scanning: false, message: "Connecting to native host...",
    device: {}, filter: "", selectedCategory: "All", monitor: false, error: ""};

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
  document.head.appendChild(css);
  const footer = document.querySelector(".status-help");
  if(footer) footer.innerHTML = `<div id="nativeP2">
    <button class="native-btn" data-native-action="audioSetup">AUDIO SETUP</button>
    <button class="native-btn primary" data-go-native-rack="1">LIVE FX RACK</button>
    <button class="native-btn" data-native-action="monitor">MONITOR OFF</button>
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
  function renderRack() {
    if(!workspace || currentTab()!=="FX RACK") return;
    const matches = state.catalog.filter(p => (
      (state.selectedCategory==="All"||classFor(p)===state.selectedCategory) &&
      ((p.name+" "+p.vendor+" "+p.category).toLowerCase().includes(state.filter.toLowerCase()))
    ));
    workspace.dataset.nativeRack="1";
    workspace.innerHTML = `<div class="native-panel" data-live-native="1">
      ${cardHeader("Live VST3 FX Rack","Input 1 · live stereo chain · plugin order is the real processing order · 8 inserts maximum")}
      <div class="native-toolbar" style="margin-bottom:12px">
        <button class="native-btn primary" data-native-action="importVst3">＋ IMPORT VST3 FILE</button>
        <button class="native-btn" data-native-action="addScanFolder">ADD PLUGIN FOLDER</button>
        <button class="native-btn" data-native-action="scanVst3">${state.scanning?"SCANNING...":"RESCAN VST3"}</button>
        <span class="native-sub">${state.catalog.length} plugins · ${escapeHtml(state.message)}</span>
      </div>
      <div class="native-grid">
        <section class="native-card"><h3>ACTIVE PROCESSING CHAIN · ${state.chain.length}/8</h3>
          <div class="native-nodes">
            <div class="native-status">MIC / AUDIO INPUT →</div>
            ${state.chain.map((p,i)=>`<div class="native-node ${p.bypass?"is-bypassed":""}">
              <span class="node-index">${String(i+1).padStart(2,"0")}</span>
              <span class="node-text">${escapeHtml(p.name)}<small>${p.latency||0} samples plugin latency</small></span>
              <button class="native-btn" data-native-fx="editor" data-slot="${i}">EDIT</button>
              <button class="native-btn ${p.bypass?"":"on"}" data-native-fx="bypass" data-slot="${i}">${p.bypass?"OFF":"ON"}</button>
              <button class="native-btn" data-native-fx="up" data-slot="${i}" ${i===0?"disabled":""}>↑</button>
              <button class="native-btn" data-native-fx="down" data-slot="${i}" ${i===state.chain.length-1?"disabled":""}>↓</button>
              <button class="native-btn warn" data-native-fx="remove" data-slot="${i}">×</button>
              </div>`).join("")}
            ${state.chain.length===0?'<div class="native-empty">No real effects loaded. Choose a VST3 from the catalog to hear its processing.</div>':""}
            <div class="native-status">→ MASTER · ${escapeHtml(state.device.device||"Audio output")}</div>
          </div>
          <div class="native-sub" style="margin-top:13px">Live plugin changes briefly pause audio in this build. Avoid editing a chain during a livestream.</div>
        </section>
        <section class="native-card">
          <h3>INSTALLED / IMPORTED PLUGIN CATALOG</h3>
          <input class="native-search" id="nativeVstSearch" autocomplete="off" placeholder="Search VST3 name, vendor, category..." value="${escapeHtml(state.filter)}">
          <div class="native-category">${fxCategories.map(c=>`<button class="native-btn ${c===state.selectedCategory?"on":""}" data-native-category="${c}">${c}</button>`).join("")}</div>
          <div class="native-scroll">${matches.length?matches.slice(0,200).map(p=>`<div class="native-item">
            <span class="native-tag">VST3</span><div><b title="${escapeHtml(p.path)}">${escapeHtml(p.name)}</b>
            <small>${escapeHtml(p.vendor||"Unknown developer")} · ${escapeHtml(p.category||"Effect")}</small></div>
            <button class="native-btn primary" data-native-add="${escapeHtml(p.id)}" ${state.chain.length>=8?"disabled":""}>＋ ADD</button></div>`).join("")
          : '<div class="native-empty">No matching VST3. Click RESCAN VST3, ADD PLUGIN FOLDER, or IMPORT VST3 FILE.</div>'}</div>
        </section>
      </div>
      <p class="native-sub" style="margin-top:11px">Only VST3 modules (.vst3) are supported. Legacy VST2 .dll is not a VST3 file. VST3 plugin editors open in separate native windows.</p>
    </div>`;
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
          <button class="native-btn" data-go-native-rack="1">OPEN LIVE FX RACK</button>
        </div>
        <p class="native-sub" style="margin-top:14px">For first test: wired headphones, 48 kHz and 128/256 samples. Start MONITOR OFF. ASIO and WASAPI are not interchangeable with virtual streaming outputs; loopback routing is a separate feature.</p>
      </section>
    </div>`;
  }
  function patchMixer() {
    if(!mixer) return;
    const tracks=mixer.querySelectorAll(".strip");
    tracks.forEach((track,i)=>{
      if(i>0) {track.title="UI preview channel (audio not connected yet)";return;}
      const rack=track.querySelector(".rack");
      if(!rack || rack.dataset.liveNative==="1") return;
      rack.dataset.liveNative="1";
      rack.innerHTML = state.chain.slice(0,4).map(p=>
        `<button class="insert" data-go-native-rack="1" title="Open active VST3 FX chain"><span class="insert-led">◉</span>${escapeHtml(p.name)}</button>`
      ).join("")+`<button class="insert empty" data-go-native-rack="1"><span class="insert-led">+</span>＋ REAL VST3</button>`;
    });
  }

  let renderPending = false;
  function syncView(){
    if(renderPending) return;
    renderPending=true;
    queueMicrotask(()=>{
      renderPending=false;
      if(currentTab()==="FX RACK"){
        if(workspace.dataset.nativeRack!=="1")renderRack();
      } else if(currentTab()==="SETUP"){
        if(workspace.dataset.nativeSetup!=="1")renderSetup();
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

  // Capture native commands before the legacy v0.3 demo handlers.
  document.addEventListener("click",e=>{
    const target=e.target.closest("[data-native-action],[data-native-add],[data-native-fx],[data-native-category],[data-go-native-rack],[data-insert]");
    if(!target)return;
    if(target.matches("[data-insert]") && currentTab()!=="MIX") return;
    e.preventDefault();e.stopImmediatePropagation();
    if(target.hasAttribute("data-go-native-rack")||target.hasAttribute("data-insert")){
      document.querySelector('.nav button[data-view="FX RACK"]')?.click();return;
    }
    const action=target.dataset.nativeAction;
    if(action){
      if(action==="monitor")send("monitor",{value:!state.monitor});
      else send(action);
      return;
    }
    if(target.dataset.nativeAdd){send("addCatalog",{id:target.dataset.nativeAdd});return;}
    if(target.dataset.nativeCategory){state.selectedCategory=target.dataset.nativeCategory;renderRack();return;}
    const fx=target.dataset.nativeFx,slot=Number(target.dataset.slot);
    if(fx==="editor")send("fxEditor",{slot});
    if(fx==="remove")send("fxRemove",{slot});
    if(fx==="bypass")send("fxBypass",{slot,value:!state.chain[slot]?.bypass});
    if(fx==="up"||fx==="down")send("fxMove",{slot,value:fx==="up"?-1:1});
  },true);
  document.addEventListener("input",e=>{
    if(e.target.id!=="nativeVstSearch")return;
    state.filter=e.target.value;
    // Keep typing focus and selection while updating only catalog list on
    // the next browser task. Full redraw of the search input would break UX.
    const input=e.target;
    const pos=input.selectionStart;
    renderRack();
    const newInput=document.querySelector("#nativeVstSearch");
    newInput?.focus();
    newInput?.setSelectionRange(pos,pos);
  },true);

  backend.addEventListener("arMixCatalog",data=>{
    state.catalog=Array.isArray(data.items)?data.items:[];
    state.scanning=!!data.scanning;
    state.message=data.message||"Ready";
    repaint();
  });
  backend.addEventListener("arMixChain",nodes=>{
    state.chain=Array.isArray(nodes)?nodes:[];
    repaint();
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
  backend.addEventListener("arMixStatus",d=>{
    state.device=d;
    state.monitor=!!d.monitor;
    const b=document.querySelector('[data-native-action="monitor"]');
    if(b){b.textContent=state.monitor?"MONITOR ON":"MONITOR OFF";b.classList.toggle("on",state.monitor);}
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
    if(statusSetup && !document.activeElement?.matches("input"))renderSetup();
  });
  // JS bridge may initialise after the initial native push. Explicit sync.
  send("refreshState");
  syncView();
})();
