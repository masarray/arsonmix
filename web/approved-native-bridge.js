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
  // Shipped DAW-style physical-source controls. These live in the ONE
  // approved /preview/ WebView2, not web/mixer-io.js (not part of this EXE).
  // The ORIGINAL visible strip IN row replaces demo input choices. Each of
  // the ten installed M4 inputs has its own native routing command and ACK.
  let selectedSourcePending=null, selectedSourceSerial=5000;
  const availablePhysicalInputs=next=>{
    const d=next?.device;
    if(!next?.raw?.legacyM4?.active || !Array.isArray(d?.inputEndpoints) ||
       !Array.isArray(d?.enabledInputs))return [];
    return d.inputEndpoints.filter(p=>
      p?.active===true && Number.isInteger(p.port) &&
      d.enabledInputs.includes(p.port)).sort((a,b)=>a.port-b.port);
  };
  const routeValue=r=>r?.mode==="MONO"?"MONO:"+r.left:
    r?.mode==="STEREO"?"STEREO:"+r.left+":"+r.right:
    r?.mode==="NONE"?"NONE":"CUSTOM";
  const sourceChoices=pins=>{
    const items=[["NONE","None · silent"]];
    for(const p of pins)
      items.push(["MONO:"+p.port,"Mono · "+String(p.label||"IN "+(p.port+1))]);
    for(let i=0;i+1<pins.length;i++)
      if(pins[i+1].port===pins[i].port+1)
        items.push(["STEREO:"+pins[i].port+":"+pins[i+1].port,
          "Stereo · IN "+(pins[i].port+1)+"/"+(pins[i+1].port+1)]);
    return items;
  };
  function renderVerifiedStripInputs(next) {
    if(!connected||!scope?.tracks)return;
    const d=next?.device,legacyRoute=next?.raw?.legacyM4;
    const rows=Array.isArray(legacyRoute?.channels)?legacyRoute.channels:[];
    const pins=availablePhysicalInputs(next),choices=sourceChoices(pins);
    const signature=JSON.stringify(choices);
    const ready=legacyRoute?.active===true && rows.length===10 && pins.length>0;
    const tracks=scope.tracks.filter(t=>t.kind==="audio").slice(0,10);
    for(let channel=0;channel<tracks.length;channel++){
      const track=tracks[channel];
      const strip=document.querySelector('.strip[data-id="'+track.id+'"]');
      if(!strip)continue; // off-screen banks are decorated when rendered
      const demo=strip.querySelector('select.route-select[data-field="input"]');
      let select=strip.querySelector('select[data-armix-physical-input]');
      if(demo){
        select=document.createElement("select");
        select.className="route-select";
        select.dataset.armixPhysicalInput=String(channel);
        select.setAttribute("aria-label","CH "+(channel+1)+" hardware input");
        select.title="Choose physical input directly · native ACK required";
        demo.replaceWith(select);
      }
      if(select){
        if(select.dataset.options!==signature){
          select.replaceChildren(...choices.map(([value,label])=>{
            const option=document.createElement("option");
            option.value=value;option.textContent=label;return option;
          }));
          select.dataset.options=signature;
        }
        const live=rows[channel],value=routeValue(live);
        if(value==="CUSTOM" && !Array.from(select.options).some(o=>o.value===value)){
          const extra=document.createElement("option");
          extra.value="CUSTOM";extra.textContent="Internal bus · advanced";
          select.appendChild(extra);
        }
        const selected=Array.from(select.options).some(o=>o.value===value)?value:"NONE";
        if(select.value!==selected)select.value=selected;
        const disabled=!ready || !!selectedSourcePending;
        if(select.disabled!==disabled)select.disabled=disabled;
        select.title=ready?"CH "+(channel+1)+" · "+(select.selectedOptions[0]?.textContent||"Input")+
          " · accepted hardware route":"Choose real ASIO/WASAPI input/output in Audio Setup";
      }
      // Don't display Pages-only SOURCE ONLY and phantom hardware outputs.
      const demoOut=strip.querySelector('select.route-select[data-field="output"]');
      let actualOut=strip.querySelector('[data-armix-native-output]');
      if(demoOut){
        actualOut=document.createElement("span");
        actualOut.className="route-select";
        actualOut.dataset.armixNativeOutput=String(channel);
        demoOut.replaceWith(actualOut);
      }
      if(actualOut){
        const accepted=legacyRoute?.routeAccepted===true && ready;
        const text=!accepted?"MAIN · unverified":
          rows[channel]?.main===false?"SEND ONLY":
          rows[channel]?.mode==="NONE"?"MAIN · no input":"MAIN";
        if(actualOut.textContent!==text)actualOut.textContent=text;
        actualOut.title="Native output "+String(d?.playbackDevice||"not set");
      }
    }
  }
  function requestNativeSource() {
    const p=selectedSourcePending;
    if(!p)return;
    p.phase="patch";
    backend.emitEvent("arMixAction",{
      action:"routingSourceSwitch",requestId:p.id,channel:p.channel,
      mode:p.mode,left:p.left,right:p.right,
      deviceEpoch:p.epoch,revision:p.revision
    });
    status("CH "+(p.channel+1)+" · input routing requested");
  }
  function completeNativeSourceFromReadback(next) {
    const p=selectedSourcePending;
    if(!p||p.phase!=="readback")return;
    if(next?.device?.epoch!==String(p.epoch)){
      selectedSourcePending=null;
      status("Device changed · select input again");return;
    }
    const l=next?.raw?.legacyM4,actual=l?.channels?.[p.channel];
    if(l?.routeAccepted!==true||routeValue(actual)!==p.value)return;
    if(next.device.outputArmed){
      selectedSourcePending=null;
      status("CH "+(p.channel+1)+" · hardware input LIVE");return;
    }
    // One operator input choice also requests monitoring after an actual
    // native patch ACK. Native liveOutputArmFault remains final authority.
    // Driver opens/hotplug NEVER initiate this independently.
    const anyRouted=l.channels.some(r=>r.main!==false &&
      (r.mode==="MONO"||r.mode==="STEREO"));
    if(!anyRouted){
      selectedSourcePending=null;
      status("Inputs unpatched · output remains muted");return;
    }
    const revision=safeNativeCounter(l.routingRevision);
    if(revision===null){selectedSourcePending=null;return;}
    p.phase="arming";
    backend.emitEvent("arMixAction",{action:"monitor",value:true,
      expectedEpoch:p.epoch,expectedRevision:revision});
  }
  function installNativeStripInputEvents() {
    const bank=document.getElementById("tracks");
    if(bank && typeof MutationObserver==="function"){
      const observer=new MutationObserver(()=>renderVerifiedStripInputs(hardware));
      // Only the bank's direct children change on re-render/bank switch.
      // Updating <option> children never retriggers this observer.
      observer.observe(bank,{childList:true});
    }
    document.addEventListener("change",event=>{
      const select=event.target?.closest?.("select[data-armix-physical-input]");
      if(!select)return;
      event.preventDefault();event.stopImmediatePropagation();
      const channel=Number(select.dataset.armixPhysicalInput);
      const d=hardware?.device,l=legacy();
      const epoch=safeNativeCounter(d?.epoch);
      const revision=safeNativeCounter(l?.routingRevision);
      const row=l?.channels?.[channel];
      const tokens=select.value.split(":");
      const mode=tokens[0],left=mode==="NONE"?-1:Number(tokens[1]);
      const right=mode==="STEREO"?Number(tokens[2]):-1;
      const pins=availablePhysicalInputs(hardware).map(x=>x.port);
      const valid=(mode==="NONE") ||
        (mode==="MONO"&&pins.includes(left)) ||
        (mode==="STEREO"&&pins.includes(left)&&pins.includes(right)&&left!==right);
      if(!deviceReady()||!Array.isArray(l?.channels)||l.channels.length!==10||
         !Number.isInteger(channel)||channel<0||channel>=10||
         epoch===null||revision===null||!valid||selectedSourcePending){
        renderVerifiedStripInputs(hardware);
        status("Input selection rejected · verify driver and channel");return;
      }
      const value=mode==="MONO"?"MONO:"+left:
        mode==="STEREO"?"STEREO:"+left+":"+right:"NONE";
      if(routeValue(row)===value)return;
      const p={id:++selectedSourceSerial,channel,mode,left,right,value,
        epoch,revision,phase:"initial"};
      selectedSourcePending=p;
      renderVerifiedStripInputs(hardware);
      // Same-format switch uses native atomic source crossfade. Structural
      // changes must first safely mute MAIN; never add a second DSP callback.
      const structural=row?.mode!==mode;
      if(d.outputArmed && structural){
        p.phase="muting";
        backend.emitEvent("arMixAction",{action:"monitor",value:false});
      } else requestNativeSource();
      window.setTimeout(()=>{
        if(selectedSourcePending?.id===p.id){
          selectedSourcePending=null;
          renderVerifiedStripInputs(hardware);
          status("Input request timed out · native route unchanged or unverified");
          controller?.subscribe();
        }
      },8000);
    },true);
  }
  function updateHardwarePanel(next) {
    hardware=next || null;
    renderVerifiedStripInputs(next);
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
  function installFirstSoundMenu() {
    const menu=document.querySelector?.("#moreTools .more-tools-panel");
    if(!menu || typeof document.createElement!=="function")return;
    const item=(tag,cls,label)=>{
      const el=document.createElement(tag);
      el.className=cls;el.textContent=label;
      if(tag==="button")el.type="button";
      return el;
    };
    // The approved per-strip IN selector is the everyday workflow.
    // Keep old CH1 patch/ARM as collapsed diagnostics only, not a wizard.
    const advanced=document.createElement("details");
    advanced.className="arNativeAdvancedRecovery";
    const summary=item("summary","more-tools-caption","ADVANCED · DRIVER / FIRST SOUND DIAGNOSTICS");
    advanced.appendChild(summary);
    const caption=item("span","more-tools-caption","HARDWARE RECOVERY ONLY");
    const setup=item("button","tool","Audio I/O");
    setup.addEventListener("click",()=>{
      // The pre-existing WebView I/O pane already supports native device
      // inventory + one audited Apply transaction. Do not open a duplicate
      // huge JUCE AudioDeviceSelectorComponent for routine setup.
      if(typeof window.__arMixOpenAudioIO==="function")
        window.__arMixOpenAudioIO("device");
      else status("AUDIO I/O unavailable; use the mixer AUDIO I/O button.");
    });
    const driver=item("button","tool","Advanced JUCE driver settings");
    driver.addEventListener("click",()=>{
      backend.emitEvent("arMixAction",{action:"audioSetup"});
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
    advanced.append(caption,setup,driver,inputSelect,routeBtn,armBtn,muteBtn,hardwareNote);
    menu.appendChild(advanced);
    updateHardwarePanel(hardware);
  }

  function onNativeReadback(next) {
    updateHardwarePanel(next);
    completeNativeSourceFromReadback(next);
    const caps=next?.graph?.capabilities;
    const nativeAdmitted=next.admitted && caps?.callbackOwnsChannelIds===true;
    if (!nativeAdmitted) {
      flags.productionCallbackLive=false;flags.acceptedGraphConnected=false;
      lastPaintedSignature="";
      stopDemo();
      status("NATIVE G0 · PATCH CH1/ARM tersedia pada device terverifikasi; kontrol ChannelId/WIRE menunggu PCM native.");
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
      const p=selectedSourcePending;
      if(p?.phase==="patch" && ack?.requestId===p.id &&
         Number(ack.channel)===p.channel){
        if(ack.ok===true){
          p.phase="readback";
          controller.subscribe(); // require a subsequent native producer snapshot
        }else{
          selectedSourcePending=null;
          status("CH "+(p.channel+1)+" · input rejected: "+(ack.error||"stale route"));
          renderVerifiedStripInputs(hardware);
          controller.subscribe();
        }
        return;
      }
      if(!pendingSourceId||ack?.requestId!==pendingSourceId)return;
      pendingSourceId=0;
      if(ack.ok===true) {
        status("NATIVE: CH1 PATCH accepted by JUCE; validating later readback.");
        controller.subscribe();
      }else status("NATIVE PATCH REJECTED: "+(ack?.error||"stale physical route"));
      updateHardwarePanel(hardware);
    });
    backend.addEventListener("arMixAudioArmAck", ack=>{
      const p=selectedSourcePending;
      if(p?.phase==="muting"){
        if(ack?.ok===true && ack.armed===false &&
           Number(ack.deviceEpoch)===p.epoch){
          requestNativeSource();
        }else{
          selectedSourcePending=null;
          status("Cannot safely switch source · output mute rejected");
          renderVerifiedStripInputs(hardware);
        }
        return;
      }
      if(p?.phase==="arming"){
        selectedSourcePending=null;
        renderVerifiedStripInputs(hardware);
        status(ack?.ok===true&&ack?.armed===true?
          "CH "+(p.channel+1)+" · monitoring enabled":
          "Input patched; monitoring unavailable: "+(ack?.error||"device not ready"));
        controller.subscribe();
        return;
      }
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
      if (!flags.acceptedGraphConnected) {
        if (field==="level")el.value=String(t.level);
        else if (field==="pan")el.value=String(t.pan);
        status("NATIVE: ChannelId PCM not admitted; control unchanged");
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
    installNativeStripInputEvents();
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
