// SPDX-License-Identifier: AGPL-3.0-only
(function installPluginUI(){
  "use strict";
  // Native catalog-only renderer. No timers, filters run only on user input.
  const escapeHtml = v => String(v ?? "").replace(/[&<>"']/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
  const manager={query:"",sort:"name",ascending:true,limit:120};
  const pathParts=p=>{const v=String(p||"");const i=Math.max(v.lastIndexOf("/"),v.lastIndexOf("\\"));return {file:i<0?v:v.slice(i+1),folder:i<0?"":v.slice(0,i)}};
  const compare=(a,b)=>String(a??"").localeCompare(String(b??""),undefined,{sensitivity:"base",numeric:true});
  const categoryMatch=(p,category,classFor)=>category==="All"||classFor(p)===category;
  function renderManagerRows(dialog,state){
    const list=dialog?.querySelector("#arPluginManagerRows");
    if(!list)return;
    const q=manager.query.trim().toLocaleLowerCase();
    const filtered=state.catalog.filter(p=>!q||[p.name,p.vendor,p.category,p.path].join(" ").toLocaleLowerCase().includes(q));
    const direction=manager.ascending?1:-1;
    filtered.sort((a,b)=>direction*compare(a[manager.sort],b[manager.sort])||compare(a.name,b.name));
    list.innerHTML=filtered.length?filtered.slice(0,manager.limit).map(p=>{
      const path=pathParts(p.path),name=escapeHtml(p.name||path.file),vendor=escapeHtml(p.vendor||"Unknown");
      return `<div class="ar-plugin-row" role="row">
        <span class="ar-plugin-name" role="cell" title="${name}">${name}</span>
        <span class="ar-plugin-state" role="cell">INDEXED</span>
        <span role="cell" class="ar-plugin-format">VST3</span>
        <span role="cell" class="ar-plugin-ellipsis" title="${vendor}">${vendor}</span>
        <span role="cell" class="ar-plugin-ellipsis" title="${escapeHtml(p.category||"Effect")}">${escapeHtml(p.category||"Effect")}</span>
        <span role="cell" class="ar-plugin-file" title="${escapeHtml(p.path)}">
          <span>${escapeHtml(path.file)}</span><small>${escapeHtml(path.folder)}</small>
        </span></div>`;
    }).join(""):'<div class="ar-manager-empty">No matching plugins. Try another search or import a VST3.</div>';
    const count=dialog.querySelector("#arManagerCount");
    if(count)count.textContent=`${filtered.length} of ${state.catalog.length} indexed effects`;
    const more=dialog.querySelector("#arManagerMore");
    if(more){more.hidden=filtered.length<=manager.limit;more.textContent=`Show more (${Math.min(120,filtered.length-manager.limit)} of ${filtered.length-manager.limit} remaining)`;}
    dialog.querySelectorAll("[data-armix-sort]").forEach(b=>{
      const active=b.dataset.armixSort===manager.sort;
      b.setAttribute("aria-label",`Sort by ${b.dataset.armixSort}${active?(manager.ascending?", ascending":", descending"):""}`);
      b.dataset.sortActive=String(active);
      b.querySelector(".ar-sort-symbol").textContent=active?(manager.ascending?" ↑":" ↓"):"";
    });
  }
  function renderManager(dialog,state,progressHtml){
    if(!dialog)return;
    dialog.querySelector(".native-dialog-body").innerHTML=`
      <div class="ar-manager-actions">
        <div class="ar-manager-action-group">
          <span class="ar-manager-label">IMPORT INTO STUDIO <small>Portable copy</small></span>
          <button class="native-btn primary" data-native-action="importVst3" ${state.scanning?"disabled":""}>+ IMPORT .VST3 FILE</button>
          <button class="native-btn primary" data-native-action="importVst3Bundle" ${state.scanning?"disabled":""}>+ IMPORT BUNDLE</button>
        </div>
        <div class="ar-manager-action-group">
          <span class="ar-manager-label">EXTERNAL PLUGINS <small>Reference original</small></span>
          <button class="native-btn" data-native-action="importVst3InPlace" ${state.scanning?"disabled":""}>LINK FILE</button>
          <button class="native-btn" data-native-action="importVst3BundleInPlace" ${state.scanning?"disabled":""}>LINK BUNDLE</button>
          <button class="native-btn" data-native-action="addScanFolder" ${state.scanning?"disabled":""}>ADD FOLDER</button>
          <button class="native-btn" data-native-action="scanVst3" ${state.scanning?"disabled":""}>SCAN SYSTEM</button>
        </div>
      </div>
      <section class="ar-manager-scan" aria-label="Plugin scanner status">
        <span class="ar-manager-status-dot ${state.scanning?"is-scanning":""}" aria-hidden="true"></span>
        <div class="ar-manager-progress" id="nativeManagerProgress" role="status" aria-live="polite">
          ${state.scanning?progressHtml():`<span>${escapeHtml(state.message||"Ready")}</span>`}
        </div>
      </section>
      <section class="ar-manager-catalog" aria-label="Indexed VST3 effects">
        <div class="ar-manager-searchline">
          <input class="ar-manager-search" id="arManagerSearch" type="search" aria-label="Search indexed plugins"
            placeholder="Search plugins, vendors, category or path" value="${escapeHtml(manager.query)}" autocomplete="off">
          <span id="arManagerCount" aria-live="polite"></span>
        </div>
        <div class="ar-manager-table-scroll">
          <div class="ar-manager-table" role="table" aria-label="VST3 plugin catalog">
            <div class="ar-plugin-row ar-plugin-header" role="row">
              <button role="columnheader" data-armix-sort="name">Plugin <span class="ar-sort-symbol"></span></button>
              <span role="columnheader">Status</span>
              <span role="columnheader">Format</span>
              <button role="columnheader" data-armix-sort="vendor">Vendor <span class="ar-sort-symbol"></span></button>
              <button role="columnheader" data-armix-sort="category">Type <span class="ar-sort-symbol"></span></button>
              <button role="columnheader" data-armix-sort="path">File / location <span class="ar-sort-symbol"></span></button>
            </div>
            <div id="arPluginManagerRows" role="rowgroup"></div>
          </div>
        </div>
        <button id="arManagerMore" class="ar-manager-more" type="button" hidden>Show more</button>
      </section>`;
    dialog.querySelector(".native-dialog-footer").textContent="INDEXED means catalogued, not load-verified. VST2 .dll is unsupported. Third-party scanning is not yet crash-isolated.";
    renderManagerRows(dialog,state);
  }
  const filteredPicker=(state,classFor)=>state.catalog.filter(p=>categoryMatch(p,state.selectedCategory,classFor))
    .filter(p=>[p.name,p.vendor,p.category].join(" ").toLocaleLowerCase().includes(state.filter.toLocaleLowerCase()));
  function updatePickerRows(dialog,state,classFor,channel){
    if(!dialog)return;
    const list=dialog.querySelector("#arPickerRows");
    if(!list)return;
    const matches=filteredPicker(state,classFor);
    list.innerHTML=matches.length?matches.slice(0,80).map(p=>`
      <div class="ar-picker-row" role="listitem">
        <div class="ar-picker-details">
          <span class="ar-picker-name" title="${escapeHtml(p.name)}">${escapeHtml(p.name)}</span>
          <span class="ar-picker-sub" title="${escapeHtml((p.vendor||"Unknown vendor")+" · "+(p.category||"Effect"))}">
            ${escapeHtml(p.vendor||"Unknown vendor")} · ${escapeHtml(p.category||"Effect")}</span>
        </div>
        <span class="ar-picker-format">VST3</span>
        <button class="native-btn primary ar-picker-add" data-native-add="${escapeHtml(p.id)}"
          ${state.chain.length>=8||state.adding||channel!==0?"disabled":""}
          title="${channel===0?"Insert VST3 in Channel 1":"This channel does not have native FX routing"}">+ ADD</button>
      </div>`).join(""):'<div class="ar-picker-empty">No matching effects. Open Plugin Manager to import or scan.</div>';
    const count=dialog.querySelector("#arPickerCount");
    if(count)count.textContent=`${matches.length} matching · ${Math.min(matches.length,80)} shown`;
    dialog.querySelectorAll("[data-native-category]").forEach(button=>
      button.classList.toggle("on",button.dataset.nativeCategory===state.selectedCategory));
  }
  function updatePickerFeedback(dialog,state){
    const feedback=dialog?.querySelector("#nativeAddFeedback");
    if(!feedback)return;
    feedback.hidden=!state.addError&&!state.adding;
    feedback.className="native-add-feedback "+(state.addError?"error":state.adding?"waiting":"");
    feedback.setAttribute("aria-busy",String(state.adding));
    feedback.textContent=state.adding?`Loading ${state.pendingPlugin}… Initializing VST3.`:
      state.addError?`FX NOT LOADED: ${state.addError}`:"";
  }
  function renderPicker(dialog,state,context,categories,classFor){
    if(!dialog)return;
    dialog.querySelector(".native-dialog-body").innerHTML=`
      <div class="ar-picker-context">${escapeHtml(context)}</div>
      <input class="native-search ar-picker-search" id="nativeVstSearch" type="search"
        placeholder="Find an effect or vendor..." aria-label="Search VST3 effects"
        value="${escapeHtml(state.filter)}" autocomplete="off">
      <div class="native-category ar-picker-categories" aria-label="Effect type">
        ${categories.map(c=>`<button class="native-btn ${c===state.selectedCategory?"on":""}"
          data-native-category="${escapeHtml(c)}">${escapeHtml(c)}</button>`).join("")}
      </div>
      <div id="nativeAddFeedback" aria-live="polite" role="status" hidden></div>
      <div id="arPickerRows" class="ar-picker-results" role="list" aria-label="Matching VST3 effects"></div>`;
    dialog.querySelector(".native-dialog-footer").innerHTML=
      `<span id="arPickerCount"></span><a href="#" data-plugin-manager="1">OPEN PLUGIN MANAGER</a>`;
    updatePickerFeedback(dialog,state);
    updatePickerRows(dialog,state,classFor,context.startsWith("MASTER")?-1:Number(context.match(/CH (\d+)/)?.[1]||1)-1);
  }
  function init(getState){
    // Delegated once; no mousemove/hover/RAF, no focus-destroying rerenders on typing.
    document.addEventListener("input",e=>{
      if(e.target?.id!=="arManagerSearch")return;
      manager.query=e.target.value;manager.limit=120;
      renderManagerRows(document.getElementById("nativePluginManager"),getState());
    });
    document.addEventListener("click",e=>{
      const more=e.target.closest("#arManagerMore");
      if(more){manager.limit+=120;renderManagerRows(document.getElementById("nativePluginManager"),getState());return;}
      const sort=e.target.closest("#nativePluginManager [data-armix-sort]");
      if(sort){
        const key=sort.dataset.armixSort;
        if(!["name","vendor","category","path"].includes(key))return;
        manager.ascending=manager.sort===key?!manager.ascending:true;
        manager.sort=key;manager.limit=120;
        renderManagerRows(document.getElementById("nativePluginManager"),getState());
      }
    });
  }
  window.ArSonPluginUI={init,renderManager,renderManagerRows,renderPicker,updatePickerRows,updatePickerFeedback};
})();
