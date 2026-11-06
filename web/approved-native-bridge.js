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
  // B-stage: attached-node-only meter caches; a structural render invalidates
  // them atomically. Never retain detached WebView2 strips or stale channel IDs.
  const meterElements = new Map();
  let controller = null;
  const status = message => {
    lastNativeError = message;
    const info = document.getElementById("info");
    if (info && info.textContent !== message) info.textContent = message;
  };
  const stopDemo = () => {
    // The public Pages demo remains unchanged. In native, DEMO meters and
    // reference-only physical operations are never passed off as live audio.
    const demo = document.getElementById("preview");
    if (demo) {
      if (!demo.disabled) demo.disabled = true;
      const title = "Demo meters disabled in native Windows application";
      if (demo.title !== title) demo.title = title;
    }
    scope?.stopDemo?.();
    const authority = flags.productionCallbackLive ? "product-live" :
      flags.acceptedGraphConnected ? "pcm-accepted" : "not-admitted";
    if (document.documentElement.dataset.armixNativeAuthority !== authority)
      document.documentElement.dataset.armixNativeAuthority = authority;
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
    meterElements.clear(); // installGraph() replaces strip HTML and meter nodes
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
      let elements=meterElements.get(ui);
      if (!elements?.root?.isConnected ||
          elements.root.dataset.id!==String(ui)) {
        const root=document.querySelector('.strip[data-id="'+ui+'"]');
        if (!root) {meterElements.delete(ui);continue;}
        elements={root,
          left:root.querySelector('.meter-fill.left'),
          right:root.querySelector('.meter-fill.right')};
        meterElements.set(ui,elements);
      }
      const entry=data[ch.id];
      const one=typeof entry==="number"&&Number.isFinite(entry)&&entry>=0?entry:null;
      const l=typeof entry?.peakL==="number"&&Number.isFinite(entry.peakL)&&entry.peakL>=0?
        entry.peakL:one;
      const right=typeof entry?.peakR==="number"&&Number.isFinite(entry.peakR)&&entry.peakR>=0?
        entry.peakR:ch.processingFormat==="MONO"?l:null;
      const paint=(side,peak)=>{
        const fill=elements[side];
        if(!fill)return;
        const db=peak===null || peak===0?-90:20*Math.log10(peak);
        scope.displayMeterDbfs(fill,Math.max(-90,Math.min(6,db)),ts,ui+':'+(side==="left"?"L":"R"));
      };
      paint("left",l);
      paint("right",right);
    }
  }

  // Native-only operator controls in the EXISTING approved More menu:
  // user selects REAL JUCE active pin -> explicitly patches CH1 -> arms output.
  // No auto-arm, demo PCM, guessed physical port or second GUI.
  let hardware=null, inputSelect=null, routeBtn=null, armBtn=null, muteBtn=null;
  let hardwareNote=null, pendingSourceId=0, sourceSerial=100;
  const safeNativeCounter=x=>{
    if(typeof x!=="string" || !/^[1-9]\d*$/.test(x))return null;
    const number=Number(x);
    return Number.isSafeInteger(number)&&number>0?number:null;
  };
  const legacy=()=>hardware?.raw?.legacyM4;
  const deviceReady=()=>!!(hardware?.device?.backend &&
    hardware.device.sampleRate>0 &&
    hardware.device.enabledInputs.length>0 &&
    hardware.device.enabledOutputs.length>=2 && legacy()?.active===true);
  function updateHardwarePanel(next) {
    hardware=next || null;
    const d=next?.device;
    const deviceOpen=Boolean(d?.backend && d.sampleRate>0 &&
      d.enabledOutputs.length>=2);
    const label=document.getElementById("transport");
    const transport=!deviceOpen?"Device not ready":
      d.outputArmed?"Output armed":"Output muted";
    if(label && label.textContent!==transport)label.textContent=transport;
    const warning=document.querySelector?.(".status-bar .warning");
    const warningText=!deviceOpen?"NO VERIFIED AUDIO DEVICE":
      d.outputArmed?(next.admitted?"NATIVE PCM · HARDWARE FAT PENDING":
        "MAIN ARMED · PCM NOT YET ADMITTED"):
      legacy()?.routeAccepted?"INPUT PATCHED · OUTPUT MUTED":
      "NO AUDIBLE OUTPUT · PATCH INPUT + ARM";
    if(warning && warning.textContent!==warningText)warning.textContent=warningText;
    if(!inputSelect)return;
    const pins=deviceOpen?d.inputEndpoints.filter(p=>
      p.active===true && d.enabledInputs.includes(p.port)):[];
    const key=pins.map(p=>p.id+"|"+p.label).join(";");
    if(inputSelect.dataset.pins!==key) {
      const before=inputSelect.value;
      inputSelect.dataset.pins=key;
      inputSelect.replaceChildren();
      const option=document.createElement("option");
      option.value="";option.textContent="Physical input for CH1";
      inputSelect.appendChild(option);
      for(const pin of pins){
        const item=document.createElement("option");
        item.value=String(pin.port);
        item.textContent=pin.label+" · IN "+(pin.port+1);
        inputSelect.appendChild(item);
      }
      if(pins.some(p=>String(p.port)===before))inputSelect.value=before;
    }
    const ready=deviceReady();
    const selectDisabled=!ready;
    if(inputSelect.disabled!==selectDisabled)inputSelect.disabled=selectDisabled;
    const routeDisabled=!ready || inputSelect.value==="" || pendingSourceId!==0;
    if(routeBtn && routeBtn.disabled!==routeDisabled)routeBtn.disabled=routeDisabled;
    const armDisabled=!ready || legacy()?.routeAccepted!==true || d.outputArmed;
    if(armBtn && armBtn.disabled!==armDisabled)armBtn.disabled=armDisabled;
    const muteDisabled=!deviceOpen || !d.outputArmed;
    if(muteBtn && muteBtn.disabled!==muteDisabled)muteBtn.disabled=muteDisabled;
    if(hardwareNote) {
      const evidence=protocol.diagnoseFirstSound?.(next,inputSelect.value);
      // Human-readable PCM stages from the ORIGINAL native callback only.
      // These values are NOT evidence that the DAC or speaker is audible.
      const note=evidence?
        evidence.readout+" · "+evidence.nextAction:
        "No verified hardware PCM readback available.";
      if(hardwareNote.textContent!==note)hardwareNote.textContent=note;
    }
  }
  // The approved Windows GUI is built from /preview/ (NOT web/mixer-io).
  // A single compact device preference panel uses the existing JUCE
  // AudioDeviceManager inventory/APPLY ACK. All controls stay off the DSP RT.
  let ioInventory=null,ioDraft=null,ioPanel=null,ioRequest=null,ioAfterMute=null;
  const virtualOutput=name=>/cable input|virtual cable|vb.audio|virtual audio/i.test(String(name||""));
  const cableFeedbackPair=()=>{
    const d=hardware?.device;
    return /cable output/i.test(String(d?.captureDevice||"")) &&
           /cable input/i.test(String(d?.playbackDevice||"")) &&
           /vb.audio/i.test(String(d?.captureDevice||"")+" "+String(d?.playbackDevice||""));
  };
  const ioType=()=>ioInventory?.types?.find(t=>t.name===ioDraft?.backend);
  function paintNativeIo(){
    if(!ioPanel||ioPanel.hidden||!ioInventory)return;
    const active=ioInventory.active||{},types=ioInventory.types||[];
    if(!ioDraft)ioDraft={backend:active.backend||types[0]?.name||"",
      captureDevice:active.captureDevice||"",playbackDevice:active.playbackDevice||"",
      sampleRate:0,bufferSize:0};
    const setOptions=(id,values,current)=>{
      const sel=ioPanel.querySelector('[data-native-io="'+id+'"]');
      if(!sel)return;
      const previous=sel.value,signature=JSON.stringify(values);
      if(sel.dataset.options!==signature){
        sel.replaceChildren(...values.map(([value,label])=>new Option(label,value)));
        sel.dataset.options=signature;
      }
      if(current!==undefined)sel.value=current;
      else if(values.some(v=>v[0]===previous))sel.value=previous;
    };
    setOptions("backend",types.map(t=>[t.name,t.name]),ioDraft.backend);
    const type=ioType()||{};
    const outputs=type.outputs||[],inputs=type.separate?(type.inputs||[]):outputs;
    if(!outputs.includes(ioDraft.playbackDevice))ioDraft.playbackDevice=outputs[0]||"";
    if(!inputs.includes(ioDraft.captureDevice))ioDraft.captureDevice=
      type.separate?inputs[0]||"":ioDraft.playbackDevice;
    if(!type.separate)ioDraft.captureDevice=ioDraft.playbackDevice;
    setOptions("capture",inputs.map(n=>[n,n]),ioDraft.captureDevice);
    setOptions("playback",outputs.map(n=>[n,n]),ioDraft.playbackDevice);
    const same=active.open&&active.backend===ioDraft.backend&&
      active.captureDevice===ioDraft.captureDevice&&
      active.playbackDevice===ioDraft.playbackDevice;
    const rates=same?(active.rates||[]):[],buffers=same?(active.buffers||[]):[];
    setOptions("rate",[[0,"Auto"],...rates.map(n=>[n,Math.round(n)+" Hz"])],
      ioDraft.sampleRate||0);
    setOptions("buffer",[[0,"Auto"],...buffers.map(n=>[n,n+" samples"])],
      ioDraft.bufferSize||0);
    const selected=ioDraft.playbackDevice;
    const hint=ioPanel.querySelector('[data-native-io="hint"]');
    if(hint)hint.textContent=
      "Current: "+(active.open?active.backend+" · "+active.playbackDevice:"No audio device")+
      " · "+(active.rate||0)+" Hz / "+(active.buffer||0)+" samples. "+
      (virtualOutput(selected)?
        "Selected output appears virtual: this is NOT a physical headphone/speaker. Choose a real playback device to listen.":
        "Output: "+(selected||"not selected")+". Keep headphone volume low.");
    const apply=ioPanel.querySelector('[data-native-io="apply"]');
    if(apply)apply.disabled=!!ioRequest||!ioDraft.captureDevice||!ioDraft.playbackDevice;
  }
  function installNativeIo(){
    if(!connected||ioPanel||typeof document.createElement!=="function")return;
    const css=document.createElement("style");
    css.textContent=`
      .armix-native-io{position:fixed;inset:0;z-index:200000;background:#0009;
        display:grid;place-items:center;font:12px Inter,Arial,sans-serif;color:#d8eaf0}
      .armix-native-io[hidden]{display:none}
      .armix-native-io-panel{width:min(560px,calc(100vw - 32px));
        max-height:calc(100vh - 40px);overflow:auto;background:#142630;
        border:1px solid #45616d;border-radius:8px;box-shadow:0 20px 70px #0009;padding:18px}
      .armix-native-io-header{display:flex;align-items:center;justify-content:space-between;
        gap:14px;margin-bottom:13px;font-size:15px;font-weight:500}
      .armix-native-io-field{display:grid;grid-template-columns:105px minmax(0,1fr);
        align-items:center;gap:12px;margin:8px 0}
      .armix-native-io select{min-width:0;width:100%;box-sizing:border-box;
        background:#0c1c25;color:#d8eaf0;border:1px solid #4a6672;
        border-radius:4px;padding:8px;font:inherit}
      .armix-native-io button{background:#25414d;color:#e2f3f5;
        border:1px solid #4a6875;border-radius:4px;padding:7px 13px;cursor:pointer;font:inherit}
      .armix-native-io button:disabled{opacity:.45;cursor:default}
      .armix-native-io-hint{color:#a7c7d0;line-height:1.5;font-size:11px;margin:13px 0}
      .armix-native-io-footer{display:flex;justify-content:flex-end;gap:8px;margin-top:15px}
      .armix-native-io [data-native-io="apply"]{background:#245b61;border-color:#488f8e}
      .armix-native-io details{margin-top:12px}
    `;
    document.head.appendChild(css);
    ioPanel=document.createElement("div");ioPanel.className="armix-native-io";ioPanel.hidden=true;
    ioPanel.innerHTML=`<section class="armix-native-io-panel" role="dialog" aria-modal="true"
      aria-label="Audio hardware">
      <header class="armix-native-io-header"><span>Audio I/O · Hardware</span>
      <button type="button" data-native-io="close" aria-label="Close audio settings">×</button></header>
      <label class="armix-native-io-field">Driver <select data-native-io="backend"></select></label>
      <label class="armix-native-io-field">Input device <select data-native-io="capture"></select></label>
      <label class="armix-native-io-field">Output device <select data-native-io="playback"></select></label>
      <details><summary>Advanced · sample rate &amp; buffer</summary>
      <label class="armix-native-io-field">Sample rate <select data-native-io="rate"></select></label>
      <label class="armix-native-io-field">Buffer <select data-native-io="buffer"></select></label></details>
      <p class="armix-native-io-hint" data-native-io="hint"></p>
      <div class="armix-native-io-footer">
      <button type="button" data-native-io="cancel">Cancel</button>
      <button type="button" data-native-io="apply">Use device</button></div></section>`;
    document.body.appendChild(ioPanel);
    const open=()=>{
      ioPanel.hidden=false;ioDraft=null;ioInventory=null;
      ioPanel.querySelector('[data-native-io="hint"]').textContent="Reading actual installed drivers...";
      backend.emitEvent("arMixAction",{action:"audioDeviceInventory"});
    };
    const close=()=>{if(!ioRequest)ioPanel.hidden=true;};
    const bar=document.querySelector(".command-bar");
    const btn=document.createElement("button");btn.className="tool";
    btn.type="button";btn.textContent="Audio I/O";
    btn.title="Choose hardware driver, input and output once";
    bar?.insertBefore(btn,document.getElementById("moreTools"));
    btn.addEventListener("click",open);
    ioPanel.addEventListener("click",event=>{
      const action=event.target.closest?.("[data-native-io]")?.dataset.nativeIo;
      if(action==="close"||action==="cancel"){close();return;}
      if(action!=="apply"||ioRequest||!ioDraft)return;
      const active=ioInventory?.active;
      if(!active){return;}
      const same=active.open&&active.backend===ioDraft.backend&&
        active.captureDevice===ioDraft.captureDevice&&active.playbackDevice===ioDraft.playbackDevice;
      const config={...ioDraft,
        enabledInputs:same?active.enabledInputs:null,
        enabledOutputs:same?active.enabledOutputs:null};
      ioRequest={requestId:"daw-io-"+(++sourceSerial),config,
        expectedDeviceGeneration:Number(active.deviceGeneration)||0};
      ioPanel.querySelector('[data-native-io="hint"]').textContent="Applying driver (native readback required)...";
      if(hardware?.device?.outputArmed){
        ioAfterMute=ioRequest;
        backend.emitEvent("arMixAction",{action:"monitor",value:false});
      }else sendNativeIoApply();
    });
    ioPanel.addEventListener("change",event=>{
      const field=event.target.dataset.nativeIo;
      if(!ioDraft||!["backend","capture","playback","rate","buffer"].includes(field))return;
      const value=event.target.value;
      if(field==="backend"){
        ioDraft={backend:value,captureDevice:"",playbackDevice:"",sampleRate:0,bufferSize:0};
      }else if(field==="capture")ioDraft.captureDevice=value;
      else if(field==="playback")ioDraft.playbackDevice=value;
      else if(field==="rate")ioDraft.sampleRate=Number(value)||0;
      else if(field==="buffer")ioDraft.bufferSize=Number(value)||0;
      paintNativeIo();
    });
    ioPanel.addEventListener("click",event=>{if(event.target===ioPanel)close();});
    backend.addEventListener("arMixAudioDeviceInventory",data=>{
      ioInventory=data;
      if(!ioPanel.hidden)paintNativeIo();
    });
    backend.addEventListener("arMixAudioDeviceApplyAck",ack=>{
      if(!ioRequest||ack?.requestId!==ioRequest.requestId)return;
      ioRequest=null;ioAfterMute=null;
      if(ack?.ok===true&&ack?.active?.open){
        ioPanel.hidden=true;controller?.subscribe();
        status("AUDIO I/O READY · choose IN directly on any mixer channel");
      }else{
        ioPanel.querySelector('[data-native-io="hint"]').textContent=
          "Driver rejected: "+String(ack?.error||"device not ready");
        paintNativeIo();
      }
    });
    backend.addEventListener("arMixAudioArmAck",ack=>{
      if(!ioAfterMute)return;
      if(ack?.ok===true&&ack?.armed===false)sendNativeIoApply();
      else{
        ioAfterMute=null;ioRequest=null;
        ioPanel.querySelector('[data-native-io="hint"]').textContent=
          "Cannot change driver safely while output is active";
      }
    });
  }
  function sendNativeIoApply(){
    if(!ioRequest)return;
    backend.emitEvent("arMixAction",{
      action:"audioDeviceApply",...ioRequest});
  }
  function installFirstSoundMenu() {
    const menu=document.querySelector?.("#moreTools .more-tools-panel");
    if(!menu || typeof document.createElement!=="function")return;
    const item=(tag,cls,label)=>{
      const el=document.createElement(tag);
      el.className=cls;el.textContent=label;
      if(tag==="button")el.type="button";
      return el;
    };
    const caption=item("span","more-tools-caption","REAL AUDIO · FIRST SOUND");
    const setup=item("button","tool","Audio Setup");
    // Device preferences remain a one-time JUCE owner. The approved Pages
    // WebView does NOT load legacy mixer-io.js; never call its absent overlay.
    setup.addEventListener("click",()=>{
      backend.emitEvent("arMixAction",{action:"audioSetup"});
      status("Choose hardware driver/input/output once; then select IN on each strip.");
    });
    inputSelect=item("select","toolbar-select","");
    inputSelect.setAttribute("aria-label","JUCE physical input for CH1");
    inputSelect.addEventListener("change",()=>updateHardwarePanel(hardware));
    routeBtn=item("button","tool","Patch CH1");
    routeBtn.addEventListener("click",()=>{
      const d=hardware?.device,l=legacy();
      const epoch=safeNativeCounter(d?.epoch);
      const revision=safeNativeCounter(l?.routingRevision);
      const pin=Number(inputSelect?.value);
      if(!deviceReady()||inputSelect.value==="" ||
         epoch===null||revision===null||
         !Number.isInteger(pin)||!d.enabledInputs.includes(pin)){
        status("NATIVE: invalid input pin, epoch or route revision.");return;
      }
      pendingSourceId=++sourceSerial;
      routeBtn.disabled=true;
      backend.emitEvent("arMixAction",{
        action:"routingSourceSwitch",requestId:pendingSourceId,
        channel:0,mode:"MONO",left:pin,right:-1,
        deviceEpoch:epoch,revision
      });
      status("NATIVE: CH1 physical input patch pending real ACK.");
    });
    armBtn=item("button","tool","Arm output");
    armBtn.addEventListener("click",()=>{
      if(!deviceReady()||legacy()?.routeAccepted!==true)return;
      const epoch=safeNativeCounter(hardware.device.epoch);
      const revision=safeNativeCounter(legacy().routingRevision);
      if(epoch===null||revision===null)return;
      backend.emitEvent("arMixAction",{action:"monitor",value:true,
        expectedEpoch:epoch,expectedRevision:revision});
      status("NATIVE: output ARM requested; awaiting JUCE ACK.");
    });
    muteBtn=item("button","tool","Mute output");
    muteBtn.addEventListener("click",()=>{
      backend.emitEvent("arMixAction",{action:"monitor",value:false});
      status("NATIVE: output MUTE requested; awaiting JUCE ACK.");
    });
    hardwareNote=item("span","more-tools-caption","Device not initialized");
    hardwareNote.style.whiteSpace="normal";
    // Do not expose a second patch/ARM wizard as the normal mixer workflow.
    // Retain hardware FAT and panic diagnostics in a collapsed expert-only
    // section, while existing strip IN selects and routes audio in one action.
    const advanced=document.createElement("details");
    advanced.className="arNativeFirstSoundAdvanced";
    const summary=document.createElement("summary");
    summary.textContent="Advanced · hardware diagnostics";
    advanced.append(summary,caption,inputSelect,routeBtn,armBtn,muteBtn,hardwareNote);
    menu.append(setup,advanced);
    updateHardwarePanel(hardware);
  }

  // The installed M4 renderer already has ten real native strip setters.
  // Before ChannelId migration, show its ACCEPTED readback rather than
  // disabling fader/PAN/MUTE/SOLO or inventing an optimistic demo state.
  // User's one IN selection is the entire normal patch+monitor gesture.
  // A real ACK/epoch/accepted row must precede any guarded output resume.
  let pendingStripSource=null;
  function chooseNativeStripInput(select) {
    const n=m4StripIndex(select),d=hardware?.device,m=legacy();
    const epoch=safeNativeCounter(d?.epoch),revision=safeNativeCounter(m?.routingRevision);
    const parts=String(select.value).split(":");
    const mode=parts[0],left=Number(parts[1]),right=mode==="STEREO"?Number(parts[2]):-1;
    const active=new Set((d?.inputEndpoints||[]).filter(x=>x.active===true).map(x=>x.port));
    if(!deviceReady()||n<0||n>=10||pendingSourceId||
       epoch===null||revision===null||!["MONO","STEREO"].includes(mode)||
       !Number.isInteger(left)||!active.has(left)||
       (mode==="STEREO"&&(!Number.isInteger(right)||right===left||!active.has(right)))) {
      status("INPUT NOT CHANGED · select a verified active mono/stereo input");
      paintM4Inputs(hardware);return;
    }
    pendingSourceId=++sourceSerial;
    pendingStripSource={requestId:pendingSourceId,channel:n,mode,left,right,epoch};
    backend.emitEvent("arMixAction",{
      action:"routingSourceSwitch",requestId:pendingSourceId,
      channel:n,mode,left,right,deviceEpoch:epoch,revision
    });
    status("CH "+(n+1)+" · connecting input (native ACK)");
    paintM4Inputs(hardware);
  }
  function paintM4Inputs(next) {
    if(!Array.isArray(scope?.tracks))return;
    const d=next?.device,m=next?.raw?.legacyM4;
    const active=Array.isArray(d?.inputEndpoints)?d.inputEndpoints.filter(p=>
      p?.active===true && d.enabledInputs?.includes(p.port)):[];
    const options=[];
    // NONE is displayed as state, not offered as a nonfunctional patch.
    options.push(["NONE","No input"]);
    for(const pin of active)options.push(["MONO:"+pin.port,
      "Mono · "+pin.label]);
    for(let i=0;i<active.length;i++)for(let j=i+1;j<active.length;j++){
      const l=active[i],r=active[j];
      if(r.port===l.port+1)options.push([
        "STEREO:"+l.port+":"+r.port,
        "Stereo · "+l.label+" / "+r.label]);
    }
    const sources=Array.isArray(m?.channelSources)?m.channelSources:[];
    for(let i=0;i<Math.min(scope.tracks.length,10);i++){
      const t=scope.tracks[i];
      const root=document.querySelector('.strip[data-id="'+t.id+'"]');
      const select=root?.querySelector('select[data-field="input"]');
      if(!select)continue;
      const src=sources[i],value=src?.mode==="STEREO"?
        "STEREO:"+src.left+":"+src.right:src?.mode==="MONO"?
        "MONO:"+src.left:"NONE";
      const list=options.slice();
      if(value!=="NONE"&&!list.some(o=>o[0]===value))
        list.push([value,"Previous input · not active"]);
      const signature=JSON.stringify(list);
      if(select.dataset.nativeOptions!==signature){
        select.replaceChildren(...list.map(([v,label])=>new Option(label,v)));
        select.dataset.nativeOptions=signature;
      }
      if(select.value!==value)select.value=value;
      select.disabled=!deviceReady()||pendingSourceId!==0||!Array.isArray(m?.channelSources);
      select.title="CH "+(i+1)+" · verified hardware IN · native route ACK";
    }
  }
  function m4Verified(){
    const m=legacy(),d=hardware?.device;
    return m?.active===true&&m.routeAccepted===true &&
      Array.isArray(m.channelControls)&&m.channelControls.length===10 &&
      safeNativeCounter(d?.epoch)!==null &&
      safeNativeCounter(m.routingRevision)!==null;
  }
  function m4StripIndex(el){
    // /preview/ uses stable view-local data-id, NOT the retired data-strip.
    const root=el?.closest?.('.strip[data-id]');
    if(!root || !Array.isArray(scope?.tracks))return -1;
    const index=scope.tracks.findIndex(t=>String(t.id)===root.dataset.id);
    return index>=0&&index<10?index:-1;
  }
  function m4Send(parameter,channel,value){
    if(!m4Verified()||channel<0||channel>=10)return false;
    if(typeof value!=="boolean"&&!(Number.isFinite(value)))return false;
    backend.emitEvent("arMixParameter",{
      parameter,channel:channel+1,value,
      routeEpoch:safeNativeCounter(hardware.device.epoch),
      routeRevision:safeNativeCounter(legacy().routingRevision)
    });
    // 12Hz native snapshots are authoritative; never update Pages model here.
    return true;
  }
  function paintM4Readback(next) {
    const m=next?.raw?.legacyM4,controls=m?.channelControls;
    if(!m?.active||!m.routeAccepted||!Array.isArray(controls)||
       controls.length!==10||!scope?.render||!Array.isArray(scope?.tracks))return;
    let change=false;
    for(let i=0;i<10;i++){
      const ctl=controls[i],track=scope.tracks[i];
      if(!ctl||!track||track.kind!=="audio"||
         !Number.isFinite(ctl.db)||!Number.isFinite(ctl.pan)||
         typeof ctl.mute!=="boolean"||typeof ctl.solo!=="boolean")return;
      if(Math.abs(track.level-ctl.db)>.015){track.level=ctl.db;change=true;}
      if(Math.abs(track.pan-ctl.pan)>.015){track.pan=ctl.pan;change=true;}
      if(track.mute!==ctl.mute){track.mute=ctl.mute;change=true;}
      if(track.solo!==ctl.solo){track.solo=ctl.solo;change=true;}
    }
    if(scope.master && Number.isFinite(m.masterDb) &&
       Math.abs(scope.master.level-m.masterDb)>.015){
      scope.master.level=m.masterDb;change=true;
    }
    if(scope.master && typeof m.masterMute==="boolean" &&
       scope.master.mute!==m.masterMute){
      scope.master.mute=m.masterMute;change=true;
    }
    if(change)scope.render();
    paintM4Inputs(next);
    const l=m.channelPeaksL,r=m.channelPeaksR;
    if(!Array.isArray(l)||!Array.isArray(r)||!scope.displayMeterDbfs)return;
    const now=performance.now();
    for(let i=0;i<10;i++){
      const root=document.querySelector('.strip[data-id="'+scope.tracks[i]?.id+'"]');
      if(!root)continue;
      for(const [side,v] of [["left",l[i]],["right",r[i]]]){
        if(!Number.isFinite(v)||v<0)continue;
        const fill=root.querySelector('.meter-fill.'+side);
        if(fill)scope.displayMeterDbfs(fill,v===0?-90:
          Math.max(-90,Math.min(6,20*Math.log10(v))),now,
          "m4:"+i+":"+side);
      }
    }
  }
  function onNativeReadback(next) {
    updateHardwarePanel(next);
    const caps=next?.graph?.capabilities;
    const nativeAdmitted=next.admitted && caps?.callbackOwnsChannelIds===true;
    if (!nativeAdmitted) {
      flags.productionCallbackLive=false;flags.acceptedGraphConnected=false;
      lastPaintedSignature="";
      stopDemo();
      paintM4Readback(next);
      if(!m4Verified())paintM4Inputs(next);
      status(m4Verified()?
        "LIVE 10CH MIXER · INPUT/FADER/PAN/MUTE native · ChannelId migration pending":
        "NATIVE DEVICE · select real IN on channel; output remains safe until native route");
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
    backend.addEventListener("arMixConsoleSnapshot", snapshot=>{
      // Stage A: do not add time-sampling overhead unless explicitly armed.
      const profiler=window.ArSonMixPerf;
      if(!profiler?.active){controller.onSnapshot(snapshot);return;}
      const started=performance.now();
      try{controller.onSnapshot(snapshot);}
      finally{profiler.sample("nativeSnapshotJsMs",performance.now()-started);}
    });
    backend.addEventListener("arMixConsoleAck", ack=>controller.onAck(ack));
    backend.addEventListener("arMixSourceSwitchAck", ack=>{
      if(!pendingSourceId||ack?.requestId!==pendingSourceId)return;
      const selected=pendingStripSource;
      pendingSourceId=0;pendingStripSource=null;
      const result=ack?.state,channels=result?.acceptedRoute?.channels;
      const row=selected&&Array.isArray(channels)?channels[selected.channel]:null;
      const verified=!!selected&&ack?.ok===true&&result?.ready===true &&
        result?.hasAcceptedRoute===true&&
        Number(result.epoch)===selected.epoch&&
        Number(hardware?.device?.epoch)===selected.epoch&&
        Number.isSafeInteger(Number(result.revision))&&Number(result.revision)>0&&
        row?.mode===selected.mode&&row?.left===selected.left&&
        (selected.mode!=="STEREO"||row.right===selected.right);
      if(verified){
        status("CH "+(selected.channel+1)+" · INPUT CONNECTED · native ACK");
        // Avoid directly feeding VB-Cable playback back into its own
        // recording endpoint. A physical output or a deliberate independent
        // stream must be selected before software monitoring that loop.
        if(cableFeedbackPair())
          status("INPUT CONNECTED · VB-Cable feedback loop blocked; choose headphones in Audio I/O");
        else if(hardware?.device?.outputArmed!==true)
          backend.emitEvent("arMixAction",{
            action:"monitor",value:true,
            expectedEpoch:selected.epoch,
            expectedRevision:Number(result.revision)
          });
      }else if(ack?.ok!==true)status("INPUT REJECTED · "+(ack?.error||"device changed"));
      else if(selected)status("INPUT UNVERIFIED · source readback changed; MAIN remains safe");
      controller.subscribe(); // Never assume ACK alone is audible PCM.
      updateHardwarePanel(hardware);
      paintM4Inputs(hardware);
    });
    backend.addEventListener("arMixAudioArmAck", ack=>{
      if(ack?.ok===true){
        status(ack.armed?"NATIVE: user ARM accepted; checking real PCM.":
          "NATIVE: output muted.");
        controller.subscribe();
      }else status("NATIVE ARM REJECTED: "+(ack?.error||"device not ready"));
    });
  }
  const nativeHooks=Object.freeze({
    enabled:connected,
    consumeField(event) {
      if (!connected)return false;
      const el=event.target,field=el?.dataset?.field;
      if (!field)return false;
      const t=scope?.findTrack?.(el) || null;
      if (!t)return true;
      // Approved /preview/ IN selects from actual JUCE callback pins.
      // The installed M4 native graph owns the ten physical source switches.
      // This is the SAME engine route used by the live PCM callback.
      if(field==="input"){
        if(event.type==="change")chooseNativeStripInput(el);
        return true;
      }
      if (!flags.acceptedGraphConnected) {
        if(m4Verified()&&t.kind==="master"&&field==="level"){
          if(event.type==="change"&&Number.isFinite(Number(el.value)))
            backend.emitEvent("arMixParameter",{
              parameter:"masterGain",channel:-1,value:Number(el.value),
              routeEpoch:safeNativeCounter(hardware.device.epoch),
              routeRevision:safeNativeCounter(legacy().routingRevision)});
          return true;
        }
        const n=m4StripIndex(el);
        if(m4Verified()&&n>=0&&(field==="level"||field==="pan")){
          if(event.type!=="change")return true; // cap preview, native control on commit
          const numeric=Number(el.value);
          const value=field==="pan"?
            (!el.dataset.panKeyboard&&!el.dataset.panAltPointer&&Math.abs(numeric)<=12?0:numeric):
            numeric;
          const parameter=field==="level"?"faderDb":"pan";
          if(!m4Send(parameter,n,value))
            status("NATIVE M4: control rejected; waiting real readback");
          return true;
        }
        if(field==="level")el.value=String(t.level);
        else if(field==="pan")el.value=String(t.pan);
        status("NATIVE: route not yet accepted; control unchanged");
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
        if(!flags.acceptedGraphConnected && m4Verified()&&
           t.kind==="master"&&action==="mute"){
          backend.emitEvent("arMixParameter",{
            parameter:"masterMute",channel:-1,value:!t.mute,
            routeEpoch:safeNativeCounter(hardware.device.epoch),
            routeRevision:safeNativeCounter(legacy().routingRevision)});
          return true;
        }
        if(!flags.acceptedGraphConnected && m4Verified()){
          const n=m4StripIndex(btn);
          if(n>=0) {
            if(!m4Send(action,n,!t[action]))
              status("Native M4 control rejected; waiting readback");
            return true;
          }
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
      if (event.target.classList.contains("fader"))intent("SET_FADER",t,{valueDb:0});
      else if(event.target.classList.contains("center-pan"))intent("SET_PAN",t,{value:0});
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
  // Disable physical control gestures while G0 has no accepted ChannelId graph.
  // Presentation-only bank/selection remains available.
  document.addEventListener("pointerdown",event=>{
    if (flags.acceptedGraphConnected) return;
    if (m4Verified()&&
        (m4StripIndex(event.target)>=0||
         scope?.findTrack?.(event.target)?.kind==="master"))return;
    if (!event.target.closest?.(".fader-box,.center-pan")) return;
    event.preventDefault();event.stopImmediatePropagation();
    status("NATIVE G0: physical gain/pan unavailable until engine graph admission");
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
    installFirstSoundMenu();
    installNativeIo();
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
