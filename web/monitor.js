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
    "#arSmartProfile{background:#142a36;color:#c5eaf6;border:1px solid #598496;border-radius:4px;padding:6px;font-size:11px}" +
    "#arSmartStats{margin:7px 0 2px;color:#9de4cd;font:11px monospace;overflow-wrap:anywhere}";
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
    '<button id="arSignalLogs">OPEN DIAGNOSTIC LOG</button></div>' +
    '<div id="arDiagnosticLab"><label for="arSmartProfile">SMART AUDIO</label>' +
      '<select id="arSmartProfile" aria-label="Smart audio routing profile">' +
      '<option value="AUTO">AUTO · ADAPTIVE</option><option value="STABLE">STABLE · MORE HEADROOM</option>' +
      '<option value="LOW_LATENCY">LOW LATENCY</option><option value="DIRECT">DIRECT · NO SRC</option></select></div>' +
    '<div id="arSmartStats" role="status"></div>' +
    '<div id="arDiagnosticLab"><label for="arDiagnosticMode">P0 AUDIO ISOLATION</label>' +
      '<select id="arDiagnosticMode"><option value="NORMAL">NORMAL</option>' +
      '<option value="DRY_THROUGH">DRY THROUGH (0 FX)</option>' +
      '<option value="OUTPUT_ONLY_TONE">OUTPUT ONLY · 440 Hz -30 dBFS</option>' +
      '<option value="CAPTURE_ONLY">CAPTURE ONLY · SILENT OUTPUT</option></select>' +
      '<button id="arDiagnosticMark">MARK CRACKLE</button>' +
      '<button id="arDiagnosticCopy" title="Copies native driver settings, recent events and timing data">COPY FULL REPORT</button>' +
      '<span id="arDiagnosticCopyStatus" role="status">One click → paste to ChatGPT</span></div>' +
    '<div id="arHealthPanel" style="padding:8px 0;font:11px monospace;color:#8ad6e0"></div>' +
    '<div id="arSignalFooter">The legacy Main L/R, Record and Streaming buttons are UI previews, not hardware routing. Select output using AUDIO SETUP. Use headphones at low volume.</div>' +
    '</section>';
  document.body.appendChild(overlay);
  const el = id => document.getElementById(id);
  const percent = v => Math.max(1,Math.min(100,(20*Math.log10(Math.max(1e-6,Number(v)||0))+60)*100/60));
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
    if ((d.inputPeak||0)>.006 && (d.postFxPeak||0)<.0002) return "Mic input is detected but VST3 output is silent. Enable DRY TEST to bypass all plugins.";
    if ((d.postFxPeak||0)>.003 && (d.peak||0)<.0002) return "Post-FX audio exists, but hardware output is silent. Check faders, mutes and output settings.";
    if ((d.peak||0)>.003) return "Samples reach the output buffer. If you cannot hear them, check your interface mixer, physical output selection and headphone volume.";
    if ((d.inputPeak||0)<.0005) return "Microphone signal is low/absent. Check the selected ASIO/WASAPI input and gain.";
    return "Output armed. Speak into the microphone and inspect all three meters.";
  }
  function repaint(d) {
    el("arSignalBadge").textContent = !d.monitor ? "OUTPUT MUTED" : d.masterMute ? "MASTER MUTED" : d.channelMute ? "CH1 MUTED" : "OUTPUT ARMED";
    el("arSignalBadge").style.color = d.monitor && !d.masterMute && !d.channelMute ? "#71e9cc" : "#e9be7c";
    const monitorButton=document.querySelector('[data-native-action="monitor"]');
    if (monitorButton) monitorButton.textContent=d.monitor?"MUTE OUTPUT":"UNMUTE OUTPUT";
    // The original UI shell had fixed 48 kHz / 64-sample demo badges. Show
    // the REAL negotiated driver parameters instead of misleading the user.
    const rateLabel=document.querySelector('.spec:not(.spec-device):not(.bpm) strong');
    if(rateLabel)rateLabel.textContent=d.rate?String(Math.round(d.rate/1000)):"—";
    const bufferLabel=document.querySelector('.spec.bpm strong');
    if(bufferLabel)bufferLabel.textContent=d.buffer?String(d.buffer):"—";
    const driverLabel=document.querySelector('.spec-device strong');
    if(driverLabel)driverLabel.textContent=d.driver||"NO DRIVER";
    const liveLabel=document.querySelector('.status > span:first-child > b');
    if(liveLabel)liveLabel.textContent="NATIVE CH1";
    const edition=document.querySelector('.status > span:last-child');
    if(edition)edition.textContent="AR MIX P2 · CH1 NATIVE + UI PREVIEW";
    if (!overlay.classList.contains("open")) return;
    el("arInputName").textContent=d.diagnosticRoute==="OUTPUT_ONLY_TONE"?"INTERNAL TEST TONE":d.inputDevice||"Input device";
    el("arFxName").textContent=d.diagnosticRoute==="OUTPUT_ONLY_TONE"?"NO INPUT / NO FX":d.diagnosticRoute!=="NORMAL"?"NO FX":d.dryTest?"DRY BYPASS":"VST3 Chain";
    el("arOutputName").textContent=d.outputDevice||"Output device";
    el("arInputBar").style.width=percent(d.inputPeak)+"%";
    el("arFxBar").style.width=percent(d.postFxPeak)+"%";
    el("arOutputBar").style.width=percent(d.peak)+"%";
    el("arSignalReason").textContent=diagnosis(d);
    el("arDiagnosticMode").value=d.diagnosticRoute||"NORMAL";
    el("arSmartProfile").value=d.smartUserProfile||"AUTO";
    const bridged=d.smartMode==="DUAL_CLOCK_BRIDGED";
    el("arSmartStats").textContent=bridged
      ? "LIVE SPLIT CLOCKS · capture "+(d.bridgeCaptureRate||0)+" Hz → render "+(d.bridgeRenderRate||0)+
        " Hz · FIFO "+(d.bridgeFillFrames||0)+" fr · drift "+Number(d.bridgeCorrectionPpm||0).toFixed(1)+
        " ppm · src target "+(d.smartTargetLatencyMs||0)+" ms · underflow "+(d.bridgeConsumerUnderflowFrames||0)+
        " fr · overflow "+(d.bridgeProducerOverflowFrames||0)+" fr"
      : "DIRECT JUCE · no added SRC · supported ASIO/shared device fast path";
    el("arSignalMonitor").textContent=d.monitor?"MUTE OUTPUT":"UNMUTE OUTPUT";
    el("arSignalDry").textContent=d.dryTest?"DRY TEST ON":"DRY TEST OFF";
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
      " | Render underflow UNOBSERVABLE via JUCE (timing NOT xruns)";
    const timing=Array.isArray(d.pluginTimings)?d.pluginTimings:[];
    const fxInfo=timing.map(x=>x.slot+". "+x.name+": "+x.lastUs+" µs now, worst "+x.worstUs+" µs").join(" | ");
    if(fxInfo)el("arHealthPanel").textContent+=" | FX: "+fxInfo;
    el("arSignalFooter").textContent=(d.driver||"Audio")+" · "+(d.rate||0)+" Hz / "+(d.buffer||0)+
      " samples · "+(d.deviceInputChannels||0)+" active input / "+(d.deviceOutputChannels||0)+" active output channels · "+
      (d.latencySamples||0)+" samples plugin latency. Choose hardware output using AUDIO SETUP; Main L/R and Streaming are UI previews.";
  }
  api.addEventListener("arMixStatus",d=>{state=d;repaint(d);});
  el("arSignalOpen")?.addEventListener("click",()=>{overlay.classList.add("open");repaint(state);});
  el("arSignalClose").addEventListener("click",()=>overlay.classList.remove("open"));
  el("arSignalMonitor").addEventListener("click",()=>api.emitEvent("arMixAction",{action:"monitor",value:!state.monitor}));
  el("arSignalDry").addEventListener("click",()=>api.emitEvent("arMixAction",{action:"dryTest",value:!state.dryTest}));
  el("arDiagnosticMode").addEventListener("change",e=>
    api.emitEvent("arMixAction",{action:"diagnosticRoute",value:e.target.value}));
  el("arSmartProfile").addEventListener("change",e=>
    api.emitEvent("arMixAction",{action:"smartAudioProfile",value:e.target.value}));
  el("arDiagnosticMark").addEventListener("click",()=>
    api.emitEvent("arMixAction",{action:"diagnosticMark"}));
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
