// SPDX-License-Identifier: AGPL-3.0-only
// Native output diagnostics: JS control only, never audio callback work.
(() => {
  const api = window.__JUCE__?.backend;
  if (!api?.emitEvent || !api?.addEventListener) return;
  let state = {};
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
    ".arRouteRow{display:grid;grid-template-columns:60px 105px 1fr 1fr 110px;gap:6px;align-items:center;padding:5px 0;border-bottom:1px solid #2d4653}" +
    ".arRouteRow label{font-size:11px;font-weight:600}.arRouteRow select{width:100%;min-width:0;padding:6px;background:#0d202b;border:1px solid #4d6879;border-radius:4px;color:#e1eef5;font-size:11px}" +
    ".arRouteFoot{display:flex;gap:8px;flex-wrap:wrap;justify-content:flex-end;margin-top:12px}.arRouteFoot button,.arRouteClose{color:#f0f7fa;background:#24475a;border:1px solid #587f91;border-radius:4px;padding:8px 12px;cursor:pointer}" +
    "#arRouteApply{background:#237566;border-color:#4ba48d}#arRouteStop{background:#50343c;border-color:#a8717c}" +
    "#arRouteFeedback{font-size:11px;min-height:18px;color:#8bdec1;margin-top:10px}";
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
    '<p>This is a real native hardware test with DRY DSP ONLY: no VST3 chains, AUX sends, or portable route recall. Use headphones at LOW volume. ' +
    'The normal CH1 VST3 host resumes after STOP. Every port shown is from the device active input mask.</p>' +
    '<div id="arRouteRows"></div><div id="arRouteFeedback" role="status">Select channels and APPLY; unassigned channels stay silent.</div>' +
    '<div class="arRouteFoot"><button id="arRouteStop">STOP LAB</button><button id="arRouteApply">APPLY TO NATIVE AUDIO</button></div></section>';
  document.body.appendChild(routeOverlay);
  const draftRoutes=Array.from({length:10},()=>({mode:"NONE",left:-1,right:-1,gain:0.25}));
  const safeRouteText=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const routeSelect=(port,value)=>'<option value="-1">—</option>'+port.map(p=>
    '<option value="'+Number(p.port)+'" '+(Number(p.port)===value?'selected':'')+'>'+
    safeRouteText(p.label||("Input "+(Number(p.port)+1)))+' (IN '+(Number(p.port)+1)+')</option>').join('');
  function drawRoutes(){
    const ports=Array.isArray(state.dryRoutingLab?.ports)?state.dryRoutingLab.ports:[];
    const rows=document.getElementById("arRouteRows");
    rows.innerHTML=Array.from({length:10},(_,i)=>{
      const v=draftRoutes[i];
      return '<div class="arRouteRow" data-route-row="'+i+'"><label>CH '+(i+1)+'</label>'+
      '<select data-route-mode aria-label="CH '+(i+1)+' format">'+
      ['NONE','MONO','STEREO'].map(m=>'<option value="'+m+'" '+(v.mode===m?'selected':'')+'>'+m+'</option>').join('')+'</select>'+
      '<select data-route-left aria-label="CH '+(i+1)+' hardware left">'+routeSelect(ports,v.left)+'</select>'+
      '<select data-route-right aria-label="CH '+(i+1)+' hardware right">'+routeSelect(ports,v.right)+'</select>'+
      '<select data-route-gain aria-label="CH '+(i+1)+' test level">'+
      [[.125,'−18 dB'],[.25,'−12 dB'],[.5,'−6 dB'],[1,'0 dB']].map(([a,label])=>
        '<option value="'+a+'" '+(v.gain===a?'selected':'')+'>'+label+'</option>').join('')+'</select></div>';
    }).join('');
    rows.querySelectorAll('[data-route-row]').forEach(row=>{
      const model=draftRoutes[Number(row.dataset.routeRow)];
      row.querySelector('[data-route-mode]').addEventListener('change',e=>{model.mode=e.target.value;refreshMode(row);});
      row.querySelector('[data-route-left]').addEventListener('change',e=>{model.left=Number(e.target.value);});
      row.querySelector('[data-route-right]').addEventListener('change',e=>{model.right=Number(e.target.value);});
      row.querySelector('[data-route-gain]').addEventListener('change',e=>{model.gain=Number(e.target.value);});
      refreshMode(row);
    });
    updateRouteLabIdentity();
  }
  function refreshMode(row){
    const type=row.querySelector('[data-route-mode]').value;
    row.querySelector('[data-route-left]').disabled=type==="NONE";
    row.querySelector('[data-route-right]').disabled=type!=="STEREO";
  }
  function updateRouteLabIdentity(){
    const d=state.dryRoutingLab||{};
    const label=document.getElementById("arRouteIdentity");
    if(!label)return;
    setTextIfChanged(label,(d.active ? (d.ready?"DRY LAB ACTIVE":"DRY LAB STALE — SILENT, REAPPLY"):"DRY LAB OFF")+
      " · device epoch "+(d.epoch||0)+" · rev "+(d.revision||0)+
      " · "+(d.ports?.length||0)+" enabled physical inputs");
  }
  document.getElementById("arOpenRoutingLab").addEventListener("click",()=>{
    drawRoutes();
    routeOverlay.classList.add("open");
    overlay.classList.remove("open");
  });
  document.getElementById("arRouteClose").addEventListener("click",()=>routeOverlay.classList.remove("open"));
  routeOverlay.addEventListener("click",e=>{if(e.target===routeOverlay)routeOverlay.classList.remove("open");});
  document.getElementById("arRouteApply").addEventListener("click",()=>{
    const d=state.dryRoutingLab||{};
    if(!Array.isArray(d.ports)||!d.ports.length){
      document.getElementById("arRouteFeedback").textContent="Select a real ASIO/WASAPI audio input in AUDIO SETUP first.";return;
    }
    document.getElementById("arRouteFeedback").textContent="Applying to actual native DSP...";
    api.emitEvent("arMixAction",{action:"routingDryApply",value:{
      deviceEpoch:Number(d.epoch)||0,revision:Number(d.revision)||0,
      channels:draftRoutes.map(c=>({...c}))
    }});
  });
  document.getElementById("arRouteStop").addEventListener("click",()=>api.emitEvent("arMixAction",{action:"routingDryStop"}));
  api.addEventListener("arMixRoutingLabAck",reply=>{
    const result=reply?.state||reply||{};
    document.getElementById("arRouteFeedback").textContent=
      reply?.ok===false?("REJECTED · "+(reply.error||"Invalid route")):
      result.active?"NATIVE ACK: DRY LAB ENABLED · zero VST3 · low headphone volume":"NATIVE ACK: DRY LAB STOPPED";
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
    if(routeOverlay.classList.contains("open"))updateRouteLabIdentity();
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
    setTextIfChanged(liveLabel,"NATIVE CH1");
    const edition=document.querySelector('.status > span:last-child');
    setTextIfChanged(edition,"AR MIX P2 · CH1 NATIVE + UI PREVIEW");
    if (!overlay.classList.contains("open")) return;
    el("arInputName").textContent=d.diagnosticRoute==="OUTPUT_ONLY_TONE"?"INTERNAL TEST TONE":d.inputDevice||"Input device";
    el("arFxName").textContent=d.diagnosticRoute==="OUTPUT_ONLY_TONE"?"NO INPUT / NO FX":d.diagnosticRoute!=="NORMAL"?"NO FX":d.dryTest?"DRY BYPASS":"VST3 Chain";
    el("arOutputName").textContent=d.outputDevice||"Output device";
    el("arInputBar").style.width=percent(d.inputPeak)+"%";
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
