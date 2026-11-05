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

  // Existing approved /preview/ mixer owns input-selection presentation.
  // web/mixer-io.js owns ten physical channel selectors and the SINGLE
  // custom WebView2 Audio I/O dialog. Do not add a competing CH1 wizard or
  // invoke the generic JUCE selector for routine live performance.
  let hardware=null,hardwareNote=null,muteBtn=null;
  const legacy=()=>hardware?.raw?.legacyM4;

  // Adobe Audition / FL Studio operator pattern:
  // one hardware setup, physical IN selector on EACH channel, master monitor.
  // G0 is still the real existing ten-channel M4 source kernel, not fake
  // ChannelId admission. Physical route mutation always needs JUCE ACK.
  const physicalUiIds=(scope?.tracks||[]).filter(t=>t.kind==="audio")
    .slice(0,10).map(t=>String(t.id));
  let sourceRequest=10000, sourcePending=null, deviceInventory=null;
  let deviceDialog=null, deviceDraft=null, devicePending=null, deviceMutePending=false;
  const actualPorts=()=>Array.isArray(hardware?.device?.inputEndpoints)?
    hardware.device.inputEndpoints.filter(p=>p?.active===true &&
      hardware.device.enabledInputs.includes(p.port)):[];
  const routeValue=r=>r?.mode==="MONO"?"MONO:"+r.left:
    r?.mode==="STEREO"?"STEREO:"+r.left+":"+r.right:"NONE";
  const firstSoundReady=()=>hardware?.raw?.legacyM4?.active===true;
  function portOptions(){
    const ports=actualPorts(),opts=[["NONE","None"]];
    const pins=new Set(ports.map(p=>p.port));
    for(const p of ports)opts.push(["MONO:"+p.port,"Mono · "+p.label]);
    for(const p of ports)if(pins.has(p.port+1))
      opts.push(["STEREO:"+p.port+":"+(p.port+1),
        "Stereo · IN "+(p.port+1)+"/"+(p.port+2)]);
    return opts;
  }
  function syncDawNativeInputs(){
    const options=portOptions();
    const sources=hardware?.raw?.legacyM4?.channelSources;
    // ACK is not final presentation authority. Keep the source disabled
    // until the following CURRENT-EPOCH snapshot proves the accepted route.
    if(sourcePending?.acked && Array.isArray(sources) &&
       sources.length===10 &&
       String(hardware?.device?.epoch)===String(sourcePending.epoch) &&
       routeValue(sources[sourcePending.channel])===
         routeValue(sourcePending)){
      sourcePending=null;
    }
    if(sourcePending &&
       String(hardware?.device?.epoch)!==String(sourcePending.epoch))
      sourcePending=null; // hotplug/driver change cancels stale source UI
    const signature=JSON.stringify(options);
    for(let i=0;i<physicalUiIds.length;i++){
      const strip=document.querySelector('.strip[data-id="'+physicalUiIds[i]+'"]');
      const select=strip?.querySelector('.route-select[data-field="input"]');
      if(!select)continue;
      select.dataset.nativeChannel=String(i);
      if(select.dataset.nativePins!==signature){
        const nodes=options.map(([value,label])=>{
          const o=document.createElement("option");o.value=value;o.textContent=label;return o;
        });
        select.replaceChildren(...nodes);
        select.dataset.nativePins=signature;
      }
      const accepted=Array.isArray(sources)&&sources.length===10?
        routeValue(sources[i]):"NONE";
      const pending=sourcePending?.channel===i;
      if(!pending && select.value!==accepted)select.value=accepted;
      select.disabled=!firstSoundReady()||!!sourcePending||!Array.isArray(sources)||
        sources.length!==10;
      select.title="CH "+(i+1)+" · verified physical input · native ACK";
    }
    const master=document.querySelector(".strip.master .route-top");
    if(master){
      let monitor=master.querySelector("[data-native-monitor]");
      if(!monitor){
        monitor=document.createElement("button");
        monitor.type="button";monitor.className="mx-util-key";
        monitor.dataset.nativeMonitor="1";
        monitor.setAttribute("aria-label","Enable or mute MAIN output monitoring");
        monitor.addEventListener("click",event=>{
          event.preventDefault();event.stopPropagation();
          const armed=hardware?.device?.outputArmed===true;
          if(!armed && (!firstSoundReady()||
              hardware?.raw?.legacyM4?.routeAccepted!==true)){
            status("Select a physical input on a channel before monitoring MAIN");
            return;
          }
          const epoch=Number(hardware?.device?.epoch);
          const rev=Number(hardware?.raw?.legacyM4?.routingRevision);
          if(!armed && (!Number.isSafeInteger(epoch)||!Number.isSafeInteger(rev)||
                       epoch<=0||rev<=0))return;
          backend.emitEvent("arMixAction",{action:"monitor",value:!armed,
            ...(!armed?{expectedEpoch:epoch,expectedRevision:rev}:{})});
          status(!armed?"MAIN monitor requested · awaiting hardware ACK":
            "MAIN mute requested");
        });
        master.appendChild(monitor);
      }
      const armed=hardware?.device?.outputArmed===true;
      monitor.textContent=armed?"I ON":"I OFF";
      monitor.title=armed?"MAIN output monitoring ON · click to MUTE":
        "MAIN input monitoring OFF · click to listen";
      monitor.setAttribute("aria-pressed",String(armed));
      monitor.disabled=!armed && (!firstSoundReady()||
        hardware?.raw?.legacyM4?.routeAccepted!==true);
    }
  }
  function changeNativeInput(event){
    const select=event.target?.closest?.('.route-select[data-field="input"]');
    if(!select || !connected)return;
    event.preventDefault();event.stopImmediatePropagation();
    const index=Number(select.dataset.nativeChannel);
    const parts=String(select.value).split(":");
    const mode=parts[0],left=mode==="NONE"?-1:Number(parts[1]);
    const right=mode==="STEREO"?Number(parts[2]):-1;
    const allowed=portOptions().some(pair=>pair[0]===select.value);
    const epoch=Number(hardware?.device?.epoch);
    const revision=Number(hardware?.raw?.legacyM4?.routingRevision);
    if(!firstSoundReady()||!!sourcePending||!allowed||
       !Number.isInteger(index)||index<0||index>=10||
       !Number.isSafeInteger(epoch)||epoch<=0||
       !Number.isSafeInteger(revision)||revision<=0){
      status("INPUT REJECTED · verify hardware, native graph and actual ports");
      syncDawNativeInputs();return;
    }
    sourcePending={id:++sourceRequest,channel:index,mode,left,right,
      epoch,revision};
    backend.emitEvent("arMixAction",{action:"routingSourceSwitch",
      requestId:sourcePending.id,channel:index,mode,left,right,
      deviceEpoch:epoch,revision});
    status("CH "+(index+1)+" INPUT · applying hardware source · native ACK pending");
    syncDawNativeInputs();
  }
  function createAudioDeviceDialog(){
    if(deviceDialog)return deviceDialog;
    const style=document.createElement("style");
    style.id="armix-native-device-style";
    style.textContent=`
      #armixNativeDevice{width:min(560px,94vw);max-height:85vh;overflow:auto;
        background:#13232d;color:#d9e7ee;border:1px solid #476373;
        border-radius:9px;box-shadow:0 18px 65px #0009;padding:17px 19px;
        font:12px Inter,Segoe UI,sans-serif}
      #armixNativeDevice::backdrop{background:#0009}
      #armixNativeDevice .io-top{display:flex;justify-content:space-between;
        align-items:center;gap:12px;margin-bottom:15px}
      #armixNativeDevice h3{font-size:15px;font-weight:550;margin:0}
      #armixNativeDevice .io-fields{display:grid;grid-template-columns:124px 1fr;
        align-items:center;gap:11px}
      #armixNativeDevice label{color:#abbdc8}
      #armixNativeDevice select{width:100%;min-width:0;padding:7px;background:#1d3441;
        color:#e9f3fa;border:1px solid #536f7b;border-radius:5px;font:inherit}
      #armixNativeDevice button{background:#284454;color:#f0f7fa;
        border:1px solid #607b89;border-radius:5px;padding:7px 13px;
        font:inherit;cursor:pointer}
      #armixNativeDevice button:disabled{opacity:.44;cursor:default}
      #armixNativeDevice .io-actions{display:flex;justify-content:flex-end;gap:8px;
        margin-top:16px}
      #armixNativeDevice [data-io-status]{display:block;margin-top:14px;min-height:2em;
        color:#bcd3df;line-height:1.5}
      #armixNativeDevice .io-advanced{margin-top:14px}
      #armixNativeDevice .io-advanced summary{cursor:pointer;color:#b3c8d3}
    `;
    document.head.appendChild(style);
    deviceDialog=document.createElement("dialog");deviceDialog.id="armixNativeDevice";
    deviceDialog.innerHTML=`
      <div class="io-top"><h3>Audio I/O · Hardware</h3>
        <button type="button" data-io-close aria-label="Close">Close</button></div>
      <div class="io-fields">
        <label for="armixDriver">Driver</label><select id="armixDriver"></select>
        <label for="armixCapture">Input device</label><select id="armixCapture"></select>
        <label for="armixPlayback">Output device</label><select id="armixPlayback"></select>
      </div>
      <details class="io-advanced"><summary>Advanced · Sample rate / Buffer</summary>
        <div class="io-fields" style="margin-top:11px">
          <label for="armixRate">Sample rate</label>
          <select id="armixRate"><option value="0">Auto / driver default</option>
          <option value="44100">44,100 Hz</option><option value="48000">48,000 Hz</option></select>
          <label for="armixBuffer">Buffer</label><select id="armixBuffer">
            <option value="0">Auto / driver default</option><option value="128">128 samples</option>
            <option value="256">256 samples</option><option value="480">480 samples</option>
            <option value="512">512 samples</option></select>
        </div>
      </details>
      <small data-io-status role="status">Reading actual JUCE devices…</small>
      <div class="io-actions"><button type="button" data-io-refresh>Refresh</button>
        <button type="button" data-io-apply>Apply device</button></div>`;
    document.body.appendChild(deviceDialog);
    const get=id=>deviceDialog.querySelector("#"+id);
    deviceDialog.querySelector("[data-io-close]").addEventListener("click",
      ()=>deviceDialog.close());
    deviceDialog.querySelector("[data-io-refresh]").addEventListener("click",
      ()=>backend.emitEvent("arMixAction",{action:"audioDeviceInventory"}));
    get("armixDriver").addEventListener("change",()=>{
      const type=deviceInventory?.types?.find(x=>x.name===get("armixDriver").value);
      deviceDraft={backend:type?.name||"",captureDevice:type?.inputs?.[0]||"",
        playbackDevice:type?.outputs?.[0]||"",sampleRate:0,bufferSize:0};
      refreshAudioDeviceDialog();
    });
    get("armixCapture").addEventListener("change",()=>{
      if(deviceDraft)deviceDraft.captureDevice=get("armixCapture").value;
    });
    get("armixPlayback").addEventListener("change",()=>{
      if(deviceDraft)deviceDraft.playbackDevice=get("armixPlayback").value;
      refreshAudioDeviceHint();
    });
    deviceDialog.querySelector("[data-io-apply]").addEventListener("click",()=>{
      if(!deviceInventory?.active||!deviceDraft||devicePending)return;
      deviceDraft.sampleRate=Number(get("armixRate").value);
      deviceDraft.bufferSize=Number(get("armixBuffer").value);
      const type=deviceInventory.types.find(x=>x.name===deviceDraft.backend);
      if(!type?.inputs?.includes(deviceDraft.captureDevice)||
         !type?.outputs?.includes(deviceDraft.playbackDevice)||
         (!type.separate&&deviceDraft.captureDevice!==deviceDraft.playbackDevice)){
        getStatus("Invalid driver/input/output selection");return;
      }
      devicePending={requestId:"io-"+(++sourceRequest),
        generation:Number(deviceInventory.active.deviceGeneration)};
      if(hardware?.device?.outputArmed===true){
        deviceMutePending=true;
        backend.emitEvent("arMixAction",{action:"monitor",value:false});
        getStatus("Muting MAIN safely before switching device…");
      }else sendDeviceApply();
    });
    return deviceDialog;
  }
  function getStatus(message){
    const el=deviceDialog?.querySelector("[data-io-status]");
    if(el)el.textContent=message;
  }
  function fillSelect(el,items,current){
    el.replaceChildren(...items.map(([v,label])=>{
      const o=document.createElement("option");o.value=String(v);o.textContent=label;
      return o;
    }));
    if(items.some(x=>String(x[0])===String(current)))el.value=String(current);
  }
  function refreshAudioDeviceHint(){
    const output=String(deviceDraft?.playbackDevice||"");
    const virtual=/virtual|cable input|vb-audio|voicemeeter/i.test(output);
    getStatus(virtual?"Virtual playback output · NOT automatically audible on headphones":
      "Selected playback: "+(output||"none")+" · output stays muted during setup");
  }
  function refreshAudioDeviceDialog(){
    if(!deviceDialog?.open||!deviceInventory)return;
    const d=deviceDialog,types=Array.isArray(deviceInventory.types)?
      deviceInventory.types:[];
    const active=deviceInventory.active||{};
    if(!deviceDraft){
      deviceDraft={backend:active.backend||types[0]?.name||"",
        captureDevice:active.captureDevice||"",playbackDevice:active.playbackDevice||"",
        sampleRate:0,bufferSize:0};
    }
    const type=types.find(x=>x.name===deviceDraft.backend);
    fillSelect(d.querySelector("#armixDriver"),types.map(t=>[t.name,t.name]),deviceDraft.backend);
    fillSelect(d.querySelector("#armixCapture"),
      (type?.inputs||[]).map(x=>[x,x]),deviceDraft.captureDevice);
    fillSelect(d.querySelector("#armixPlayback"),
      (type?.outputs||[]).map(x=>[x,x]),deviceDraft.playbackDevice);
    const apply=d.querySelector("[data-io-apply]");
    apply.disabled=!!devicePending||!type||!type.inputs?.includes(deviceDraft.captureDevice)||
      !type.outputs?.includes(deviceDraft.playbackDevice);
    refreshAudioDeviceHint();
  }
  function openNativeAudioSetup(){
    const dialog=createAudioDeviceDialog();
    if(!dialog.open)dialog.showModal();
    backend.emitEvent("arMixAction",{action:"audioDeviceInventory"});
  }
  function sendDeviceApply(){
    if(!devicePending||!deviceInventory?.active)return;
    const active=deviceInventory.active;
    const same=active.backend===deviceDraft.backend &&
      active.captureDevice===deviceDraft.captureDevice &&
      active.playbackDevice===deviceDraft.playbackDevice;
    const config={...deviceDraft};
    if(same && Array.isArray(active.enabledInputs) &&
       Array.isArray(active.enabledOutputs)){
      config.enabledInputs=active.enabledInputs.slice();
      config.enabledOutputs=active.enabledOutputs.slice();
    }
    // For NEW devices, physical pin names are not yet verified; native JUCE
    // opens safe defaults before the next authoritative inventory.
    backend.emitEvent("arMixAction",{
      action:"audioDeviceApply",
      requestId:devicePending.requestId,
      expectedDeviceGeneration:devicePending.generation,config
    });
    getStatus("Opening selected hardware · verifying actual driver readback…");
  }
  function updateHardwarePanel(next) {
    hardware=next||null;
    syncDawNativeInputs();
    const d=next?.device;
    const deviceOpen=Boolean(d?.backend && d.sampleRate>0 &&
      d.enabledOutputs.length>=2);
    const label=document.getElementById("transport");
    if(label){
      const nextText=!deviceOpen?"Device not ready":
        d.outputArmed?"Output enabled":"Output muted";
      if(label.textContent!==nextText)label.textContent=nextText;
    }
    const warning=document.querySelector?.(".status-bar .warning");
    if(warning){
      const nextText=!deviceOpen?"NO VERIFIED AUDIO DEVICE":
        d.outputArmed?(next.admitted?"NATIVE PCM · HARDWARE FAT PENDING":
          "OUTPUT ENABLED · VERIFY AUDIO"):
        legacy()?.routeAccepted?"INPUT ROUTED · OUTPUT MUTED":
        "SELECT HARDWARE INPUT ON ANY CHANNEL";
      if(warning.textContent!==nextText)warning.textContent=nextText;
    }
    if(muteBtn)muteBtn.disabled=!deviceOpen||!d.outputArmed;
    if(hardwareNote){
      const evidence=protocol.diagnoseFirstSound?.(next);
      const text=evidence?evidence.readout+" · "+
        evidence.nextAction:"Awaiting verified hardware readback";
      if(hardwareNote.textContent!==text)hardwareNote.textContent=text;
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
    const caption=item("span","more-tools-caption","HARDWARE AUDIO · ADVANCED");
    const setup=item("button","tool","Audio I/O settings");
    // Same custom device dialog as the toolbar's Audio Setup button.
    // No additional native AudioDeviceSelectorComponent window.
    setup.addEventListener("click",()=>{
      openNativeAudioSetup();
    });
    muteBtn=item("button","tool","Mute output");
    muteBtn.addEventListener("click",()=>{
      backend.emitEvent("arMixAction",{action:"monitor",value:false});
      status("Native output MUTE requested; awaiting engine ACK");
    });
    hardwareNote=item("span","more-tools-caption","Select IN on any channel strip");
    hardwareNote.style.whiteSpace="normal";
    menu.append(caption,setup,muteBtn,hardwareNote);
    updateHardwarePanel(hardware);
  }

  function onNativeReadback(next) {
    updateHardwarePanel(next);
    const caps=next?.graph?.capabilities;
    const nativeAdmitted=next.admitted && caps?.callbackOwnsChannelIds===true;
    if (!nativeAdmitted) {
      flags.productionCallbackLive=false;flags.acceptedGraphConnected=false;
      lastPaintedSignature="";
      stopDemo();
      status("NATIVE G0 · pilih hardware IN langsung pada CH1–CH10; fader/WIRE ChannelId tetap menunggu accepted PCM.");
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
    backend.addEventListener("arMixConsoleSnapshot", snapshot=>controller.onSnapshot(snapshot));
    backend.addEventListener("arMixConsoleAck", ack=>controller.onAck(ack));
    // Accepted native M4 source-switch ACK, verified against actual route
    // readback; neither UI option nor ACK alone invents PCM or monitoring.
    backend.addEventListener("arMixSourceSwitchAck",ack=>{
      const p=sourcePending;
      if(!p||ack?.requestId!==p.id||ack?.channel!==p.channel)return;
      const route=ack?.state?.acceptedRoute?.channels?.[p.channel];
      const valid=ack.ok===true&&Number(ack?.state?.epoch)===p.epoch&&
        route?.mode===p.mode&&
        (p.mode==="NONE" || (route?.left===p.left&&route?.right===p.right));
      if(valid)sourcePending.acked=true;
      else sourcePending=null;
      status(valid?"CH "+(p.channel+1)+" source ACK accepted · verifying next readback":
        "CH "+(p.channel+1)+" input rejected · "+String(ack?.error||"readback mismatch"));
      controller?.subscribe();
      syncDawNativeInputs();
    });
    backend.addEventListener("arMixAudioDeviceInventory",list=>{
      if(!list || !Array.isArray(list.types))return;
      deviceInventory=list;
      refreshAudioDeviceDialog();
    });
    backend.addEventListener("arMixAudioDeviceApplyAck",ack=>{
      if(!devicePending||ack?.requestId!==devicePending.requestId)return;
      devicePending=null;
      deviceDraft=null;
      getStatus(ack.ok===true?
        "Device accepted · "+ack.active?.backend+" · "+ack.active?.rate+
        " Hz · "+ack.active?.playbackDevice+" · select IN on channel":
        "Device rejected · "+String(ack.error||ack.state||"unavailable"));
      backend.emitEvent("arMixAction",{action:"audioDeviceInventory"});
      controller?.subscribe();
    });

    backend.addEventListener("arMixAudioArmAck", ack=>{
      if(deviceMutePending){
        deviceMutePending=false;
        if(ack?.ok===true&&ack.armed===false)sendDeviceApply();
        else {devicePending=null;getStatus("Cannot safely mute output; device unchanged");}
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
    // Capture source selection BEFORE the public Pages demo reducer. It
    // remains the same visible IN dropdown, never a separate floating patch.
    document.addEventListener("change",changeNativeInput,true);
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
