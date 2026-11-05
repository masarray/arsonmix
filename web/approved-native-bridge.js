// SPDX-License-Identifier: AGPL-3.0-only
// ONE approved /preview/ DOM, native authority only. No new UI, no JS PCM.
(() => {
  "use strict";
  const backend = window.__JUCE__?.backend;
  const protocol = window.ArSonApprovedNativeContract;
  // The public GUI owns one lexical model. Native-only bundle exports a narrow
  // reference to that SAME model; never reconstruct a parallel DOM/DSP.
  const scope = window.__ARMIX_NATIVE_SCOPE__ || null;
  const connected = Boolean(backend?.emitEvent && backend?.addEventListener && protocol);
  // The old M1 wording is retained as an explicit boot-time fallback for
  // release-train compatibility, NOT a claim that a demo graph is audio.
  const fallback = "M1_VISUAL_BUNDLE_ONLY__NOT_AUDIO_READY";
  const flags = {acceptedGraphConnected:false,productionCallbackLive:false};
  const nativeByUiId = new Map();       // SMALL UI ordinal -> EXACT decimal-string native ChannelId
  const uiByNativeId = new Map();       // EXACT uint64 string -> local view-only ordinal
  const wireByUiId = new Map();         // view-only EdgeId -> EXACT decimal-string native EdgeId
  const uiByWireId = new Map();
  let lastPaintedSignature = "", lastNativeError = "NATIVE_NOT_SUBSCRIBED";
  let controller = null;
  const status = message => {
    lastNativeError = message;
    const info = document.getElementById("info");
    if (info) info.textContent = message;
  };
  const stopDemo = () => {
    // The public Pages demo remains unchanged. In native, DEMO meters and
    // reference-only physical operations are never passed off as live audio.
    const demo = document.getElementById("preview");
    if (demo) {demo.disabled = true;demo.title = "Demo meters disabled in native Windows application";}
    scope?.stopDemo?.();
    document.documentElement.dataset.armixNativeAuthority =
      flags.productionCallbackLive ? "product-live" :
      flags.acceptedGraphConnected ? "pcm-accepted" : "not-admitted";
  };
  const nativeIdFor = t => nativeByUiId.get(t?.id) || null;
  const intent = (operation,t,payload) => {
    if (!flags.acceptedGraphConnected) {
      status("NATIVE: accepted graph not projected; control unchanged");
      return false;
    }
    const id = nativeIdFor(t);
    const result = controller?.intent(operation,id,payload) || {ok:false,error:"NATIVE_UNAVAILABLE"};
    if (!result.ok) status("NATIVE: "+result.error+" — controls unchanged");
    else window.setTimeout(()=>controller?.expire(result.requestId),8000);
    return result.ok;
  };
  function projection(native) {
    if (!native?.admitted || !Array.isArray(native.graph.edges) ||
        !scope?.make || !scope?.render || !scope?.wireGraph ||
        !scope?.installGraph || !scope?.allocateWireId) return false;
    const accepted = native.graph;
    // No fake default fader/mute/solo when the producer omitted real readback.
    if (accepted.channels.some(ch => !ch.controls ||
        !["faderDb","pan","mute","solo"].every(k => Object.hasOwn(ch.controls,k)))) return false;
    const previous = new Map(scope.tracks.concat(scope.master).map(t=>[t.id,t]));
    const nextTracks = [], localByNative = new Map(), nextNativeByUi = new Map();
    let nextMaster = null;
    for (const [index,ch] of accepted.channels.entries()) {
      const kind = ch.role==="MAIN"?"master":ch.role==="GROUP"?"bus":
        ch.role==="FX_RETURN"?"fx":"audio";
      const savedId = uiByNativeId.get(ch.id);
      const t = savedId !== undefined && previous.has(savedId) ?
        previous.get(savedId) : scope.make(ch.name,kind,scope.colors[index%scope.colors.length]||"#568ead");
      if (savedId !== undefined) t.id = savedId;
      // No Number(ch.id): all native identities stay canonical uint64 strings.
      localByNative.set(ch.id,t.id);
      nextNativeByUi.set(t.id,ch.id);
      t.name=ch.name;t.kind=kind;
      t.format=ch.processingFormat.toLowerCase();
      t.level=ch.controls.faderDb;
      // Native PAN is normalized [-1,+1]; approved fader console uses [-100,+100].
      t.pan=ch.controls.pan*100;
      t.mute=ch.controls.mute;
      t.solo=ch.controls.solo;
      t.selected=previous.get(t.id)?.selected || false;
      if (Array.isArray(ch.inserts)) {
        t.fx=Array.from({length:8},(_,i)=>ch.inserts[i]?.name||"None");
        t.slotBypass=Array.from({length:8},(_,i)=>Boolean(ch.inserts[i]?.bypassed));
        t.fxInsertIds=Array.from({length:8},(_,i)=>ch.inserts[i]?
          {name:ch.inserts[i].name,id:ch.inserts[i].id}:null);
      }
      if (ch.role==="MAIN") nextMaster=t;
      else nextTracks.push(t);
    }
    if (!nextMaster || nextMaster.id !== localByNative.get(accepted.mainChannelId)) return false;
    const edges = [], newWireByUi = new Map(), newUiByWire = new Map();
    for (const edge of accepted.edges) {
      if (!edge.enabled) continue;
      const src=localByNative.get(edge.from), dst=localByNative.get(edge.to);
      if (!src || !dst || edge.from===accepted.mainChannelId) return false;
      const source=accepted.channels.find(ch=>ch.id===edge.from);
      const isMain=edge.to===accepted.mainChannelId;
      const isPrimary=source?.primaryOutId===edge.to;
      const kind=isMain||isPrimary?"primary":edge.tap==="SPLIT_PRE"||edge.tap==="SPLIT_POST"?"split":"send";
      const mode=isMain?"postFader":kind==="primary"?"routeOnly":
        kind==="split"?(edge.tap==="SPLIT_PRE"?"splitPre":"splitPost"):"send";
      if (isMain && edge.gainLinear!==1) return false;
      const gainDb=isMain?0:Math.round(Math.max(-60,Math.min(6,
        edge.gainLinear>0?20*Math.log10(edge.gainLinear):-60))*10)/10;
      let uiId=uiByWireId.get(edge.edgeId);
      if (uiId===undefined) uiId=scope.allocateWireId();
      newWireByUi.set(uiId,edge.edgeId);
      newUiByWire.set(edge.edgeId,uiId);
      edges.push({id:uiId,sourceId:src,destId:dst,kind,mode,
        enabled:true,levelDb:gainDb});
    }
    if (!scope.wireGraph.validateCandidateGraph(nextTracks,nextMaster.id,edges).ok) return false;
    const previouslySelected = nextTracks.filter(t=>t.selected);
    if (!previouslySelected.length && nextTracks.length) nextTracks[0].selected=true;
    // One atomic accepted readback projection. Demo wire reducer NEVER runs.
    for (const [id,ui] of localByNative) uiByNativeId.set(id,ui);
    nativeByUiId.clear();for(const [ui,id] of nextNativeByUi)nativeByUiId.set(ui,id);
    wireByUiId.clear();for(const [ui,id] of newWireByUi)wireByUiId.set(ui,id);
    uiByWireId.clear();for(const [id,ui] of newUiByWire)uiByWireId.set(id,ui);
    scope.installGraph(nextTracks,nextMaster,edges);
    return true;
  }
  // Native telemetry owns the physical meter. When no measured per-channel
  // samples are published, leave the meter dark; never use Pages sine demo.
  function paintNativeMeters(next) {
    if (!scope?.displayMeterDbfs)return;
    const data=next.telemetry?.channelMeterRmsPeak;
    if (!data || typeof data!=="object")return;
    const ts=performance.now();
    for (const ch of next.graph.channels) {
      const ui=uiByNativeId.get(ch.id);
      if (ui===undefined)continue;
      const root=document.querySelector('.strip[data-id="'+ui+'"]');
      if (!root)continue;
      const entry=data[ch.id];
      const one=typeof entry==="number"&&Number.isFinite(entry)&&entry>=0?entry:null;
      const l=typeof entry?.peakL==="number"&&Number.isFinite(entry.peakL)&&entry.peakL>=0?
        entry.peakL:one;
      const right=typeof entry?.peakR==="number"&&Number.isFinite(entry.peakR)&&entry.peakR>=0?
        entry.peakR:ch.processingFormat==="MONO"?l:null;
      const paint=(side,peak)=>{
        const fill=root.querySelector('.meter-fill.'+side);
        if(!fill)return;
        const db=peak===null || peak===0?-90:20*Math.log10(peak);
        scope.displayMeterDbfs(fill,Math.max(-90,Math.min(6,db)),ts,ui+':'+(side==="left"?"L":"R"));
      };
      paint("left",l);
      paint("right",right);
    }
  }

  // Exactly one device configuration flow: reuse the existing custom
  // Audio I/O panel, not a second large JUCE AudioDeviceSelectorComponent.
  // It remains available as an Advanced driver-recovery tool in native host.
  let hardware=null, hardwareNote=null, lastDryReadback=null;
  const legacy=()=>hardware?.raw?.legacyM4;
  const deviceReady=()=>!!(hardware?.device?.backend &&
    hardware.device.sampleRate>0 &&
    hardware.device.enabledInputs.length>0 &&
    hardware.device.enabledOutputs.length>=2 && legacy()?.active===true);
  const verifiedM4=()=>{
    const d=lastDryReadback;
    return deviceReady() && legacy()?.routeAccepted===true &&
      d?.ready===true && d?.hasAcceptedRoute===true && !d?.topologyFault &&
      String(d.epoch)===hardware.device.epoch &&
      String(d.revision)===legacy().routingRevision;
  };
  function updateHardwarePanel(next) {
    hardware=next||null;
    const d=next?.device;
    const deviceOpen=Boolean(d?.backend && d.sampleRate>0 &&
      d.enabledOutputs.length>=2);
    const label=document.getElementById("transport");
    if(label)label.textContent=!deviceOpen?"Choose audio device":
      d.outputArmed?"MAIN monitoring on":"MAIN monitoring off";
    if(ioMonitorButton){
      ioMonitorButton.textContent=d?.outputArmed?"Monitor on":"Monitor off";
      ioMonitorButton.dataset.on=String(!!d?.outputArmed);
    }
    const warning=document.querySelector?.(".status-bar .warning");
    if(warning)warning.textContent=!deviceOpen?"CHOOSE AUDIO OUTPUT":
      d.outputArmed?(next.admitted?"NATIVE PCM · HARDWARE FAT PENDING":
        "MAIN MONITOR ON · VERIFY SOUND"):
      "CHOOSE CHANNEL IN · THEN MONITOR";
    if(hardwareNote)hardwareNote.textContent=!deviceOpen?
      "Audio I/O: choose the hardware input and actual listening/output device.":
      "Choose IN on any mixer channel. Source and monitoring are committed by native readback.";
  }
  function installFirstSoundMenu() {
    const menu=document.querySelector?.("#moreTools .more-tools-panel");
    if(!menu || typeof document.createElement!=="function")return;
    const caption=document.createElement("span");
    caption.className="more-tools-caption";
    caption.textContent="AUDIO · DEVICE SETTINGS";
    const setup=document.createElement("button");
    setup.className="tool";setup.type="button";setup.textContent="Audio I/O Settings";
    setup.dataset.nativeAction="audioSetup";
    setup.addEventListener("click",()=>{
      // Event may be intercepted by the existing mixer-io.js capture handler.
      // When handled there, do not emit the old JUCE setup command.
      if(window.__arMixOpenAudioIO){
        window.__arMixOpenAudioIO("device");
        status("Choose actual capture and playback devices in Audio I/O.");
      }else status("Audio I/O panel unavailable; do not open a second setup wizard.");
    });
    hardwareNote=document.createElement("span");
    hardwareNote.className="more-tools-caption";
    hardwareNote.style.whiteSpace="normal";
    menu.append(caption,setup,hardwareNote);
    updateHardwarePanel(hardware);
  }
  // Compact Audio I/O is native-only. The EXISTING JUCE AudioDeviceManager
  // remains the single owner, via its already-tested inventory/Apply APIs.
  // No AudioDeviceSelectorComponent popup or additional device callback in the
  // normal workflow; the stock JUCE selector remains Advanced recovery.
  let ioRoot=null,ioDraft=null,ioInventory=null,ioBusy=false,
    ioMutePending=false,ioRequest="",ioCounter=0,ioMessage="",
    ioMonitorButton=null;
  const ioElement=(tag,cls,label)=>{
    const n=document.createElement(tag);
    if(cls)n.className=cls;if(label!==undefined)n.textContent=label;
    if(tag==="button")n.type="button";
    return n;
  };
  const ioType=()=>ioInventory?.types?.find(t=>t.name===ioDraft?.backend);
  const ioField=(parent,id,label)=>{
    const row=ioElement("div","ar-native-io-field");
    const text=ioElement("label","",label);text.htmlFor=id;
    const select=ioElement("select");select.id=id;row.append(text,select);
    parent.appendChild(row);return select;
  };
  const ioPopulate=(select,items,current)=>{
    const key=JSON.stringify(items);
    if(select.dataset.options!==key){
      select.dataset.options=key;
      select.replaceChildren(...items.map(([value,label])=>new Option(label,value)));
    }
    if(items.some(x=>x[0]===current))select.value=current;
    else select.value=items[0]?.[0]??"";
  };
  const actualIo=()=>ioInventory?.active||{};
  const setIoDraftFromActual=()=>{
    const a=actualIo(),types=ioInventory?.types||[];
    const name=a.backend||types.find(t=>/ASIO/i.test(t.name))?.name||
      types[0]?.name||"";
    const selected=types.find(t=>t.name===name);
    const capture=a.open&&a.backend===name?a.captureDevice:
      selected?.inputs?.[0]||selected?.outputs?.[0]||"";
    const playback=a.open&&a.backend===name?a.playbackDevice:
      selected?.outputs?.[0]||"";
    ioDraft={backend:name,captureDevice:capture,playbackDevice:playback,
      sampleRate:a.open&&a.backend===name?Number(a.rate)||0:0,
      bufferSize:a.open&&a.backend===name?Number(a.buffer)||0:0};
  };
  const ioSameDevice=()=>{
    const a=actualIo(),d=ioDraft;
    return !!(a?.open&&d&&a.backend===d.backend&&
      a.captureDevice===d.captureDevice&&a.playbackDevice===d.playbackDevice);
  };
  const ioVirtualOutput=name=>/virtual|vb-audio|cable input|loopback|obs/i.test(name||"");
  function renderNativeIo(){
    if(!ioRoot)return;
    const a=actualIo(),d=ioDraft||{},types=ioInventory?.types||[],type=ioType();
    const backendSelect=ioRoot.querySelector("#arNativeBackend");
    const capture=ioRoot.querySelector("#arNativeCapture");
    const playback=ioRoot.querySelector("#arNativePlayback");
    const rate=ioRoot.querySelector("#arNativeRate");
    const buffer=ioRoot.querySelector("#arNativeBuffer");
    ioPopulate(backendSelect,types.map(t=>[t.name,t.name]),d.backend);
    const inputs=type?.inputs||[],outputs=type?.outputs||[];
    ioPopulate(capture,inputs.map(x=>[x,x]),d.captureDevice);
    ioPopulate(playback,outputs.map(x=>[x,x]),d.playbackDevice);
    capture.disabled=!!ioBusy||!type?.separate;
    if(!type?.separate && outputs.length){
      ioDraft.captureDevice=ioDraft.playbackDevice;
      ioPopulate(capture,[[ioDraft.captureDevice,ioDraft.captureDevice]],ioDraft.captureDevice);
    }
    const same=ioSameDevice();
    ioPopulate(rate,[["0","Auto / driver default"],
      ...(same?(a.rates||[]).map(x=>[String(x),Number(x).toLocaleString()+" Hz"]):[])],
      String(same?d.sampleRate:0));
    ioPopulate(buffer,[["0","Auto / driver default"],
      ...(same?(a.buffers||[]).map(x=>[String(x),x+" samples"]):[])],
      String(same?d.bufferSize:0));
    for(const select of [backendSelect,playback,rate,buffer])
      select.disabled=!!ioBusy||!select.options.length;
    const status=ioRoot.querySelector("#arNativeIoActual");
    status.textContent=a.open?
      "ACTIVE · "+a.backend+" · "+a.captureDevice+" → "+a.playbackDevice+
      " · "+Math.round(Number(a.rate)||0)+" Hz / "+(a.buffer||0)+" samples"+
      (a.outputMuted?" · MAIN muted":" · MAIN monitoring"):
      "NO VERIFIED AUDIO DEVICE · choose driver, input and output";
    const virtual=ioRoot.querySelector("#arNativeIoVirtual");
    virtual.textContent=ioVirtualOutput(d.playbackDevice)?
      "Virtual playback endpoint: this may feed another app, not your headphones. Choose a physical speaker/headphone output to listen directly.":"";
    const note=ioRoot.querySelector("#arNativeIoNote");
    note.textContent=ioMessage||"Apply hardware once. Then select IN on any mixer channel.";
    ioRoot.querySelector("#arNativeIoApply").disabled=ioBusy||
      !d.backend||!d.captureDevice||!d.playbackDevice||!a||
      !ioInventory?.types?.length;
    ioRoot.querySelector("#arNativeIoApply").textContent=ioBusy?"Verifying…":"Apply Device";
  }
  function installNativeIoPanel(){
    if(ioRoot)return;
    ioRoot=ioElement("div","ar-native-io");ioRoot.id="arNativeAudioIo";
    ioRoot.hidden=true;ioRoot.setAttribute("role","dialog");
    ioRoot.setAttribute("aria-label","Audio hardware setup");
    ioRoot.setAttribute("aria-modal","true");
    const panel=ioElement("section","ar-native-io-panel");
    const head=ioElement("header","ar-native-io-header");
    head.append(ioElement("strong","","Audio I/O · Driver & Monitoring"));
    const close=ioElement("button","ar-native-io-close","×");
    close.setAttribute("aria-label","Close audio settings");
    close.addEventListener("click",()=>{ioRoot.hidden=true;});
    head.append(close);panel.append(head);
    const body=ioElement("div","ar-native-io-body");
    const readback=ioElement("div","ar-native-io-readback");readback.id="arNativeIoActual";
    body.append(readback);
    const backendSelect=ioField(body,"arNativeBackend","Audio driver");
    const capture=ioField(body,"arNativeCapture","Capture / input");
    const playback=ioField(body,"arNativePlayback","Playback / output");
    backendSelect.addEventListener("change",()=>{
      const type=(ioInventory?.types||[]).find(t=>t.name===backendSelect.value);
      ioDraft={backend:backendSelect.value,
        captureDevice:type?.separate?(type.inputs?.[0]||""):(type?.outputs?.[0]||""),
        playbackDevice:type?.outputs?.[0]||"",sampleRate:0,bufferSize:0};
      ioMessage="Device selection is a draft until Apply Device succeeds.";
      renderNativeIo();
    });
    capture.addEventListener("change",()=>{ioDraft.captureDevice=capture.value;renderNativeIo();});
    playback.addEventListener("change",()=>{
      ioDraft.playbackDevice=playback.value;
      if(!ioType()?.separate)ioDraft.captureDevice=playback.value;
      renderNativeIo();
    });
    const warning=ioElement("div","ar-native-io-notice");warning.id="arNativeIoVirtual";
    body.append(warning);
    const advanced=ioElement("details","");
    const summary=ioElement("summary","","Advanced · sample rate & buffer");
    advanced.append(summary);
    const fields=ioElement("div","ar-native-io-body");
    const rate=ioField(fields,"arNativeRate","Sample rate");
    const buffer=ioField(fields,"arNativeBuffer","Buffer");
    rate.addEventListener("change",()=>{ioDraft.sampleRate=Number(rate.value);});
    buffer.addEventListener("change",()=>{ioDraft.bufferSize=Number(buffer.value);});
    advanced.append(fields);body.append(advanced);
    const actions=ioElement("div","ar-native-io-controls");
    const apply=ioElement("button","primary","Apply Device");apply.id="arNativeIoApply";
    apply.addEventListener("click",applyNativeAudioIO);
    const recover=ioElement("button","","Driver Recovery…");
    recover.title="Advanced JUCE selector — only for driver troubleshooting";
    recover.addEventListener("click",()=>{
      ioRoot.hidden=true;
      backend.emitEvent("arMixAction",{action:"audioSetup"});
    });
    actions.append(apply,recover);body.append(actions);
    const note=ioElement("div","ar-native-io-busy");note.id="arNativeIoNote";
    body.append(note);panel.append(body);ioRoot.append(panel);
    ioRoot.addEventListener("click",e=>{if(e.target===ioRoot)ioRoot.hidden=true;});
    document.body.appendChild(ioRoot);
    const bar=document.querySelector(".command-bar");
    const before=document.getElementById("pluginsButton");
    if(bar&&before){
      const b=ioElement("button","tool ar-native-io-open","Audio I/O");
      b.id="arNativeIoOpen";b.addEventListener("click",openNativeAudioIO);
      before.insertAdjacentElement("afterend",b);
      ioMonitorButton=ioElement("button","tool ar-native-monitor","Monitor off");
      ioMonitorButton.id="arNativeMonitor";
      ioMonitorButton.addEventListener("click",()=>{
        const armed=hardware?.device?.outputArmed===true;
        if(!armed&&!verifiedM4()){
          status("Choose a verified input on any mixer channel first.");return;
        }
        backend.emitEvent("arMixAction",{action:"monitor",value:!armed,
          ...(armed?{}:{
            expectedEpoch:Number(lastDryReadback.epoch),
            expectedRevision:Number(lastDryReadback.revision)
          })});
      });
      b.insertAdjacentElement("afterend",ioMonitorButton);
    }
  }
  function openNativeAudioIO(){
    installNativeIoPanel();
    ioRoot.hidden=false;
    ioMessage="Reading verified driver inventory…";
    renderNativeIo();
    backend.emitEvent("arMixAction",{action:"audioDeviceInventory"});
  }
  function applyNativeAudioIO(){
    if(ioBusy||!ioDraft||!ioInventory)return;
    const d=ioDraft;
    if(!d.backend||!d.captureDevice||!d.playbackDevice){
      ioMessage="Choose a valid input and playback device.";renderNativeIo();return;
    }
    ioBusy=true;
    ioMessage="Applying driver selection · verify hardware ACK before using audio.";
    if(hardware?.device?.outputArmed){
      ioMutePending=true;
      backend.emitEvent("arMixAction",{action:"monitor",value:false});
    }else submitDeviceApply();
    renderNativeIo();
  }
  function submitDeviceApply(){
    const d=ioDraft,a=actualIo(),same=ioSameDevice();
    ioRequest="io-"+(++ioCounter);
    backend.emitEvent("arMixAction",{action:"audioDeviceApply",
      requestId:ioRequest,expectedDeviceGeneration:Number(a.deviceGeneration)||0,
      config:{backend:d.backend,captureDevice:d.captureDevice,
        playbackDevice:d.playbackDevice,
        sampleRate:same?Number(d.sampleRate)||0:0,
        bufferSize:same?Number(d.bufferSize)||0:0,
        enabledInputs:same?a.enabledInputs:null,
        enabledOutputs:same?a.enabledOutputs:null}});
  }
  // Same operator contract as Adobe/FL: every processing strip selects
  // its own VERIFIED hardware input; output initially targets MAIN. No
  // forced CH1, no draft graph, no separate Start or Arm button.
  const inputAt=track=>{
    const index=scope?.tracks?.indexOf(track)??-1;
    return index>=0&&index<10?index:-1;
  };
  const routeRow=index=>(lastDryReadback?.acceptedRoute?.channels||
    lastDryReadback?.preparedM4Route?.channels||[])[index];
  const routeValue=r=>r?.mode==="MONO"?"MONO:"+r.left:
    r?.mode==="STEREO"?"STEREO:"+r.left+":"+r.right:"None";
  const hardwareInputs=()=>{
    const d=hardware?.device;
    if(!deviceReady()||!Array.isArray(d?.inputEndpoints))return [];
    const enabled=new Set(d.enabledInputs);
    return d.inputEndpoints.filter(p=>p?.active===true &&
      Number.isInteger(p.port)&&enabled.has(p.port) && p.port>=0&&p.port<64);
  };
  let nativeSourcePending=null,nativeSourceSerial=1500000;
  const signalSourceResult=message=>{status(message);};
  const stopSource=error=>{
    nativeSourcePending=null;
    signalSourceResult("INPUT NOT CHANGED · "+error+
      ". Review hardware I/O; previous audio route preserved.");
  };
  const submitSource=()=>{
    const p=nativeSourcePending,d=lastDryReadback||{};
    if(!p||!deviceReady()||!d.ready||d.topologyFault||
       String(d.epoch)!==String(p.epoch)||Number(d.revision)!==p.revision){
      stopSource("device or route became stale");return;
    }
    p.phase="source";
    backend.emitEvent("arMixAction",{
      action:"routingSourceSwitch",requestId:p.id,channel:p.channel,
      mode:p.mode,left:p.left,right:p.right,
      deviceEpoch:p.epoch,revision:p.revision
    });
    signalSourceResult("CH "+(p.channel+1)+" · connecting verified hardware source…");
  };
  const monitorAfterSource=()=>{
    const p=nativeSourcePending,d=p?.accepted;
    if(!p||!d?.ready||!d.hasAcceptedRoute||d.topologyFault||
       String(d.epoch)!==String(p.epoch)){
      stopSource("output cannot be monitored until route is verified");return;
    }
    p.phase="monitor";p.revision=Number(d.revision);
    backend.emitEvent("arMixAction",{action:"monitor",value:true,
      expectedEpoch:p.epoch,expectedRevision:p.revision});
    signalSourceResult("CH "+(p.channel+1)+" · enabling MAIN monitoring…");
  };
  const chooseNativePhysicalInput=(track,value)=>{
    const channel=inputAt(track),d=lastDryReadback||{};
    if(channel<0)return false;
    if(value==="__IO__"){openNativeAudioIO();return true;}
    if(nativeSourcePending) {signalSourceResult("Previous input is still being verified.");return true;}
    if(!deviceReady()||!d.ready||d.topologyFault||!d.active||
       String(d.epoch)!==hardware.device.epoch){
      signalSourceResult("Choose Audio I/O and a valid input/output device first.");
      return true;
    }
    const mode=value==="None"?"NONE":value.startsWith("MONO:")?"MONO":
      value.startsWith("STEREO:")?"STEREO":"INVALID";
    if(mode==="INVALID"){signalSourceResult("Input unavailable on this hardware.");return true;}
    const pins=hardwareInputs(),set=new Set(pins.map(p=>p.port));
    const parts=value.split(":"),left=mode==="NONE"?-1:Number(parts[1]),
      right=mode==="STEREO"?Number(parts[2]):-1;
    if(mode==="MONO"&&(!set.has(left)||parts.length!==2) ||
       mode==="STEREO"&&(!set.has(left)||!set.has(right)||left===right||parts.length!==3)){
      signalSourceResult("This input is not enabled by the selected audio driver.");return true;
    }
    const before=routeRow(channel);
    const unchanged=routeValue(before)===value;
    if(unchanged&&hardware.device.outputArmed || unchanged&&mode==="NONE")
      return true;
    const epoch=Number(d.epoch),revision=Number(d.revision);
    if(!Number.isSafeInteger(epoch)||epoch<=0||!Number.isSafeInteger(revision)||revision<1){
      signalSourceResult("Native route revision is not ready.");return true;
    }
    nativeSourcePending={id:++nativeSourceSerial,channel,mode,left,right,
      epoch,revision,phase:"source",wasMonitoring:hardware.device.outputArmed===true};
    // Native M4 applies SAME-FORMAT source changes in 64-sample crossfade.
    // Structural changes while monitored mute on the existing control thread
    // before preparing the immutable graph, then resume only after real ACK.
    if(hardware.device.outputArmed && !unchanged&&before?.mode!==mode){
      nativeSourcePending.phase="mute";
      backend.emitEvent("arMixAction",{action:"monitor",value:false,
        expectedEpoch:epoch,expectedRevision:revision});
    }else if(unchanged){
      nativeSourcePending.accepted=d;
      monitorAfterSource();
    }else submitSource();
    return true;
  };
  function nativeChannelOptions(track,type){
    if(type==="input"){
      if(inputAt(track)<0)return [["None","No input"]];
      const pins=hardwareInputs(),list=[["None","No input"]];
      for(const pin of pins)
        list.push(["MONO:"+pin.port,"Mono · IN "+(pin.port+1)+" · "+(pin.label||"Input")]);
      for(let i=0;i+1<pins.length;++i)
        list.push(["STEREO:"+pins[i].port+":"+pins[i+1].port,
          "Stereo · IN "+(pins[i].port+1)+" / "+(pins[i+1].port+1)]);
      if(!pins.length)list.push(["__IO__","Configure Audio I/O…"]);
      // Explicitly keep the selected accepted route visible even if the
      // device was hotplugged: do NOT claim it is available on new hardware.
      return list;
    }
    if(type==="output"){
      if(track===scope?.master){
        const name=hardware?.device?.playbackDevice||"Choose Audio I/O";
        return [["Default Output","MAIN → "+name],["__IO__","Change hardware output…"]];
      }
      return [["Master","MAIN"],["SOURCE ONLY (No Main)","No MAIN · advanced"]];
    }
    return [["None","No send"]];
  }
  // M4 is already the TRUE ten-channel realtime PCM owner before the
  // universal ChannelId ABI is fully admitted. Its proven native parameter
  // setter must not be hidden behind an unrelated G0 ChannelId gate.
  const m4Parameter=(track,parameter,value)=>{
    if(!verifiedM4()){
      status("Select IN on a verified channel before adjusting the live mix.");
      return false;
    }
    const channel=scope?.tracks?.indexOf(track)??-1;
    if(channel<0||channel>=10){
      status("No native ten-channel identity for this control.");
      return false;
    }
    const epoch=Number(lastDryReadback.epoch),revision=Number(lastDryReadback.revision);
    if(!Number.isSafeInteger(epoch)||epoch<=0||
       !Number.isSafeInteger(revision)||revision<=0)return false;
    backend.emitEvent("arMixParameter",{
      parameter,channel:channel+1,value,routeEpoch:epoch,routeRevision:revision
    });
    return true; // request sent; not ACK or altered GUI model
  };
  const renderMeasuredM4=(live)=>{
    if(flags.acceptedGraphConnected||!scope?.tracks)return;
    const d=live?.dryRoutingLab||{};
    lastDryReadback=d;
    let changed=false;
    // Source identity must follow native accepted/prepared routes after ANY
    // hotplug, restart or external patch: never leave old demo input names.
    const rows=d.acceptedRoute?.channels||d.preparedM4Route?.channels||[];
    if(d.ready && Array.isArray(rows)&&rows.length>=10)for(let i=0;i<10;++i){
      const t=scope.tracks[i],r=rows[i];
      if(!t||!r)continue;
      const source=routeValue(r);
      const output=r.main?"Master":"SOURCE ONLY (No Main)";
      if(t.input!==source){t.input=source;changed=true;}
      if(t.output!==output){t.output=output;changed=true;}
    }
    if(!verifiedM4()){
      if(changed)scope.render?.();
      return;
    }
    const controls=d.channelControls,peaksL=d.channelPeaksL,peaksR=d.channelPeaksR;
    if(!Array.isArray(controls)||controls.length!==10)return;
    for(let i=0;i<10;++i){
      const t=scope.tracks[i],c=controls[i];
      if(!t||!c)continue;
      const f=Number(c.db),p=Number(c.pan);
      if(Number.isFinite(f)&&Math.abs(t.level-f)>0.02){t.level=f;changed=true;}
      if(Number.isFinite(p)&&Math.abs(t.pan-p)>0.02){t.pan=p;changed=true;}
      if(typeof c.mute==="boolean"&&t.mute!==c.mute){t.mute=c.mute;changed=true;}
      if(typeof c.solo==="boolean"&&t.solo!==c.solo){t.solo=c.solo;changed=true;}
    }
    const master=scope.master;
    const masterDb=Number(live?.masterDb);
    if(master&&Number.isFinite(masterDb)&&masterDb>=-90&&masterDb<=12&&
       Math.abs(master.level-masterDb)>0.02){master.level=masterDb;changed=true;}
    if(master&&typeof live?.masterMute==="boolean"&&
       master.mute!==live.masterMute){master.mute=live.masterMute;changed=true;}
    // Render only on ACCEPTED control state changes, never at 12Hz. The
    // WebView compositor is not a part of the realtime audio callback.
    if(changed)scope.render?.();
    const timestamp=performance.now();
    const paint=(track,left,right)=>{
      const root=document.querySelector('.strip[data-id="'+track?.id+'"]');
      if(!root)return;
      for(const [side,peak] of [["left",left],["right",right]]){
        const fill=root.querySelector('.meter-fill.'+side);
        if(!fill||!Number.isFinite(peak)||peak<0)continue;
        const db=peak===0?-90:20*Math.log10(peak);
        scope.displayMeterDbfs?.(fill,Math.max(-90,Math.min(6,db)),timestamp,
          "m4:"+track.id+":"+side);
      }
    };
    if(Array.isArray(peaksL)&&Array.isArray(peaksR)&&peaksL.length===10&&peaksR.length===10)
      for(let i=0;i<10;++i)paint(scope.tracks[i],Number(peaksL[i]),Number(peaksR[i]));
    paint(master,Number(d.peakL),Number(d.peakR));
  };
  function onNativeReadback(next) {
    const oldPortSignature=hardware?.device?
      JSON.stringify([hardware.device.epoch,hardware.device.inputEndpoints,hardware.device.playbackDevice]):"";
    updateHardwarePanel(next);
    const newPortSignature=next?.device?
      JSON.stringify([next.device.epoch,next.device.inputEndpoints,next.device.playbackDevice]):"";
    // Only re-render the full mixer when real hardware inventory changes,
    // never on each 12Hz meter or transport update.
    if(oldPortSignature!==newPortSignature && !nativeSourcePending)
      scope?.render?.();
    const caps=next?.graph?.capabilities;
    const nativeAdmitted=next.admitted && caps?.callbackOwnsChannelIds===true;
    if (!nativeAdmitted) {
      flags.productionCallbackLive=false;flags.acceptedGraphConnected=false;
      lastPaintedSignature="";
      stopDemo();
      status("NATIVE MIXER · pilih IN pada channel mana saja; level/pan/mute/solo menggunakan engine audio.");
      return;
    }
    // Control readback may change at the SAME structural graph revision.
    // Compare the actual accepted controls/edges, never skip native fader updates.
    const signature=next.identity+"|"+JSON.stringify(next.graph.channels.map(ch=>[
      ch.id,ch.controls,ch.inserts,ch.primaryOutId]))+"|"+JSON.stringify(next.graph.edges);
    if (signature===lastPaintedSignature) {
      // Telemetry often advances without a structural graph revision.
      paintNativeMeters(next);
      return;
    }
    if (!projection(next)) {
      flags.acceptedGraphConnected=false;flags.productionCallbackLive=false;
      stopDemo();status("NATIVE READBACK INVALID · existing GUI controls unchanged");
      return;
    }
    flags.productionCallbackLive=next.productLive;flags.acceptedGraphConnected=true;
    lastPaintedSignature=signature;stopDemo();paintNativeMeters(next);
    status((next.productLive?"NATIVE PRODUCT LIVE":"NATIVE PCM GRAPH ACCEPTED · FAT PENDING")+
      " · device "+next.device.epoch+
      " · graph "+next.graph.revision+" · roster "+next.graph.rosterRevision);
  }
  if (connected) {
    controller = protocol.createController({
      emit:(name,payload)=>backend.emitEvent(name,payload),
      onAccepted:onNativeReadback,
      onStatus:state=>{
        if(state.lastError==="PENDING_NATIVE_ACK"||state.lastError==="PENDING_ACCEPTED_SNAPSHOT")
          status("NATIVE "+state.lastError+" · awaiting authoritative readback");
        else if(state.lastError!=="ACCEPTED_READBACK"&&state.lastError!=="NONE"&&
                state.lastError!=="NATIVE_NOT_SUBSCRIBED") {
          lastNativeError=state.lastError;
          // Native REJECTED / STALE / timeout must roll back a temporary fader
          // cap and every input value; no optimistic GUI state survives.
          if (!state.pending && flags.acceptedGraphConnected && scope?.render) {
            scope.render();
            if(state.current)paintNativeMeters(state.current);
          }
        }
      }
    });
    backend.addEventListener("arMixAudioDeviceInventory",data=>{
      if(!data||!Array.isArray(data.types))return;
      ioInventory=data;
      if(!ioDraft||!ioBusy)setIoDraftFromActual();
      if(ioRoot)renderNativeIo();
    });
    backend.addEventListener("arMixAudioDeviceApplyAck",ack=>{
      if(!ioBusy||ack?.requestId!==ioRequest)return;
      ioBusy=false;ioMutePending=false;ioRequest="";
      if(ioInventory&&ack?.active)ioInventory.active=ack.active;
      if(ack?.ok===true){
        setIoDraftFromActual();
        ioMessage="Device verified. Select IN on any channel; monitoring is handled automatically.";
        controller?.subscribe();
      }else ioMessage="Device rejected · "+String(ack?.error||"Check audio driver and retry");
      renderNativeIo();
    });
    backend.addEventListener("arMixAudioArmAck",ack=>{
      if(!ioMutePending)return;
      ioMutePending=false;
      if(ack?.ok===true&&ack.armed===false)submitDeviceApply();
      else {ioBusy=false;ioMessage="Cannot safely switch device while output is active.";renderNativeIo();}
    });
    backend.addEventListener("arMixConsoleSnapshot", snapshot=>controller.onSnapshot(snapshot));
    backend.addEventListener("arMixConsoleAck", ack=>controller.onAck(ack));
    backend.addEventListener("arMixStatus",live=>renderMeasuredM4(live));
    backend.addEventListener("arMixM4ControlAck",ack=>{
      if(ack?.ok!==true)
        status("Control rejected: hardware/route changed; original mix retained.");
    });
    backend.addEventListener("arMixSourceSwitchAck",ack=>{
      const p=nativeSourcePending;
      if(!p||p.phase!=="source"||Number(ack?.requestId)!==p.id||
         Number(ack?.channel)!==p.channel)return;
      const next=ack?.state||{},row=next.acceptedRoute?.channels?.[p.channel];
      if(ack.ok!==true||!next.ready||next.topologyFault||
         String(next.epoch)!==String(p.epoch)||
         routeValue(row)!==(p.mode==="NONE"?"None":
           p.mode+":"+p.left+(p.mode==="STEREO"?":"+p.right:""))){
        stopSource(String(ack?.error||"Native patch ACK/readback mismatch"));return;
      }
      lastDryReadback=next;
      p.accepted=next;
      if(scope?.tracks?.[p.channel]){
        scope.tracks[p.channel].input=routeValue(row);
        scope.render?.();
      }
      const hasInput=(next.acceptedRoute?.channels||[])
        .some(r=>r.mode==="MONO"||r.mode==="STEREO");
      if(!hasInput && p.mode==="NONE"){
        nativeSourcePending=null;
        signalSourceResult("No input selected · MAIN remains safe.");
      }else if(p.wasMonitoring&&hardware?.device?.outputArmed){
        nativeSourcePending=null;
        signalSourceResult("CH "+(p.channel+1)+" · live source switched.");
      }else monitorAfterSource();
    });
    backend.addEventListener("arMixAudioArmAck",ack=>{
      const p=nativeSourcePending;
      if(!p||!(p.phase==="mute"||p.phase==="monitor"))return;
      const valid=ack?.ok===true&&Number(ack.deviceEpoch)===p.epoch&&
        (p.phase==="mute"?ack.armed===false:ack.armed===true);
      if(!valid){stopSource(String(ack?.error||"Monitor gate rejected"));return;}
      if(hardware?.device)hardware.device.outputArmed=!!ack.armed;
      if(p.phase==="mute")submitSource();
      else {
        nativeSourcePending=null;
        signalSourceResult("CH "+(p.channel+1)+
          " → MAIN · monitoring active. Verify audible output on the selected device.");
      }
    });
  }
  const nativeHooks=Object.freeze({
    enabled:connected,
    channelOptions:nativeChannelOptions,
    consumeField(event) {
      if (!connected)return false;
      const el=event.target,field=el?.dataset?.field;
      if (!field)return false;
      const t=scope?.findTrack?.(el) || null;
      if (!t)return true;
      if (!flags.acceptedGraphConnected && field==="input") {
        chooseNativePhysicalInput(t,el.value);
        return true;
      }
      if (!flags.acceptedGraphConnected && field==="output") {
        if(t===scope?.master || el.value==="__IO__")openNativeAudioIO();
        else status("Internal output routing requires a verified native MAIN path.");
        return true;
      }
      if (!flags.acceptedGraphConnected) {
        // Drag may move only the physical fader thumb. On release, send one
        // real native M4 parameter with exact epoch/revision; the next real
        // engine readback owns the displayed value.
        const numeric=Number(el.value);
        const pan=el.dataset.panKeyboard==="true"||el.dataset.panAltPointer==="true"||
          Math.abs(numeric)>12?numeric:0;
        if(event.type==="change"&&t===scope.master){
          if(field==="level"&&Number.isFinite(numeric))
            backend.emitEvent("arMixParameter",{parameter:"masterGain",value:numeric,channel:-1});
        }else if(event.type==="change"&&field==="level"&&Number.isFinite(numeric))
          m4Parameter(t,"faderDb",numeric);
        else if(event.type==="change"&&field==="pan"&&Number.isFinite(pan))
          m4Parameter(t,"pan",pan);
        if(!verifiedM4()&&t!==scope.master){
          if(field==="level")el.value=String(t.level);
          else if(field==="pan")el.value=String(t.pan);
        }
        return true;
      }
      // During a native pointer drag move the approved fader *cap* only.
      // The authoritative track/PCM state stays unchanged until ACK+readback.
      if (event.type!=="change") {
        if (field==="level") {
          const n=Number(el.value),min=Number(el.min),max=Number(el.max);
          if ([n,min,max].every(Number.isFinite)&&max>min) {
            const p=Math.max(0,Math.min(100,100*(n-min)/(max-min)));
            const box=el.closest?.(".fader-box");
            box?.style?.setProperty("--fader-fill",p+"%");
            box?.style?.setProperty("--fader-lit",String(p/100));
            box?.style?.setProperty("--fader-y",(100-p)+"%");
            box?.style?.setProperty("--fader-frac",String(p/100));
          }
        }
        return true;
      }
      const numeric=Number(el.value);
      if (field==="level"&&Number.isFinite(numeric))
        intent("SET_FADER",t,{valueDb:numeric});
      else if (field==="pan"&&Number.isFinite(numeric)) {
        // Preserve hardware center detent without publishing demo PAN state.
        const fine=el.dataset.panKeyboard==="true";
        const bypass=el.dataset.panAltPointer==="true";
        const value=!fine&&!bypass&&Math.abs(numeric)<=12?0:numeric;
        intent("SET_PAN",t,{value:value/100});
      } else {
        status("NATIVE: "+field+" awaits native capability; reference graph unchanged");
        scope?.render?.(); // restore SELECT/route control from accepted graph
      }
      return true;
    },
    consumeAction(t,action,btn) {
      if (!connected)return false;
      // Selection, WIRE inspection and section presentation are UI-only.
      if (["wireExpand","wireSource","wireRoute","editIdentity","rename"].includes(action))
        return !["wireExpand","wireSource","wireRoute"].includes(action);
      if (action==="mute"||action==="solo") {
        if(!flags.acceptedGraphConnected) {
          if(t===scope?.master&&action==="mute")
            backend.emitEvent("arMixParameter",{parameter:"masterMute",value:!t.mute,channel:-1});
          else m4Parameter(t,action,!t[action]);
          return true;
        }
        const key=action==="mute"?"SET_MUTE":"SET_SOLO";
        intent(key,t,{value:!t[action],enabled:!t[action]});return true;
      }
      status("NATIVE: "+action+" is not an accepted ChannelId operation");return true;
    },
    consumeWire(ops) {
      if (!connected)return false;
      const current=controller?.state().current;
      if (!flags.acceptedGraphConnected || !current?.admitted ||
          !current.graph.capabilities.supportedOps.includes("APPLY_WIRE_TRANSACTION")) {
        status("NATIVE: WIRE transaction not admitted; graph unchanged");return true;
      }
      const changes=[];
      for (const op of ops) {
        const sourceId=nativeByUiId.get(op.sourceId);
        const destId=op.destId===undefined?undefined:nativeByUiId.get(op.destId);
        const edgeId=op.edgeId===undefined?undefined:wireByUiId.get(op.edgeId);
        if (!sourceId || (op.destId!==undefined&&!destId) ||
            (op.edgeId!==undefined&&!edgeId)) {
          status("NATIVE: WIRE mapped identity missing; no mutation");return true;
        }
        // ENGINE #266's real PCM decoder consumes op/from/to/edgeId,
        // NOT the Pages reducer's type/sourceId/destId. Never feed it
        // an unrecognized demo command or an unbound EdgeId.
        if (!["CONNECT","REMOVE","RETARGET","ROUTE_ONLY"].includes(op.type) ||
            (op.mode && !["send","postFader","routeOnly"].includes(op.mode))) {
          status("NATIVE: WIRE "+op.type+" needs real engine capability; unchanged");
          return true;
        }
        const change={op:op.type,from:sourceId};
        if (destId)change.to=destId;
        if (edgeId)change.edgeId=edgeId;
        if (op.type==="CONNECT" || op.type==="ROUTE_ONLY") {
          change.tap="POST_FADER";
          change.gainLinear=op.levelDb===undefined?1:
            Math.pow(10,Math.max(-60,Math.min(6,Number(op.levelDb)))/20);
        }
        changes.push(change);
      }
      const source=changes[0]?.from;
      if (!source || changes.some(change=>change.from!==source)) {
        status("NATIVE: multi-source transaction requires native batch admission");
        return true;
      }
      const accepted=controller.intent("APPLY_WIRE_TRANSACTION",source,{changes});
      if (!accepted.ok)status("NATIVE: "+accepted.error+"; WIRE unchanged");
      else window.setTimeout(()=>controller?.expire(accepted.requestId),8000);
      return true;
    },
    consumeUndo() {if (!connected)return false;status("NATIVE: WIRE undo requires accepted native transaction");return true;},
    consumeDoubleClick(event,t) {
      if (!connected)return false;
      if (event.target.classList.contains("fader")) {
        if(flags.acceptedGraphConnected)intent("SET_FADER",t,{valueDb:0});
        else if(t===scope?.master)
          backend.emitEvent("arMixParameter",{parameter:"masterGain",value:0,channel:-1});
        else m4Parameter(t,"faderDb",0);
      } else if(event.target.classList.contains("center-pan")) {
        if(flags.acceptedGraphConnected)intent("SET_PAN",t,{value:0});
        else m4Parameter(t,"pan",0);
      }
      else status("NATIVE: identity editor unavailable until native ACK contract");
      return true;
    },
    state:()=>controller?.state() || null
  });
  window.__ARMIX_NATIVE_M2__ = nativeHooks;
  const descriptor = Object.freeze({
    source:"site/preview-v2 + site/preview-buttons",
    pagesRoute:"/preview/",
    nativeTransport:connected,
    get acceptedGraphConnected(){return flags.acceptedGraphConnected;},
    get productionCallbackLive(){return flags.productionCallbackLive;},
    get status(){return lastNativeError||fallback;},
    m2:nativeHooks
  });
  Object.defineProperty(window,"__ARMIX_APPROVED_NATIVE_GUI__",{
    value:descriptor,configurable:false,writable:false
  });
  if (!connected)return;
  // G0 with a VERIFIED M4 native route is a real mixer: never block its
  // fader just because universal ChannelId migration is not yet admitted.
  document.addEventListener("pointerdown",event=>{
    if(flags.acceptedGraphConnected||verifiedM4())return;
    if(!event.target.closest?.(".fader-box,.center-pan"))return;
    event.preventDefault();event.stopImmediatePropagation();
    status("Choose a verified hardware IN before adjusting the mixer.");
  },true);
  // Reference-only buttons outside strips must not mutate fake Show/FX state.
  document.addEventListener("click",event=>{
    const target=event.target.closest?.(
      "#preview,#addTrack,#addBus,#addFx,#reset,#saveScene,#recallScene,#sceneSafe,#group,#ungroup,#copy,#paste,#clearCue,#pluginsButton,#pluginScan,#pluginImport,#pluginAddPath");
    if(!target)return;
    event.preventDefault();event.stopImmediatePropagation();
    status("NATIVE: "+target.id+" requires real engine admission and readback");
  },true);
  // Native fader is committed exactly once INSIDE the original GUI's
  // finishFaderPointer via build_approved_native_gui.py hook. A later
  // window.pointerup listener would run after that handler clears the gesture.
  const ready=()=>{
    installNativeIoPanel();
    installFirstSoundMenu();
    updateHardwarePanel(null);
    stopDemo();
    status("NATIVE: subscribed · waiting for true device and accepted ChannelId graph");
    backend.emitEvent("arMixGuiReady",{
      gui:"APPROVED_PAGES_PREVIEW_BUTTONS",visualReady:true,audioReady:false
    });
    controller.subscribe();
  };
  if(document.readyState==="loading")
    document.addEventListener("DOMContentLoaded",ready,{once:true});
  else ready();
})();
