// SPDX-License-Identifier: AGPL-3.0-only
// Native output diagnostics: JS control only, never audio callback work.
(() => {
  const api = window.__JUCE__?.backend;
  if (!api?.emitEvent || !api?.addEventListener) return;
  let state = {};
  let catalogItems = []; // native VST3 catalog, never a WebView file path
  const css = document.createElement("style");
  css.textContent = "#arSignalOverlay{display:none;position:fixed;inset:0;z-index:990;background:#020a10d9;align-items:center;justify-content:center}" +
    "#arSignalOverlay.open{display:flex}.arSignalPanel{width:min(710px,93vw);padding:16px;background:linear-gradient(#263d4b,#10212c 80%);border:1px solid #6a92a5;border-radius:9px;color:#d9eaf3;box-shadow:0 15px 55px #000c}" +
    ".arSignalHeader{display:flex;align-items:center;justify-content:space-between;margin-bottom:16px}.arSignalHeader h2{font-size:18px;font-weight:500;margin:0}" +
    ".arSignalGrid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:9px}.arSignalStage{padding:10px;background:#0d202b;border:1px solid #365565;border-radius:6px;min-width:0}" +
    ".arSignalStage small{display:block;color:#80d4e8;font-size:10px}.arSignalStage b{display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-weight:500;font-size:11px;margin:10px 0}" +
    ".arSignalMeter{background:#041019;height:9px;border:1px solid #37576a;border-radius:4px;overflow:hidden}.arSignalMeter i{display:block;width:1%;height:100%;background:linear-gradient(90deg,#14bbaa,#b6d851,#edac38)}" +
    "#arSignalReason{font-size:12px;line-height:1.6;padding:12px;background:#1c3645;border:1px solid #4d7284;border-radius:5px;margin:13px 0}" +
    ".arSignalActions{display:flex;flex-wrap:wrap;gap:8px}.arSignalActions button,.arSignalHeader button{color:#e0f5ff;padding:8px 12px;border:1px solid #57849a;border-radius:4px;background:linear-gradient(#395b70,#143447);cursor:pointer}" +
    "#arSignalFooter{margin-top:12px;font-size:11px;color:#9bbbbf;line-height:1.6}#arSignalBadge{color:#edbf7d;font-size:10px;margin-left:6px;white-space:nowrap}" +
    "#arSignalOpen{padding:4px 7px;font-size:10px}" +
    "#arDiagnosticLab{display:flex;align-items:center;gap:8px;flex-wrap:wrap;margin-top:10px;font-size:11px}" +
    "#arDiagnosticMode{color:#d9eaf3;background:#142a36;border:1px solid #57849a;border-radius:4px;padding:6px;font-size:11px}" +
    "#arDiagnosticMark{color:#f9e5ca;background:#503926;border:1px solid #b9854a;border-radius:4px;padding:7px 10px;cursor:pointer}" +
    "#arDiagnosticCopy{color:#e1f7f4;background:#166052;border:1px solid #34c8ac;border-radius:4px;padding:7px 12px;cursor:pointer}" +
    "#arDiagnosticCopyStatus{color:#8cdec9;font-size:10px}" +
    "#arSmartClockToggle{color:#f5eadb;background:#284961;border:1px solid #9bb2c5;border-radius:4px;padding:7px 12px;cursor:pointer}";
  // No blur, no animated shaders or per-strip timer. Routing draws only
  // on explicit user edit; existing native health polling stays at 12 Hz.
  css.textContent +=
    "#arRoutingLabOverlay{display:none;position:fixed;inset:0;z-index:995;background:#08121be8;align-items:center;justify-content:center}" +
    "#arRoutingLabOverlay.open{display:flex}#arRoutingLabPanel{width:min(760px,94vw);max-height:86vh;overflow:auto;box-sizing:border-box;padding:16px;background:#172834;border:1px solid #637e8a;border-radius:8px;color:#dcecf3}" +
    "#arRoutingLabPanel h2{font-size:17px;font-weight:600;margin:0 0 6px}#arRoutingLabPanel p{font-size:11px;line-height:1.6;color:#9cc5d4;margin:6px 0 13px}" +
    ".arRoutePluginLine{grid-column:1/-1;display:flex;align-items:center;gap:8px;padding:1px 4px 5px;color:#9bbfc6;font-size:10px}.arRoutePluginLine select{min-width:0;max-width:460px;flex:1;padding:5px 7px;color:#dcecf3;background:#0d202b;border:1px solid #4d6879;border-radius:4px}.arRoutePluginLine small{color:#7fa4b1}" +
    ".arRouteRow{display:grid;grid-template-columns:54px 82px minmax(65px,1fr) minmax(65px,1fr) 88px 148px;gap:6px;align-items:center;padding:5px 0;border-bottom:1px solid #2d4653}" +
    ".arRouteRow label{font-size:11px;font-weight:600}.arRouteRow select{width:100%;min-width:0;padding:6px;background:#0d202b;border:1px solid #4d6879;border-radius:4px;color:#e1eef5;font-size:11px}" +
    ".arRouteFlags{display:flex;gap:5px;align-items:center;font-size:10px}.arRouteFlags label{display:flex;gap:1px;align-items:center;font-size:10px;white-space:nowrap}.arRouteFlags input{width:12px;height:12px;margin:0}.arRoutePeak{display:block;color:#91dcd0;font-size:9px;font-variant-numeric:tabular-nums}" +
    "#arRouteRows{overflow-x:auto}.arRouteRow{min-width:590px}#arRouteSends{margin-top:12px}#arRouteSends h3{font-size:12px;margin:8px 0;color:#b1d4dd}.arSendRow{display:grid;grid-template-columns:1fr 1fr 76px 83px 32px;gap:6px;margin-bottom:5px}.arSendRow select{width:100%;min-width:0;padding:6px;color:#e1eef5;background:#0d202b;border:1px solid #4d6879;border-radius:4px}.arSendRow button{border:1px solid #9a6060;background:#442930;color:#f3dadd;border-radius:4px}#arAddSend{font-size:10px;padding:6px 8px}" +
    ".arRouteFoot{display:flex;gap:8px;flex-wrap:wrap;justify-content:flex-end;margin-top:12px}.arRouteFoot button,.arRouteClose{color:#f0f7fa;background:#24475a;border:1px solid #587f91;border-radius:4px;padding:8px 12px;cursor:pointer}" +
    "#arRouteApply{background:#237566;border-color:#4ba48d}#arRouteStop{background:#50343c;border-color:#a8717c}" +
    "#arRouteFeedback{font-size:11px;min-height:18px;color:#8bdec1;margin-top:10px}" +
    "#arSavedShows{margin:12px 0;padding:9px;background:#0c202b;border:1px solid #416071;border-radius:5px}" +
    "#arSavedShows label{font-size:11px;color:#b6d9e1;margin-right:8px}#arSavedShowSelect{width:min(100%,340px);font-size:11px;color:#e3f0f5;background:#142b39;border:1px solid #547b8a;border-radius:4px;padding:7px}" +
    "#arSavedShows button{padding:7px 10px;margin:4px;border:1px solid #517c89;border-radius:4px;background:#234657;color:#e3f0f5;cursor:pointer;font-size:11px}" +
    ".arAuxRow{display:grid;grid-template-columns:64px 52px 90px 70px 1fr;align-items:center;gap:7px;padding:7px;background:#0c202b;border-bottom:1px solid #36515f;font-size:11px}.arAuxRow label{display:flex;gap:4px;align-items:center}.arAuxRow select{min-width:0;width:100%;background:#0c202b;color:#dcecf3;border:1px solid #496b7a;padding:6px;border-radius:4px}#arAuxReturns h3{font-size:12px;color:#b1d4dd;margin:14px 0 8px}#arAuxReturns p{color:#90afbc;font-size:10px;margin:4px 0 7px}";
  document.head.appendChild(css);
  // Signal Check is declared in the permanent toolbar; footer holds status only.
  const overlay = document.createElement("div");
  overlay.id = "arSignalOverlay";
  overlay.innerHTML = '<section class="arSignalPanel" role="dialog" aria-modal="true">' +
    '<div class="arSignalHeader"><h2>Live Signal Check · Channel 1 → Master</h2><button id="arSignalClose">CLOSE ×</button></div>' +
    '<div class="arSignalGrid">' +
      '<div class="arSignalStage"><small>01 · INPUT</small><b id="arInputName">Mic</b><div class="arSignalMeter"><i id="arInputBar"></i></div></div>' +
      '<div class="arSignalStage"><small>02 · POST FX</small><b id="arFxName">VST3</b><div class="arSignalMeter"><i id="arFxBar"></i></div></div>' +
      '<div class="arSignalStage"><small>03 · HARDWARE OUTPUT</small><b id="arOutputName">Headphone</b><div class="arSignalMeter"><i id="arOutputBar"></i></div></div>' +
    '</div><div id="arSignalReason">Reading native audio state...</div>' +
    '<div class="arSignalActions"><button id="arSignalMonitor">MUTE OUTPUT</button>' +
      '<button id="arSignalDry">DRY TEST OFF</button><button id="arSignalSetup">AUDIO SETUP</button>' +
    '<button id="arSignalLogs">OPEN DIAGNOSTIC LOG</button><button id="arOpenRoutingLab">ROUTING LAB · 10 CH</button></div>' +
    '<div id="arDiagnosticLab"><label for="arDiagnosticMode">P0 AUDIO ISOLATION</label>' +
      '<select id="arDiagnosticMode"><option value="NORMAL">NORMAL</option>' +
      '<option value="DRY_THROUGH">DRY THROUGH (0 FX)</option>' +
      '<option value="OUTPUT_ONLY_TONE">OUTPUT ONLY · 440 Hz -30 dBFS</option>' +
      '<option value="CAPTURE_ONLY">CAPTURE ONLY · SILENT OUTPUT</option></select>' +
      '<button id="arDiagnosticMark">MARK CRACKLE</button>' +
      '<button id="arSmartClockToggle" title="Experimental independent capture/render clock synchronization">SMART CLOCK: DIRECT</button>' +
      '<button id="arDiagnosticCopy" title="Copies native driver settings, recent events and timing data">COPY FULL REPORT</button>' +
      '<span id="arDiagnosticCopyStatus" role="status">One click → paste to ChatGPT</span></div>' +
    '<div id="arHealthPanel" style="padding:8px 0;font:11px monospace;color:#8ad6e0"></div>' +
    '<div id="arSignalFooter">The legacy Main L/R, Record and Streaming buttons are UI previews, not hardware routing. Select output using AUDIO SETUP. Use headphones at low volume.</div>' +
    '</section>';
  document.body.appendChild(overlay);
  // One anchored engineering inspector; no fake per-strip routing badges.
  // Selections are drafts until the native device validates and ACKs Apply.
  const routeOverlay=document.createElement("div");
  routeOverlay.id="arRoutingLabOverlay";
  routeOverlay.innerHTML='<section id="arRoutingLabPanel" role="dialog" aria-modal="true" aria-label="Native 10-channel dry routing">' +
    '<div class="arSignalHeader"><div><h2>ArSonMix · Live Input Routing LAB</h2><small id="arRouteIdentity">Reading actual device ports...</small></div><button class="arRouteClose" id="arRouteClose">CLOSE ×</button></div>' +
    '<p>Real hardware routing test: independent stereo-compatible VST3 insert per channel, PRE/POST sends, and BUS returns. No portable route recall. Use headphones at LOW volume. ' +
    'The normal CH1 VST3 host resumes after STOP. Every port shown is from the device active input mask.</p>' +
    '<div id="arRouteRows"></div><section id="arAuxReturns"><h3>4 DEDICATED AUX / FX RETURNS · INPUT CH1–10 REMAIN AVAILABLE</h3><p>Each AUX has a separate stereo return, VST3, level and MASTER routing. PRE/POST sends may target AUX1–4 without consuming CH strips.</p><div id="arAuxRows"></div></section><section id="arRouteSends"><h3>INTERNAL SENDS · PRE / POST FADER (MAX 32)</h3><div id="arSendRows"></div><button id="arAddSend">＋ ADD SEND</button></section>' +
    '<div id="arSavedShows"><label for="arSavedShowSelect">NATIVE SAVED SHOWS</label><select id="arSavedShowSelect"><option value="">Browse verified Shows...</option></select><button id="arShowBrowse">BROWSE SHOWS</button><button id="arShowRecall">RECALL SELECTED DRY</button><small>Current hardware ports only · output stays MUTED · no VST3 recall</small></div>' +
    '<div id="arRouteFeedback" role="status">Select channels, BUS returns and sends; APPLY is a native graph transaction.</div>' +
    '<div class="arRouteFoot"><button id="arRouteStop">STOP LAB</button><button id="arRouteSaveDry" title="Save acknowledged native route and current live controls, only when muted and with zero VST3">SAVE DRY SHOW</button><button id="arRouteApply">APPLY TO NATIVE AUDIO</button></div></section>';
  document.body.appendChild(routeOverlay);
  const draftRoutes=Array.from({length:10},()=>({mode:"NONE",left:-1,right:-1,gain:0.25,pan:0,main:true,mute:false,solo:false,fxId:""}));
  const draftAux=Array.from({length:4},()=>({enabled:false,gain:1,main:true,mute:false,fxId:""}));
  const draftSends=[];
  let draftEpoch=0; // identity of the hardware used to draw these selectors
  function invalidateDraftForNewDevice(nextEpoch){
    if(draftEpoch===nextEpoch)return false;
    draftEpoch=nextEpoch;
    for(const route of draftRoutes) {
      route.mode="NONE";route.left=-1;route.right=-1;route.gain=0.25;route.pan=0;
      route.main=true;route.mute=false;route.solo=false;route.fxId="";
    }
    for(const b of draftAux){b.enabled=false;b.gain=1;b.main=true;b.mute=false;b.fxId="";}
    draftSends.length=0;
    return true;
  }
  const safeRouteText=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const routeSelect=(port,value)=>'<option value="-1">—</option>'+port.map(p=>
    '<option value="'+Number(p.port)+'" '+(Number(p.port)===value?'selected':'')+'>'+
    safeRouteText(p.label||("Input "+(Number(p.port)+1)))+' (IN '+(Number(p.port)+1)+')</option>').join('');
  function fxChoices(selected){
    const options=['<option value="">DRY · no VST3</option>'];
    for(const p of catalogItems){
      // All names and IDs originate from native catalog; escaped for DOM.
      if(!p?.id)continue;
      options.push('<option value="'+safeRouteText(p.id)+'" '+(p.id===selected?'selected':'')+'>'+safeRouteText(p.name||p.id)+'</option>');
    }
    if(selected && !catalogItems.some(p=>p.id===selected))
      options.push('<option value="'+safeRouteText(selected)+'" selected>UNAVAILABLE · reselect VST3</option>');
    return options.join('');
  }
  api.addEventListener("arMixCatalog",message=>{
    const items=message?.items||message?.state?.items;
    if(!Array.isArray(items))return;
    catalogItems=items.filter(p=>p&&typeof p.id==="string");
    if(routeOverlay.classList.contains("open"))drawRoutes();
  });
  // Rehydrate only from the native ACK/SSOT when opening the editor.
  // Browsing a panel or reloading the WebView must never accidentally erase
  // an already published port/FX/send arrangement with stale UI defaults.
  function hydrateAcceptedRoute(){
    const d=state.dryRoutingLab||{},saved=d.acceptedRoute;
    invalidateDraftForNewDevice(Number(d.epoch)||0);
    if(!d.ready||!saved||Number(saved.deviceEpoch)!==Number(d.epoch)||
       !Array.isArray(saved.channels)||saved.channels.length!==10)return false;
    const keys=["mode","left","right","gain","pan","main","mute","solo","fxId"];
    for(let i=0;i<10;i++){
      const source=saved.channels[i];
      if(!source||typeof source!=="object")return false;
      for(const key of keys)
        if(Object.prototype.hasOwnProperty.call(source,key))
          draftRoutes[i][key]=source[key];
    }
    if(Array.isArray(saved.auxes)&&saved.auxes.length===4)
      for(let b=0;b<4;b++)for(const key of ["enabled","gain","main","mute","fxId"])
        if(Object.prototype.hasOwnProperty.call(saved.auxes[b]||{},key))draftAux[b][key]=saved.auxes[b][key];
    draftSends.length=0;
    if(Array.isArray(saved.sends))
      for(const send of saved.sends.slice(0,32))
        if(send&&typeof send==="object")draftSends.push({...send});
    return true;
  }
  function syncNativeLevelsToRouteDraft(){
    const d=state.dryRoutingLab||{};
    if(!d.active||!d.ready||!Array.isArray(d.channelControls))return;
    // Changes performed on the main mixer must survive the next STRUCTURAL
    // APPLY; never roll back a musician's fader/PAN when editing a send.
    for(let i=0;i<Math.min(10,d.channelControls.length);++i){
      const s=d.channelControls[i],r=draftRoutes[i];
      const db=Number(s?.db),pan=Number(s?.pan);
      if(Number.isFinite(db))
        r.gain=db<=-60?0:Math.min(4,Math.max(0,10**(db/20)));
      if(Number.isFinite(pan))r.pan=Math.min(1,Math.max(-1,pan/100));
      r.mute=!!s?.mute;
      r.solo=!!s?.solo;
    }
  }
  function routeGainChoices(current){
    const base=[[.125,'−18 dB'],[.25,'−12 dB'],[.5,'−6 dB'],[1,'0 dB']];
    const matched=base.some(([v])=>Math.abs(v-current)<.00001);
    if(!matched&&Number.isFinite(current)&&current>=0&&current<=4)
      base.push([current,(current<=0?'−∞':(20*Math.log10(current)).toFixed(1))+' dB · LIVE']);
    return base.map(([v,label])=>
      '<option value="'+v+'" '+(Math.abs(v-current)<.00001?'selected':'')+'>'+label+'</option>').join('');
  }
  function drawRoutes(){
    // An identical numbered input on another ASIO device is NOT the same
    // microphone. Clear all prior user intent if device epoch changed.
    invalidateDraftForNewDevice(Number(state.dryRoutingLab?.epoch)||0);
    const ports=Array.isArray(state.dryRoutingLab?.ports)?state.dryRoutingLab.ports:[];
    const rows=document.getElementById("arRouteRows");
    rows.innerHTML=Array.from({length:10},(_,i)=>{
      const v=draftRoutes[i];
      return '<div class="arRouteRow" data-route-row="'+i+'"><label>CH '+(i+1)+'<small class="arRoutePeak" data-route-peak="'+i+'"></small></label>'+
      '<select data-route-mode aria-label="CH '+(i+1)+' format">'+
      ['NONE','MONO','STEREO','BUS'].map(m=>'<option value="'+m+'" '+(v.mode===m?'selected':'')+'>'+m+'</option>').join('')+'</select>'+
      '<select data-route-left aria-label="CH '+(i+1)+' hardware left">'+routeSelect(ports,v.left)+'</select>'+
      '<select data-route-right aria-label="CH '+(i+1)+' hardware right">'+routeSelect(ports,v.right)+'</select>'+
      '<select data-route-gain aria-label="CH '+(i+1)+' test level">'+
      routeGainChoices(v.gain)+'</select>'+
      '<div class="arRouteFlags">'+
      '<label title="Send directly to MAIN"><input type="checkbox" data-route-main '+(v.main?'checked':'')+'>MAIN</label>'+
      '<label title="Mute channel and sends"><input type="checkbox" data-route-mute '+(v.mute?'checked':'')+'>M</label>'+
      '<label title="Solo channel with upstream/downstream sends"><input type="checkbox" data-route-solo '+(v.solo?'checked':'')+'>S</label>'+
      '</div><div class="arRoutePluginLine"><span>VST3 INSERT</span><select data-route-fx aria-label="CH '+(i+1)+' native VST3 insert">'+fxChoices(v.fxId)+'</select><small>one stereo-compatible instance · real FX</small></div></div>';
    }).join('');
    rows.querySelectorAll('[data-route-row]').forEach(row=>{
      const model=draftRoutes[Number(row.dataset.routeRow)];
      row.querySelector('[data-route-mode]').addEventListener('change',e=>{model.mode=e.target.value;refreshMode(row);});
      row.querySelector('[data-route-left]').addEventListener('change',e=>{model.left=Number(e.target.value);});
      row.querySelector('[data-route-right]').addEventListener('change',e=>{model.right=Number(e.target.value);});
      row.querySelector('[data-route-gain]').addEventListener('change',e=>{model.gain=Number(e.target.value);});
      row.querySelector('[data-route-main]').addEventListener('change',e=>{model.main=e.target.checked;});
      row.querySelector('[data-route-mute]').addEventListener('change',e=>{model.mute=e.target.checked;});
      row.querySelector('[data-route-solo]').addEventListener('change',e=>{model.solo=e.target.checked;});
      row.querySelector('[data-route-fx]').addEventListener('change',e=>{model.fxId=e.target.value;});
      refreshMode(row);
    });
    drawAux();
    drawSends();
    updateRouteLabIdentity();
  }
  function drawAux(){
    const parent=document.getElementById("arAuxRows");
    parent.innerHTML=draftAux.map((bus,i)=>'<div class="arAuxRow" data-aux-row="'+i+'">'+
      '<strong>AUX '+(i+1)+'</strong>'+
      '<label><input data-aux-enable type="checkbox" '+(bus.enabled?'checked':'')+'>ON</label>'+
      '<select data-aux-gain aria-label="AUX '+(i+1)+' level">'+routeGainChoices(bus.gain)+'</select>'+
      '<label><input data-aux-main type="checkbox" '+(bus.main?'checked':'')+'>MAIN</label>'+
      '<select data-aux-fx aria-label="AUX '+(i+1)+' native VST3">'+fxChoices(bus.fxId)+'</select>'+
      '</div>').join('');
    parent.querySelectorAll('[data-aux-row]').forEach(row=>{
      const bus=draftAux[Number(row.dataset.auxRow)];
      row.querySelector('[data-aux-enable]').addEventListener('change',e=>{bus.enabled=e.target.checked;});
      row.querySelector('[data-aux-gain]').addEventListener('change',e=>{bus.gain=Number(e.target.value);});
      row.querySelector('[data-aux-main]').addEventListener('change',e=>{bus.main=e.target.checked;});
      row.querySelector('[data-aux-fx]').addEventListener('change',e=>{bus.fxId=e.target.value;});
    });
  }
  function refreshMode(row){
    const type=row.querySelector('[data-route-mode]').value;
    row.querySelector('[data-route-left]').disabled=type==="NONE"||type==="BUS";
    row.querySelector('[data-route-right]').disabled=type!=="STEREO";
  }
  function drawSends(){
    const el=document.getElementById("arSendRows");
    const channels=Array.from({length:10},(_,i)=>'<option value="'+i+'">CH '+(i+1)+'</option>');
    const destinations=channels.concat(Array.from({length:4},(_,i)=>'<option value="'+(10+i)+'">AUX '+(i+1)+'</option>'));
    const listSelect=(options,value)=>options.map((option,i)=>option.replace('<option','<option '+(i===value?'selected':''))).join('');
    el.innerHTML=draftSends.map((send,index)=>
      '<div class="arSendRow" data-send-row="'+index+'">'+
      '<select data-send-from aria-label="Send source">'+listSelect(channels,send.from)+'</select>'+
      '<select data-send-to aria-label="Send destination">'+listSelect(destinations,send.to)+'</select>'+
      '<select data-send-tap aria-label="Send tap">'+['PRE','POST'].map(t=>'<option '+(t===send.tap?'selected':'')+'>'+t+'</option>').join('')+'</select>'+
      '<select data-send-gain aria-label="Send level">'+
      [[.125,'−18 dB'],[.25,'−12 dB'],[.5,'−6 dB'],[1,'0 dB']].map(([v,l])=>'<option value="'+v+'" '+(v===send.gain?'selected':'')+'>'+l+'</option>').join('')+'</select>'+
      '<button data-send-remove aria-label="Remove send" title="Remove send">×</button></div>').join('');
    el.querySelectorAll('[data-send-row]').forEach(row=>{
      const i=Number(row.dataset.sendRow),m=draftSends[i];
      row.querySelector('[data-send-from]').addEventListener('change',e=>{m.from=Number(e.target.value);});
      row.querySelector('[data-send-to]').addEventListener('change',e=>{m.to=Number(e.target.value);});
      row.querySelector('[data-send-tap]').addEventListener('change',e=>{m.tap=e.target.value;});
      row.querySelector('[data-send-gain]').addEventListener('change',e=>{m.gain=Number(e.target.value);});
      row.querySelector('[data-send-remove]').addEventListener('click',()=>{draftSends.splice(i,1);drawSends();});
    });
    document.getElementById("arAddSend").disabled=draftSends.length>=32;
  }
  document.getElementById("arAddSend").addEventListener('click',()=>{
    if(draftSends.length>=32)return;
    // Draft only; no sound changes until native acknowledgement.
    draftSends.push({from:0,to:1,tap:"POST",gain:.25});
    drawSends();
  });
  function updateRouteLabIdentity(){
    const d=state.dryRoutingLab||{};
    const label=document.getElementById("arRouteIdentity");
    if(!label)return;
    setTextIfChanged(label,(d.active ? (d.ready?"DRY LAB ACTIVE":"DRY LAB STALE — SILENT, REAPPLY"):"DRY LAB OFF")+
      " · device epoch "+(d.epoch||0)+" · rev "+(d.revision||0)+
      " · "+(d.ports?.length||0)+" enabled inputs · "+(d.outputPorts?.length||0)+" outputs · "+(d.sendCount||0)+" channel sends · "+(d.auxSendCount||0)+" AUX sends · "+(d.auxCount||0)+" AUX");
  }
  function openRoutingLab(){
    hydrateAcceptedRoute();
    syncNativeLevelsToRouteDraft();
    drawRoutes();
    routeOverlay.classList.add("open");
    overlay.classList.remove("open");
  }
  // Exposed only to the existing native SIGNAL FLOW GUI; does not publish a
  // routing transaction or bypass the output-MUTE operator safety gate.
  window.__arMixOpenRoutingLab=openRoutingLab;
  document.getElementById("arOpenRoutingLab").addEventListener("click",openRoutingLab);
  document.getElementById("arRouteClose").addEventListener("click",()=>routeOverlay.classList.remove("open"));
  routeOverlay.addEventListener("click",e=>{if(e.target===routeOverlay)routeOverlay.classList.remove("open");});
  document.getElementById("arRouteApply").addEventListener("click",()=>{
    const d=state.dryRoutingLab||{};
    if(!draftEpoch || draftEpoch!==Number(d.epoch)){
      document.getElementById("arRouteFeedback").textContent=
        "DEVICE CHANGED · old channel assignments discarded. Re-select the actual physical inputs.";
      drawRoutes();
      return; // must never submit old ports with a newly observed epoch
    }
    if(!Array.isArray(d.ports)||!d.ports.length){
      document.getElementById("arRouteFeedback").textContent="Select a real ASIO/WASAPI audio input in AUDIO SETUP first.";return;
    }
    document.getElementById("arRouteFeedback").textContent="Applying native route (output must be MUTED)...";
    api.emitEvent("arMixAction",{action:"routingDryApply",value:{
      deviceEpoch:draftEpoch,revision:Number(d.revision)||0,
      channels:draftRoutes.map(c=>({...c})),
      auxes:draftAux.map(b=>({...b})),
      sends:draftSends.map(s=>({...s}))
    }});
  });
  document.getElementById("arRouteStop").addEventListener("click",()=>api.emitEvent("arMixAction",{action:"routingDryStop"}));
  document.getElementById("arRouteSaveDry").addEventListener("click",()=>{
    const d=state.dryRoutingLab||{};
    if(!d.ready || !d.active || !d.acceptedRoute || state.monitor ||
       Number(d.fxCount)!==0){
      document.getElementById("arRouteFeedback").textContent=
        "SAVE DRY SHOW requires an ACKnowledged native route, MUTED OUTPUT and zero VST3; no changes saved.";
      return;
    }
    document.getElementById("arRouteFeedback").textContent=
      "Saving acknowledged 10 INPUT + 4 AUX DRY state to Studio/Shows (not a live recall)...";
    api.emitEvent("arMixAction",{action:"routingDryShowSave"});
  });
  const savedShowSelect=document.getElementById("arSavedShowSelect");
  document.getElementById("arShowBrowse").addEventListener("click",()=>{
    document.getElementById("arRouteFeedback").textContent="Reading native Studio/Shows metadata...";
    api.emitEvent("arMixAction",{action:"routingDryShowList"});
  });
  api.addEventListener("arMixNativeShowListAck",ack=>{
    if(!ack?.ok){
      document.getElementById("arRouteFeedback").textContent=
        "SHOW BROWSE FAILED · "+String(ack?.error||"Unknown storage error");return;
    }
    const selected=savedShowSelect.value;
    savedShowSelect.replaceChildren();
    const placeholder=document.createElement("option");
    placeholder.value="";placeholder.textContent="Select a saved native Show";
    savedShowSelect.appendChild(placeholder);
    for(const item of (Array.isArray(ack.items)?ack.items:[])){
      if(!item || typeof item.id!=="string")continue;
      const option=document.createElement("option");
      option.value=item.id;option.textContent=String(item.name||item.id);
      savedShowSelect.appendChild(option);
    }
    if(Array.from(savedShowSelect.options).some(o=>o.value===selected))
      savedShowSelect.value=selected;
    document.getElementById("arRouteFeedback").textContent=
      "NATIVE SHOW BROWSE · "+(savedShowSelect.options.length-1)+" saved Shows (full verification on recall)";
  });
  document.getElementById("arShowRecall").addEventListener("click",()=>{
    const showId=savedShowSelect.value;
    if(!showId){
      document.getElementById("arRouteFeedback").textContent=
        "Browse and choose a saved native Show first.";return;
    }
    const d=state.dryRoutingLab||{};
    if(!d.ready||!d.active||state.monitor||Number(d.fxCount)!==0) {
      document.getElementById("arRouteFeedback").textContent=
        "RECALL requires current native Routing LAB ACK, MUTED OUTPUT and zero VST3.";return;
    }
    document.getElementById("arRouteFeedback").textContent=
      "Reading and verifying saved Show on IO worker; output remains muted...";
    api.emitEvent("arMixAction",{action:"routingDryShowRecall",value:showId});
  });
  api.addEventListener("arMixNativeShowRecallAck",ack=>{
    if(ack?.state&&typeof ack.state==="object")state.dryRoutingLab=ack.state;
    document.getElementById("arRouteFeedback").textContent=ack?.ok
      ? "NATIVE SHOW RECALL ACK · verified routing published · OUTPUT STAYS MUTED"
      : "RECALL REJECTED · "+String(ack?.error||"Unknown native error");
    if(ack?.ok){hydrateAcceptedRoute();drawRoutes();}
    updateRouteLabIdentity();
  });
  api.addEventListener("arMixNativeShowSaveAck",ack=>{
    document.getElementById("arRouteFeedback").textContent=ack?.ok
      ? "NATIVE SHOW VERIFIED · "+String(ack.showId||"")+" · Studio/Shows"
      : "SHOW NOT SAVED · "+String(ack?.error||"Unknown storage failure");
    if(ack?.ok)api.emitEvent("arMixAction",{action:"routingDryShowList"});
  });
  api.addEventListener("arMixRoutingLabAck",reply=>{
    const result=reply?.state||reply||{};
    if(result && typeof result==="object" && "epoch" in result)state.dryRoutingLab=result;
    document.getElementById("arRouteFeedback").textContent=
      reply?.ok===false?("REJECTED · "+(reply.error||"Invalid route")):
      result.active?"NATIVE ACK: "+(result.fxCount||0)+" VST3 ("+(result.fxReused||0)+" preserved) · "+(result.auxCount||0)+" dedicated AUX · "+(result.auxSendCount||0)+" AUX sends · UNMUTE OUTPUT in SIGNAL CHECK":"NATIVE ACK: STOPPED · output remains MUTED for safety";
    updateRouteLabIdentity();
  });

  const el = id => document.getElementById(id);
  // 12Hz native telemetry must not replace unchanged text nodes or cause
  // needless main-thread paint/layout invalidations on static header labels.
  const setTextIfChanged=(node,value)=>{
    const text=String(value);
    if(node&&node.textContent!==text)node.textContent=text;
  };
  const percent = v => Math.max(1,Math.min(100,(20*Math.log10(Math.max(1e-6,Number(v)||0))+60)*100/60));
  // Evidence is input-side only; JUCE cannot report hardware render underruns.
  // Do not turn capture flags into fake "CPU overload" or restart devices.
  const captureFaultEvidence=d=>d?.connected && d?.diagnosticRoute==="NORMAL" &&
    !d?.adaptiveClockActive && Number(d?.chainCount)===0 &&
    d?.backendXrunMeaning==="WASAPI_CAPTURE_DISCONTINUITIES_ONLY" &&
    Number(d?.backendXrunCount)>=3 &&
    Number(d?.overBudgetCount)===0;
  function diagnosis(d) {
    if(d.dryRoutingLab?.active) return d.dryRoutingLab.ready?
      "Live 10-channel Routing LAB: "+(d.dryRoutingLab.fxCount||0)+" independent native VST3 inserts, PRE/POST sends and BUS returns. Use low headphone volume.":
      "Routing LAB device generation changed. Output is SILENT until you remap the real device and APPLY again.";
    if(d.diagnosticRoute==="OUTPUT_ONLY_TONE") return "Output-only test: capture endpoint is disconnected. Listen for tone at low headphone volume and click MARK CRACKLE when audible.";
    if(d.diagnosticRoute==="CAPTURE_ONLY") return "Capture-only test: hardware render endpoint is disconnected. Observe capture input and backend discontinuity count; you should hear no audio.";
    if(d.diagnosticRoute==="DRY_THROUGH") return "Dry-through test: microphone to output with all VST3 bypassed. MARK CRACKLE at audible glitches.";
    if (d.error) return "Audio driver error: "+d.error;
    if (!d.connected) return "No audio device connected. Open AUDIO SETUP.";
    if (!d.deviceOutputChannels) return "No active output channels. Select a headphone/speaker output in AUDIO SETUP.";
    if (!d.monitor) return "OUTPUT MUTED by user. Audio routes automatically after a working input/output device is selected.";
    if (d.masterMute) return "MASTER MUTED: switch off the master MUTE button.";
    if (d.channelMute) return "CHANNEL 1 MUTED: switch off Lead Vocal MUTE.";
    if(captureFaultEvidence(d))
      return "Capture dropouts recorded with NO FX and no DSP overruns ("+
        Math.floor(Number(d.backendXrunCount))+" capture flags). This is not proof of render underruns. " +
        "Compare DIRECT with TRY INDEPENDENT CLOCK for your VB-Cable → headphone route. " +
        "Switching reopens devices; use low headphone volume and change only when safe.";
    if ((d.inputPeak||0)>.006 && (d.postFxPeak||0)<.0002) return "Mic input is detected but VST3 output is silent. Enable DRY TEST to bypass all plugins.";
    if ((d.postFxPeak||0)>.003 && (d.peak||0)<.0002) return "Post-FX audio exists, but hardware output is silent. Check faders, mutes and output settings.";
    if ((d.peak||0)>.003) return "Samples reach the output buffer. If you cannot hear them, check your interface mixer, physical output selection and headphone volume.";
    if ((d.inputPeak||0)<.0005) return "Microphone signal is low/absent. Check the selected ASIO/WASAPI input and gain.";
    return "Output armed. Speak into the microphone and inspect all three meters.";
  }
  function repaint(d) {
    if(routeOverlay.classList.contains("open")){
      const levels=Array.isArray(d.dryRoutingLab?.channelPeaks)?d.dryRoutingLab.channelPeaks:[];
      routeOverlay.querySelectorAll('[data-route-peak]').forEach(el=>{
        const peak=Number(levels[Number(el.dataset.routePeak)])||0;
        setTextIfChanged(el,d.dryRoutingLab?.ready?(Math.round(Math.min(1,peak)*100)+"%"):"—");
      });
      if(draftEpoch!==(Number(d.dryRoutingLab?.epoch)||0)) {
        drawRoutes(); // fresh port names; reset stale user assignments
        document.getElementById("arRouteFeedback").textContent=
          "AUDIO DEVICE CHANGED · all input assignments reset. Select ports again.";
      } else updateRouteLabIdentity();
    }
    setTextIfChanged(el("arSignalBadge"),!d.monitor ? "OUTPUT MUTED" : d.masterMute ? "MASTER MUTED" : d.channelMute ? "CH1 MUTED" : "OUTPUT ARMED");
    el("arSignalBadge").style.color = d.monitor && !d.masterMute && !d.channelMute ? "#71e9cc" : "#e9be7c";
    const monitorButton=document.querySelector('[data-native-action="monitor"]');
    setTextIfChanged(monitorButton,d.monitor?"MUTE OUTPUT":"UNMUTE OUTPUT");
    // The original UI shell had fixed 48 kHz / 64-sample demo badges. Show
    // the REAL negotiated driver parameters instead of misleading the user.
    const rateLabel=document.querySelector('.spec:not(.spec-device):not(.bpm) strong');
    setTextIfChanged(rateLabel,d.rate?String(Math.round(d.rate/1000)):"—");
    const bufferLabel=document.querySelector('.spec.bpm strong');
    setTextIfChanged(bufferLabel,d.buffer?String(d.buffer):"—");
    const driverLabel=document.querySelector('.spec-device strong');
    setTextIfChanged(driverLabel,d.driver||"NO DRIVER");
    const liveLabel=document.querySelector('.status > span:first-child > b');
    setTextIfChanged(liveLabel,d.dryRoutingLab?.active?"NATIVE 10CH FX LAB":"NATIVE CH1");
    const edition=document.querySelector('.status > span:last-child');
    setTextIfChanged(edition,d.dryRoutingLab?.active?
      "AR MIX P1H · REAL 10CH FX / BUS SENDS":"AR MIX P2 · CH1 NATIVE + UI PREVIEW");
    if (!overlay.classList.contains("open")) return;
    el("arInputName").textContent=d.dryRoutingLab?.active?"NATIVE 10 CH INPUTS":d.diagnosticRoute==="OUTPUT_ONLY_TONE"?"INTERNAL TEST TONE":d.inputDevice||"Input device";
    el("arFxName").textContent=d.dryRoutingLab?.active?(String(d.dryRoutingLab.fxCount||0)+" NATIVE VST3"):d.diagnosticRoute==="OUTPUT_ONLY_TONE"?"NO INPUT / NO FX":d.diagnosticRoute!=="NORMAL"?"NO FX":d.dryTest?"DRY BYPASS":"VST3 Chain";
    el("arOutputName").textContent=d.outputDevice||"Output device";
    el("arInputBar").style.width=percent(d.dryRoutingLab?.active?Math.max(d.dryRoutingLab.peakL||0,d.dryRoutingLab.peakR||0):d.inputPeak)+"%";
    el("arFxBar").style.width=percent(d.postFxPeak)+"%";
    el("arOutputBar").style.width=percent(d.peak)+"%";
    el("arSignalReason").textContent=diagnosis(d);
    el("arDiagnosticMode").value=d.diagnosticRoute||"NORMAL";
    el("arSignalMonitor").textContent=d.monitor?"MUTE OUTPUT":"UNMUTE OUTPUT";
    el("arSignalDry").textContent=d.dryTest?"DRY TEST ON":"DRY TEST OFF";
    const trial=captureFaultEvidence(d);
    el("arSmartClockToggle").textContent=d.adaptiveClockActive?
      "SMART CLOCK: INDEPENDENT (BETA)":trial?"TRY INDEPENDENT CLOCK (BETA)":"SMART CLOCK: DIRECT";
    el("arSmartClockToggle").title=trial?
      "Explicit A/B test for recorded WASAPI capture dropouts. Opens separate capture/render devices; can add latency. No automatic switch.":
      "Manual independent capture/render clock control";
    el("arHealthPanel").textContent="CALLBACK "+(d.callbackDurationUs||0)+" µs / period "+
      Math.round(d.periodUs||0)+" µs | Worst "+(d.worstCallbackUs||0)+
      " µs | Over-budget "+(d.overBudgetCount||0)+
      " | Long gaps "+(d.callbackGapCount||0)+
      " | Late intervals (>1.5x) "+(d.lateCallbackIntervals||0)+
      " | DSP p95/p99 budget (upper bound) "+(d.callbackP95BeyondDeadline?">100":Number(d.callbackP95BudgetUpperPct??0).toFixed(1))+"/"+(d.callbackP99BeyondDeadline?">100":Number(d.callbackP99BudgetUpperPct??0).toFixed(1))+"%"+
      " | Worst VST "+(d.worstPluginUs||0)+" µs"+
      " | device starts/stops "+(d.deviceStartCount||0)+"/"+(d.deviceStopCount||0)+
      " | backend XRUN "+(d.backendXrunCount??"N/A")+
      " ("+(d.backendXrunMeaning||"UNKNOWN")+")"+
      " | Render underflow UNOBSERVABLE via JUCE (timing NOT xruns)"+
      " | Clock "+(d.adaptiveClockMode||"DIRECT")+
      (d.adaptiveClockActive?" | FIFO "+(d.bridgeOccupancyFrames||0)+"f"+
      " | bridge lost capture "+(d.bridgeOverflowFrames||0)+
      " | bridge missing render "+(d.bridgeUnderflowFrames||0)+
      " | drift "+Math.round(d.bridgeCorrectionPpm||0)+"ppm":"");
    const timing=Array.isArray(d.pluginTimings)?d.pluginTimings:[];
    const fxInfo=timing.map(x=>x.slot+". "+x.name+": "+x.lastUs+" µs now, worst "+x.worstUs+" µs").join(" | ");
    if(fxInfo)el("arHealthPanel").textContent+=" | FX: "+fxInfo;
    el("arSignalFooter").textContent=(d.driver||"Audio")+" · "+(d.rate||0)+" Hz / "+(d.buffer||0)+
      " samples · "+(d.deviceInputChannels||0)+" active input / "+(d.deviceOutputChannels||0)+" active output channels · "+
      (d.latencySamples||0)+" samples plugin latency. Choose hardware output using AUDIO SETUP; Main L/R and Streaming are UI previews.";
  }
  // Only a fully initialized WebView2 ArSonMix page sends this. An old-IE
  // navigation error can never mark the native engine GUI as loaded.
  api.emitEvent("arMixGuiReady",{protocol:1});
  api.addEventListener("arMixStatus",d=>{state=d;repaint(d);});
  el("arSignalOpen")?.addEventListener("click",()=>{overlay.classList.add("open");repaint(state);});
  el("arSignalClose").addEventListener("click",()=>overlay.classList.remove("open"));
  el("arSignalMonitor").addEventListener("click",()=>api.emitEvent("arMixAction",{action:"monitor",value:!state.monitor}));
  el("arSignalDry").addEventListener("click",()=>api.emitEvent("arMixAction",{action:"dryTest",value:!state.dryTest}));
  el("arDiagnosticMode").addEventListener("change",e=>
    api.emitEvent("arMixAction",{action:"diagnosticRoute",value:e.target.value}));
  el("arDiagnosticMark").addEventListener("click",()=>
    api.emitEvent("arMixAction",{action:"diagnosticMark"}));
  el("arSmartClockToggle").addEventListener("click",()=>
    api.emitEvent("arMixAction",{action:"smartClock",value:!state.adaptiveClockActive}));
  el("arDiagnosticCopy").addEventListener("click",()=>{
    el("arDiagnosticCopyStatus").textContent="Preparing report...";
    api.emitEvent("arMixAction",{action:"copyDiagnosticReport"});
  });
  api.addEventListener("arMixDiagnosticReportCopied",d=>{
    el("arDiagnosticCopyStatus").textContent="COPIED ("+Math.round((Number(d.bytes)||0)/1024)+" KB) · paste into chat";
  });
  el("arSignalLogs").addEventListener("click",()=>api.emitEvent("arMixAction",{action:"openDiagnostics"}));
  el("arSignalSetup").addEventListener("click",()=>{
    overlay.classList.remove("open");
    document.querySelector('#audioSetupBtn')?.click();
  });
  overlay.addEventListener("click",e=>{if(e.target===overlay)overlay.classList.remove("open");});
  document.addEventListener("click",e=>{
    if(!e.target.closest("[data-output],#monitorSource"))return;
    e.preventDefault();e.stopImmediatePropagation();
    overlay.classList.add("open");repaint(state);
  },true);
})();
