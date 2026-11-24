// SPDX-License-Identifier: AGPL-3.0-only
// Read-only Device I/O presentation projection for the ONE approved native /preview/.
// This is NOT a device manager, session store, routing command bus, PCM engine,
// or physical availability authority. All choices derive from native readback.
(function (root, factory) {
  const api = factory();
  if (typeof module === "object" && module.exports) module.exports = api;
  if (root) root.ArSonApprovedDeviceViewModel = api;
})(typeof globalThis === "object" ? globalThis : undefined, function () {
  "use strict";
  const INTERNAL_BUS = "Internal BUS (read-only)";
  const NONE = {label:"None",left:-1,right:-1};
  // MV-8: JUCE device pin masks are SETS of physical port ordinals.
  // Keep one pure contract for draft admission, ACK verification and
  // independently installed device readback. Never compare array order,
  // coerce floating ports, invent unavailable pins or mask a driver remap.
  function validPinMask(pins,reportedNames) {
    const upper=Array.isArray(reportedNames)?
      Math.min(64,reportedNames.length):64;
    return Array.isArray(pins) && pins.length<=upper &&
      pins.every(p=>Number.isInteger(p)&&p>=0&&p<upper) &&
      new Set(pins).size===pins.length;
  }
  function samePinMask(a,b) {
    return validPinMask(a)&&validPinMask(b)&&a.length===b.length&&
      a.every(pin=>b.includes(pin));
  }
  // The native contract validates endpoints before reaching this selector.
  // Additional guards keep a lost/partial snapshot from advertising phantom pins.
  function activeInputPins(snapshot) {
    const d=snapshot?.device;
    if(!Array.isArray(d?.inputEndpoints)||!Array.isArray(d.enabledInputs))return [];
    return d.inputEndpoints.filter(p=>p?.active===true &&
      Number.isInteger(p.port) && d.enabledInputs.includes(p.port));
  }
  function inputChoices(snapshot) {
    const pins=activeInputPins(snapshot);
    const entries=[{label:"None",mode:"NONE",left:-1,right:-1}];
    for(const p of pins)entries.push({
      label:"Mono · "+p.label+" [IN "+(p.port+1)+"]",
      mode:"MONO",left:p.port,right:-1});
    // Preserve existing physical adjacency semantics, never pair packed
    // callback ordinals or guess unavailable driver pins.
    for(let i=0;i+1<pins.length;++i)
      if(pins[i+1].port===pins[i].port+1)
        {
          const a=pins[i],b=pins[i+1];
          const generic=p=>new RegExp("^\\s*(?:in|input)\\s*"+(p.port+1)+"\\s*$","i")
            .test(String(p.label||""));
          const pair="Stereo · IN "+(a.port+1)+" + "+(b.port+1);
          entries.push({label:generic(a)&&generic(b)?pair:
            pair+" · "+a.label+" / "+b.label,mode:"STEREO",
            left:a.port,right:b.port});
        }
    return entries;
  }
  function inputLabel(snapshot,row) {
    if(!row||row.mode==="NONE")return "None";
    if(row.mode==="BUS")return INTERNAL_BUS;
    const found=inputChoices(snapshot).find(x=>x.mode===row.mode &&
      x.left===row.left && x.right===row.right);
    return found?.label||"None";
  }
  function outputChoices(snapshot) {
    const d=snapshot?.device,choices=[{...NONE}];
    if(d?.backend!=="ASIO"||!Array.isArray(d.outputEndpoints)||
       !Array.isArray(d.enabledOutputs))return choices;
    const map=new Map(d.outputEndpoints.filter(p=>
      p?.active===true&&Number.isInteger(p.port)&&
      d.enabledOutputs.includes(p.port)&&
      typeof p.label==="string"&&p.label.length>0).map(p=>[p.port,p]));
    const main=Array.isArray(snapshot?.raw?.physicalMainOutputs)?
      snapshot.raw.physicalMainOutputs:[];
    const monitor=Array.isArray(snapshot?.raw?.physicalMonitorOutputs)?
      snapshot.raw.physicalMonitorOutputs:[];
    for(const [left,a] of map) {
      if(left%2!==0||!map.has(left+1))continue;
      const b=map.get(left+1);
      if(main.includes(left)||main.includes(left+1)||
         monitor.includes(left)||monitor.includes(left+1))continue;
      const shared=Array.isArray(snapshot?.raw?.externalOutputs)?
        snapshot.raw.externalOutputs.filter(row=>
          row.physicalLeft===left&&row.physicalRight===left+1).length:0;
      choices.push({label:"OUT "+(left+1)+"/"+(left+2)+" · "+
        a.label+" + "+b.label+
        (shared>1?" · SHARED "+shared+" SOURCES":""),
        left,right:left+1});
    }
    return choices;
  }
  // Project one installed row for the existing DOM. ACK-only and logical
  // assignments never promote output status to LIVE; that still requires
  // matching verified pins AND accepted native graph AND armed output.
  function externalPresentation(snapshot,row,choices=outputChoices(snapshot)) {
    const match=row&&choices.find(p=>p.left===row.physicalLeft &&
      p.right===row.physicalRight);
    const installed=row?.status==="LIVE"&&match;
    const state=installed?(snapshot?.device?.outputArmed===true?"LIVE":"MUTED"):
      row?.status==="PENDING"&&match?"PENDING":row?"NEEDS_REMAP":"NONE";
    const value=row?((state==="LIVE"||state==="MUTED")?match.label:
      (state==="PENDING"?"Pending · ":"Needs Remap · ")+
       "OUT "+(row.physicalLeft+1)+"/"+(row.physicalRight+1)):"None";
    const marker=state==="NONE"?"":state==="NEEDS_REMAP"?"REMAP":state;
    const title=state==="PENDING"?"Native PCM pending · wait for audio callback":
      state==="NEEDS_REMAP"?
      "Saved physical output disarmed; choose a verified ASIO pair":
      state==="MUTED"?"ASIO pair connected · output muted, settings retained":
      state==="LIVE"?"LIVE · independent native post-fader PCM":
      "None · internal WIRE remains unchanged";
    return {match,state,value,marker,title};
  }

  // One transient draft for the existing Device Setup dialog. All native
  // inventory and installed PCM remain external readback, never owned here.
  function setupSameEndpoint(active,draft) {
    return active?.open===true&&active.backend===draft?.backend&&
      active.captureDevice===draft.capture&&active.playbackDevice===draft.playback;
  }
  function setupDraft(inventory) {
    if(!inventory?.active||!Array.isArray(inventory.types))return null;
    const active=inventory.active,types=inventory.types;
    const backend=active.backend||types[0]?.name||"";
    const type=types.find(t=>t.name===backend);
    const capture=active.open?String(active.captureDevice||""):
      active.captureDevice||type?.inputs?.[0]||"";
    return {backend,capture,
      playback:active.playbackDevice||
        (type?.separate?type?.outputs?.[0]:capture)||"",
      inputPins:Array.isArray(active.enabledInputs)?active.enabledInputs.slice():null,
      outputPins:Array.isArray(active.enabledOutputs)?active.enabledOutputs.slice():null,
      rate:active.open&&validRate(active.rate)?active.rate:0,
      buffer:active.open&&validBuffer(active.buffer)?active.buffer:0};
  }
  const validRate=v=>Number.isFinite(v)&&v>=8000&&v<=384000;
  const validBuffer=v=>Number.isSafeInteger(v)&&v>=16&&v<=8192;
  function formatOptions(reported,current,valid) {
    // Current negotiated value may be absent from advertised alternatives.
    const values=Array.isArray(reported)?reported.filter(valid):[];
    if(valid(current))values.push(current);
    return ["0",...[...new Set(values)].sort((a,b)=>a-b).map(String)];
  }
  function setupBufferPeriod(rate,buffer) {
    return validRate(rate)&&validBuffer(buffer)?1000*buffer/rate:null;
  }
  function setupHealthReadback(active) {
    if(active?.open!==true||active.authorized!==true)return null;
    const count=v=>Number.isSafeInteger(v)&&v>=0?v:null;
    const samples=count(active.callbackBudgetSamples);
    const p99=Number.isFinite(active.callbackP99BudgetUpperPct)&&
      active.callbackP99BudgetUpperPct>=0?active.callbackP99BudgetUpperPct:null;
    const budget=samples>0?(active.callbackP99BeyondDeadline===true?
      "over deadline":p99===null?"N/A":"≤"+p99.toFixed(0)+"%"):"N/A";
    const xruns=active.backendXrunMeaning==="DRIVER_DEFINED"?
      count(active.backendXrunCount):null;
    const discontinuities=active.backendXrunMeaning==="WASAPI_CAPTURE_DISCONTINUITIES_ONLY"?
      count(active.backendXrunCount):null;
    return {budget,overBudget:count(active.overBudgetCount),
      gaps:count(active.callbackGapCount),xruns,discontinuities,samples};
  }
  // DAW-style read-only latency from the ACTUAL active JUCE device.
  // Driver input/output hints are not an electrical loopback measurement.
  // Unavailable or independently clocked paths are NEVER converted to 0 ms.
  function setupLatencyReadback(active) {
    const rate=Number(active?.rate);
    if(active?.open!==true || active.authorized===false || !Number.isFinite(rate)||rate<=0)
      return Object.freeze({ready:false});
    const frames=value=>Number.isSafeInteger(value)&&value>=0?value:null;
    const input=frames(active.driverInputLatencyFrames);
    const output=frames(active.driverOutputLatencyFrames);
    const fx=frames(active.hostFxLatencyFrames);
    const buffer=frames(active.buffer);
    const sameClock=active.latencyClockScope==="SINGLE_CLOCK_DRIVER_HINTS";
    const driverIo=sameClock&&input!==null&&output!==null?input+output:null;
    // Driver I/O reports may already include buffering. The callback period
    // is a separate metric, not an extra term in the estimate.
    const total=driverIo!==null&&fx!==null?driverIo+fx:null;
    const ms=value=>value===null?null:1000*value/rate;
    const fmt=value=>value===null?"N/A":ms(value).toFixed(2)+" ms";
    return Object.freeze({
      ready:true,inputMs:ms(input),outputMs:ms(output),fxMs:ms(fx),bufferMs:ms(buffer),
      estimatedTotalMs:ms(total),driverIoHintMs:ms(driverIo),
      line:"Input "+fmt(input)+" · Output "+fmt(output)+
        " · Buffer "+fmt(buffer)+" · FX/PDC "+fmt(fx),
      note:"Estimated total I/O "+fmt(total)+" · JUCE driver + host FX/PDC",
      provenance:active.latencyProvenance||"UNKNOWN"
    });
  }
  // Capability inventory is a driver observation, not installed PCM.
  // Never synthesize pins from a product name, Windows endpoint or count.
  function pinInventory(active,side) {
    const input=side==="input";
    const names=active?.[input?"inputNames":"outputNames"];
    const enabled=active?.[input?"enabledInputs":"enabledOutputs"];
    const count=active?.[input?"reportedInputCount":"reportedOutputCount"];
    if(active?.open!==true||!Array.isArray(names)||!validPinMask(enabled,names))
      return null;
    const total=count===undefined?names.length:count;
    if(!Number.isSafeInteger(total)||total<names.length||names.length>64||
       !names.every(n=>typeof n==="string"))return null;
    return {total,active:enabled.length,selectable:names.length,
      complete:total===names.length,
      all:names.map((_,i)=>i)};
  }
  function setupSelectAllPins(draft,inventory,side) {
    if(!["input","output"].includes(side)||
       !setupSameEndpoint(inventory?.active,draft)||draft.backend!=="ASIO")return draft;
    const pins=pinInventory(inventory.active,side);
    if(!pins?.complete||!pins.all.length||side==="output"&&pins.all.length<2)
      return draft;
    return {...draft,[side==="input"?"inputPins":"outputPins"]:pins.all};
  }
  function setupPresentation(inventory,draft,state=setupInitial()) {
    const active=inventory?.active||{},types=inventory?.types||[];
    const type=types.find(t=>t?.name===draft?.backend);
    const same=setupSameEndpoint(active,draft);
    const inputOptions=type?.separate?["",...(type.inputs||[])]:type?.inputs||[];
    const outputOptions=type?.outputs||[];
    const rateOptions=same?formatOptions(active.rates,active.rate,validRate):["0"];
    const bufferOptions=same?formatOptions(active.buffers,active.buffer,validBuffer):["0"];
    const rateValid=rateOptions.includes(String(draft?.rate??0));
    const bufferValid=bufferOptions.includes(String(draft?.buffer??0));
    const mainPins=Array.isArray(active.mainOutputs)?active.mainOutputs
      .map(pin=>active.outputNames?.[pin]).filter(Boolean):[];
    const noInput=type?.separate===true&&draft?.capture==="";
    const inputValid=noInput||Boolean(type?.inputs?.includes(draft?.capture));
    const outputValid=Boolean(outputOptions.includes(draft?.playback));
    const selectedInputPins=Array.isArray(draft?.inputPins)?
      draft.inputPins:Array.isArray(active.enabledInputs)?active.enabledInputs:[];
    const selectedOutputPins=Array.isArray(draft?.outputPins)?
      draft.outputPins:Array.isArray(active.enabledOutputs)?active.enabledOutputs:[];
    const validSelection=!same ||
      (validPinMask(selectedInputPins,active.inputNames)&&
       validPinMask(selectedOutputPins,active.outputNames)&&
       selectedOutputPins.length>=2&&
       (!noInput||selectedInputPins.length===0));
    const canApply=Boolean(type && draft?.backend && inputValid&&outputValid&&
      (type.separate===true||draft.capture===draft.playback)&&
      validSelection&&rateValid&&bufferValid);
    const selectedPins=(key,enabled)=>Array.isArray(draft?.[key])?
      draft[key]:Array.isArray(enabled)?enabled:[];
    return {type,same,backendOptions:types.map(t=>t.name),
      inputOptions,outputOptions,rateOptions,bufferOptions,
      draftPeriodMs:same?setupBufferPeriod(draft.rate,draft.buffer):null,
      driverPanelAvailable:same&&active.backend==="ASIO"&&active.hasControlPanel===true,
      inputPins:selectedPins("inputPins",active.enabledInputs),
      outputPins:selectedPins("outputPins",active.enabledOutputs),
      inputInventory:same?pinInventory(active,"input"):null,
      outputInventory:same?pinInventory(active,"output"):null,
      canApply,canAssignMain:same&&validSelection&&
        selectedOutputPins.length===2,
      canReusePins:same&&state.remapGeneration===active.deviceGeneration,
      asio4allHint:draft?.backend==="ASIO"&&/asio4all/i.test(draft?.capture||""),
      activeText:state.message||(active.open?
        "ACTIVE · "+active.backend+" · "+active.rate+" Hz / "+active.buffer+" samples"+
        (mainPins.length===2?" · MAIN "+mainPins.join(" / "):""):
        "No verified audio device. Select a supported input and output.")};
  }
  function setupEditDraft(draft,inventory,change) {
    if(!draft||!inventory?.active||!change||
       typeof change!=="object")return draft;
    const view=setupPresentation(inventory,draft);
    if(typeof change.side==="string") {
      if(!view.same||!["input","output"].includes(change.side)||
         !Number.isInteger(change.index)||change.index<0||change.index>=64||
         typeof change.checked!=="boolean")return draft;
      const key=change.side==="input"?"inputPins":"outputPins";
      const names=inventory.active[change.side==="input"?"inputNames":"outputNames"];
      if(!Array.isArray(names)||change.index>=names.length)return draft;
      const selected=new Set(view[key]);
      if(change.checked)selected.add(change.index);
      else selected.delete(change.index);
      return {...draft,[key]:[...selected].sort((a,b)=>a-b)};
    }
    const {field,value}=change;
    if(typeof value!=="string")return draft;
    if(field==="backend") {
      const type=inventory.types?.find(t=>t.name===value);
      if(!type)return draft;
      const capture=type.inputs?.[0]||"";
      return {...draft,backend:value,capture,
        playback:type.separate?type.outputs?.[0]||"":capture,
        inputPins:null,outputPins:null,rate:0,buffer:0};
    }
    if(field==="capture") {
      if(!view.inputOptions.includes(value))return draft;
      return {...draft,capture:value,
        playback:view.type?.separate?draft.playback:value,
        inputPins:null,outputPins:null,rate:0,buffer:0};
    }
    if(field==="playback") {
      if(!view.outputOptions.includes(value)||
         (view.type?.separate!==true&&!view.inputOptions.includes(value)))
        return draft;
      // A JUCE non-separate backend has one duplex endpoint identity.
      // Never present an input/output mismatch that native must reject.
      return {...draft,playback:value,
        capture:view.type?.separate?draft.capture:value,
        inputPins:null,outputPins:null,rate:0,buffer:0};
    }
    if(field==="rate"||field==="buffer") {
      const options=field==="rate"?view.rateOptions:view.bufferOptions;
      if(!view.same||!options.includes(value))return draft;
      return {...draft,[field]:Number(value)||0};
    }
    return draft;
  }

  // Device Setup UI transaction: pure presentation only. The native
  // coordinator remains the sole device apply/rollback and output authority.
  // Never treat a successful ACK as installed hardware readback.
  function setupInitial() {
    return {phase:"IDLE",pending:null,message:"",remapGeneration:null};
  }
  function setupBegin(state,{inventory,draft,requestId,baselineEpoch=null,
                             confirmRetainedPinMapping=false,assignMain=false}) {
    if(state.pending||!inventory?.active||!draft||
       typeof requestId!=="string"||!requestId||requestId.length>64)return null;
    const active=inventory.active;
    const generation=active.deviceGeneration;
    if(!Number.isSafeInteger(generation)||generation<0)return null;
    const presentation=setupPresentation(inventory,draft,state);
    if(!presentation.canApply)return null;
    const same=presentation.same;
    if(confirmRetainedPinMapping &&
       (!same||state.remapGeneration!==generation))return null;
    if(assignMain&&(!same||draft.outputPins?.length!==2))return null;
    const rate=confirmRetainedPinMapping?active.rate:same?draft.rate:0;
    const buffer=confirmRetainedPinMapping?active.buffer:same?draft.buffer:0;
    const request={action:"audioDeviceApply",requestId,
      expectedDeviceGeneration:generation,
      config:{backend:draft.backend,captureDevice:draft.capture,
        playbackDevice:draft.playback,sampleRate:rate,bufferSize:buffer,
        enabledInputs:same?presentation.inputPins:[],
        enabledOutputs:same?presentation.outputPins:[],
        ...(confirmRetainedPinMapping?{confirmRetainedPinMapping:true}:{}),
        ...(assignMain?{assignSelectedPlaybackPinsToMain:true}:{})}};
    const pending={requestId,expected:{backend:draft.backend,
      captureDevice:draft.capture,playbackDevice:draft.playback,
      generation,rate,buffer,
      // An existing endpoint has an explicitly requested pin mask.
      // A NEW endpoint's empty masks request JUCE driver defaults, not NONE.
      requestedPins:same?{
        inputs:request.config.enabledInputs.slice(),
        outputs:request.config.enabledOutputs.slice()}:null},baselineEpoch:
      typeof baselineEpoch==="string"&&/^(0|[1-9]\d*)$/.test(baselineEpoch)?
        baselineEpoch:null,ack:null};
    return {request,state:{phase:"APPLYING",pending,message:"",
      remapGeneration:null}};
  }
  function setupAck(state,ack) {
    // G-MV21: first ACK wins while APPLYING. After a matching hardware
    // acceptance, the immutable ACK lease is verified only by an installed
    // v6 readback, never replaced by duplicate positive/negative ACKs.
    if(state.phase!=="APPLYING"||!state.pending||
       state.pending.ack||ack?.requestId!==state.pending.requestId)return state;
    const active=ack.active;
    const accepted=ack.ok===true &&
      (ack.state==="ACCEPTED_LIVE"||ack.state==="ACCEPTED_MUTED") &&
      active?.open===true&&active.authorized===true&&
      active.backend===state.pending.expected.backend&&
      active.captureDevice===state.pending.expected.captureDevice&&
      active.playbackDevice===state.pending.expected.playbackDevice&&
      Number.isSafeInteger(active.deviceGeneration)&&
      active.deviceGeneration>=state.pending.expected.generation&&
      (state.pending.expected.rate===0||active.rate===state.pending.expected.rate)&&
      (state.pending.expected.buffer===0||active.buffer===state.pending.expected.buffer)&&
      Array.isArray(active.enabledInputs)&&Array.isArray(active.enabledOutputs)&&
      active.enabledOutputs.length>=2&&
      Number.isFinite(active.rate)&&active.rate>0&&
      Number.isInteger(active.buffer)&&active.buffer>0;
    // A driver can open yet return a *different* physical mask from what the
    // operator requested. Do not close the dialog or show a false success.
    // Native may already have changed hardware, so do NOT claim rollback.
    const requested=state.pending.expected.requestedPins;
    if(accepted && requested &&
       (!samePinMask(requested.inputs,active.enabledInputs)||
        !samePinMask(requested.outputs,active.enabledOutputs)))
      return {phase:"RECOVERY_REQUIRED",pending:null,remapGeneration:null,
        message:"Applied device pins differ from selection · Refresh Audio I/O and verify routes"};
    if(accepted && (!validPinMask(active.enabledInputs)||
                    !validPinMask(active.enabledOutputs)))
      return {phase:"RECOVERY_REQUIRED",pending:null,remapGeneration:null,
        message:"Device pin readback invalid · Refresh Audio I/O and verify routes"};
    if(accepted)return {...state,phase:"VERIFYING",
      pending:{...state.pending,ack:{
        active:{backend:active.backend,captureDevice:active.captureDevice,
          playbackDevice:active.playbackDevice,rate:active.rate,
          buffer:active.buffer,enabledInputs:active.enabledInputs.slice(),
          enabledOutputs:active.enabledOutputs.slice(),
          deviceGeneration:active.deviceGeneration},
        mixerBound:ack.mixerBound===true,outputMuted:ack.outputMuted===true}}};
    const remap=ack.state==="NEEDS_REMAP"&&ack.canReusePins===true&&
      active?.open===true&&active?.authorized===true&&
      Number.isSafeInteger(active.deviceGeneration)?
        active.deviceGeneration:null;
    return {phase:ack.state==="RECOVERY_REQUIRED"?"RECOVERY_REQUIRED":"REJECTED",
      pending:null,remapGeneration:remap,
      message:ack.state==="NEEDS_REMAP"?
        String(ack.error||"Playback unavailable").replace(/^NEEDS_REMAP:\s*/,""):
        ack.state==="RESTORED_LIVE"||ack.state==="RESTORED_MUTED"?
          "New device rejected · Previous device restored":
          "Device rejected · "+String(ack.error||"native hardware readback failed")};
  }
  function setupReadback(state,snapshot) {
    if(state.phase!=="VERIFYING"||!state.pending?.ack)return state;
    const d=snapshot?.device,expected=state.pending.ack.active;
    const epoch=d?.epoch;
    if(typeof epoch!=="string"||!/^(0|[1-9]\d*)$/.test(epoch))return state;
    const baseline=state.pending.baselineEpoch;
    // Old epoch and disconnected/replaced devices may never qualify an ACK.
    if(baseline!==null&&BigInt(epoch)<BigInt(baseline))
      return {phase:"STALE",pending:null,remapGeneration:null,
        message:"Device changed · Refresh Audio I/O before applying again"};
    if(snapshot?.retained===true ||
       (d?.sampleRate<=0&&baseline!==null&&BigInt(epoch)>BigInt(baseline)))
      return {phase:"RECOVERY_REQUIRED",pending:null,remapGeneration:null,
        message:"Audio device unavailable · Previous mixer settings retained"};
    // Route epoch can advance AHEAD of the JUCE generation on STOP/START,
    // but an old hardware snapshot cannot verify a later accepted Apply.
    if(BigInt(epoch)<BigInt(expected.deviceGeneration))return state;
    if(snapshot?.raw?.schemaVersion!==6||!Array.isArray(d?.enabledInputs)||
       !Array.isArray(d.enabledOutputs))return state;
    // Endpoint lists are physical sets, not ordered callback-channel arrays.
    // Require exact installed pins while tolerating a harmless reorder.
    const same=d.backend===expected.backend&&
      d.captureDevice===expected.captureDevice&&
      d.playbackDevice===expected.playbackDevice&&
      d.sampleRate===expected.rate&&d.bufferFrames===expected.buffer&&
      samePinMask(d.enabledInputs,expected.enabledInputs)&&
      samePinMask(d.enabledOutputs,expected.enabledOutputs);
    if(!same)return state; // The driver or graph may still be publishing.
    // Physical device Apply is a distinct transaction from ChannelId PCM
    // admission. Legacy M4/G0 can have a verified open driver without an
    // admitted ChannelId graph; never trap Audio I/O behind a PCM claim.
    const mixerVerified=snapshot.admitted===true;
    const live=state.pending.ack.outputMuted===false&&
      snapshot.admitted===true&&snapshot.productLive===true&&
      d.outputArmed===true;
    return {phase:"ACCEPTED",pending:null,remapGeneration:null,
      message:live?"Mixer ready · Output live":
        mixerVerified?"Mixer ready · Output muted":
          "Audio device ready · Mixer route not yet verified"};
  }
  function setupTimeout(state,requestId) {
    if(!state.pending||state.pending.requestId!==requestId)return state;
    return {phase:"TIMEOUT",pending:null,remapGeneration:null,
      message:"Device verification timed out · Refresh Audio I/O; previous settings not confirmed"};
  }
  function setupClearResult(state) {
    if(state.pending)return state;
    return {...state,phase:"IDLE",message:"",remapGeneration:null};
  }

  // G-MV20: native schema-v6 normalizer publishes the *single* canonical
  // installed pin/format fingerprint. DeviceVM consumes that read-only token;
  // it must not independently calculate a competing physical identity.
  // MV-12: one pure identity fence for transient WebView control requests.
  // This is NOT a physical device manager or second native controller. A
  // ChannelId graph can be readmitted directly after a device switch without
  // ever publishing a retained/unavailable snapshot: UI requests must still
  // lose their old lease. Graph revisions do NOT identify a new device.
  function nativeLifecycleLease(snapshot,readModelKind) {
    const d=snapshot?.device,epoch=d?.epoch;
    const kinds=["CHANNEL_ID","M4","NEEDS_REMAP","LOGICAL_ONLY","RETAINED_OFFLINE",
      "UNADMITTED","STALE"];
    const hardwareConfiguration=snapshot?.hardwareConfiguration;
    if(!kinds.includes(readModelKind) ||
       typeof epoch!=="string" || !/^(0|[1-9]\d*)$/.test(epoch) ||
       epoch.length>20 || BigInt(epoch)>18446744073709551615n ||
       typeof d.backend!=="string" ||
       typeof d.captureDevice!=="string" ||
       typeof d.playbackDevice!=="string" ||
       typeof hardwareConfiguration!=="string"||
       !hardwareConfiguration)return null;
    // Store only physical identity/format and canonical read-model authority.
    // Never copy a graph, mutable pin array, DOM, PCM or device controller.
    return Object.freeze({epoch,backend:d.backend,
      captureDevice:d.captureDevice,playbackDevice:d.playbackDevice,
      hardwareConfiguration,authority:readModelKind});
  }
  // G-MV24: pure readback scheduling/quarantine policy. The accepted native
  // read-model is the only authority; old SOURCE timers and microtasks may
  // never be scheduled from a retained or stale PCM snapshot.
  function nativeTransientReadbackPolicy(kind) {
    return Object.freeze({
      discardRetained:kind==="NEEDS_REMAP"||kind==="RETAINED_OFFLINE",
      drainM4Sources:kind==="M4"||kind==="UNADMITTED",
      drainNativeSources:kind==="CHANNEL_ID"
    });
  }
  function nativeLifecycleDecision(previous,next) {
    if(!next || typeof next.epoch!=="string" ||
       typeof next.authority!=="string")
      return Object.freeze({kind:"INVALID",expirePending:false});
    if(!previous)
      return Object.freeze({kind:"INITIAL",expirePending:false});
    const changed=previous.epoch!==next.epoch ||
      previous.backend!==next.backend ||
      previous.captureDevice!==next.captureDevice ||
      previous.playbackDevice!==next.playbackDevice ||
      previous.hardwareConfiguration!==next.hardwareConfiguration;
    if(changed)
      return Object.freeze({kind:"DEVICE_CHANGED",expirePending:true});
    // The very first accepted M4 physical SOURCE patch promotes an already
    // opened, unchanged device from UNADMITTED to M4. It is the SAME native
    // operation that the in-flight M4 input queue is waiting to verify:
    // discarding the queue here would silently lose follow-up source choices.
    // Any endpoint/epoch change above still invalidates all old gestures.
    if(previous.authority==="UNADMITTED"&&next.authority==="M4")
      return Object.freeze({kind:"M4_FIRST_SOUND",expirePending:false});
    if(previous.authority!==next.authority)
      return Object.freeze({kind:"AUTHORITY_CHANGED",expirePending:true});
    return Object.freeze({kind:"SAME",expirePending:false});
  }
  return Object.freeze({activeInputPins,inputChoices,inputLabel,outputChoices,
    externalPresentation,setupDraft,setupPresentation,setupLatencyReadback,setupEditDraft,
    pinInventory,setupSelectAllPins,setupBufferPeriod,setupHealthReadback,
    setupInitial,setupBegin,setupAck,setupReadback,
    setupTimeout,setupClearResult,nativeLifecycleLease,nativeLifecycleDecision,
    nativeTransientReadbackPolicy});
});
