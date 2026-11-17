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
        entries.push({label:"Stereo · IN "+(pins[i].port+1)+
          " + "+(pins[i+1].port+1),mode:"STEREO",
          left:pins[i].port,right:pins[i+1].port});
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

  // One disposable draft for the current native hardware inventory.
  // No selectors own a real device, a PCM graph, or persistent .smix state.
  function setupType(inventory,backend) {
    return Array.isArray(inventory?.types)?
      inventory.types.find(t=>t?.name===backend):undefined;
  }
  function setupValidSelection(inventory,draft) {
    const t=setupType(inventory,draft?.backend);
    if(!inventory?.active||!t||!draft?.playback||
       !Array.isArray(t.outputs)||!t.outputs.includes(draft.playback))
      return false;
    // Existing hardware pin masks must remain a verified stereo pair.
    // A new endpoint has no inspected pins yet; native opens it with safe
    // defaults and publishes a fresh inventory before individual pin edits.
    if(setupSameEndpoint(inventory,draft)&&Array.isArray(draft.outputPins)&&
       draft.outputPins.length<2)return false;
    if(t.separate===true)
      return draft.capture==="" ||
        (Array.isArray(t.inputs)&&t.inputs.includes(draft.capture));
    return Array.isArray(t.inputs)&&t.inputs.includes(draft.capture)&&
      draft.capture===draft.playback;
  }
  function setupSameEndpoint(inventory,draft) {
    const active=inventory?.active;
    return active?.open===true&&
      active.backend===draft?.backend&&
      active.captureDevice===draft?.capture&&
      active.playbackDevice===draft?.playback;
  }
  function setupInitialDraft(inventory) {
    if(!inventory?.active)return null;
    const active=inventory.active;
    const backend=active.backend||inventory.types?.[0]?.name||"";
    const type=setupType(inventory,backend);
    // Keep a verified output-only endpoint output-only on reopen.
    const capture=active.open===true?String(active.captureDevice||""):
      active.captureDevice||type?.inputs?.[0]||"";
    return {backend,capture,
      playback:active.playbackDevice||
        (type?.separate?type?.outputs?.[0]:capture)||"",
      inputPins:Array.isArray(active.enabledInputs)?active.enabledInputs.slice():null,
      outputPins:Array.isArray(active.enabledOutputs)?active.enabledOutputs.slice():null,
      rate:0,buffer:0};
  }
  function setupEditDraft(inventory,draft,edit) {
    if(!draft||!edit||!inventory?.active)return draft;
    const t=setupType(inventory,draft.backend);
    if(edit.pinSide==="input"||edit.pinSide==="output"){
      if(!setupSameEndpoint(inventory,draft))return draft;
      const input=edit.pinSide==="input";
      const key=input?"inputPins":"outputPins";
      const names=input?inventory.active.inputNames:inventory.active.outputNames;
      const index=edit.pinIndex;
      if(!Array.isArray(names)||!Array.isArray(draft[key])||
         !Number.isSafeInteger(index)||index<0||index>=names.length||
         typeof edit.checked!=="boolean")return draft;
      const old=draft[key],has=old.includes(index);
      if(has===edit.checked)return draft;
      const pins=edit.checked?[...old,index]:old.filter(n=>n!==index);
      return {...draft,[key]:[...new Set(pins)].sort((a,b)=>a-b)};
    }
    const field=edit.field,value=edit.value;
    if(typeof value!=="string")return draft;
    if(field==="backend"){
      if(value===draft.backend)return draft;
      const nextType=setupType(inventory,value);
      if(!nextType)return draft;
      const capture=nextType.inputs?.[0]||"";
      return {...draft,backend:value,capture,
        playback:nextType.separate===true?nextType.outputs?.[0]||"":capture,
        inputPins:null,outputPins:null,rate:0,buffer:0};
    }
    if(!t)return draft;
    if(field==="capture"){
      if(value===draft.capture)return draft;
      if(value!==""&&!t.inputs?.includes(value)||
         value===""&&t.separate!==true)return draft;
      return {...draft,capture:value,
        playback:t.separate===true?draft.playback:value,
        inputPins:null,outputPins:null,rate:0,buffer:0};
    }
    if(field==="playback"){
      if(value===draft.playback)return draft;
      if(!t.outputs?.includes(value)||
         t.separate!==true&&!t.inputs?.includes(value))return draft;
      // A combined (non-separate) JUCE driver has ONE endpoint identity;
      // changing Output must follow it on Input or Apply becomes impossible.
      return {...draft,playback:value,
        capture:t.separate===true?draft.capture:value,
        inputPins:null,outputPins:null,rate:0,buffer:0};
    }
    if(field==="rate"||field==="buffer"){
      const number=Number(value);
      const choices=field==="rate"?inventory.active.rates:inventory.active.buffers;
      if(!setupSameEndpoint(inventory,draft)||!Number.isSafeInteger(number)||
         number<0||(number!==0&&
           (!Array.isArray(choices)||!choices.includes(number))))return draft;
      if(draft[field]===number)return draft;
      return {...draft,[field]:number};
    }
    return draft;
  }
  function setupUiModel(inventory,draft,state) {
    const type=setupType(inventory,draft?.backend);
    const same=setupSameEndpoint(inventory,draft);
    const active=inventory?.active||{};
    const busy=!!state?.pending;
    const mainPins=Array.isArray(active.mainOutputs)?active.mainOutputs
      .map(pin=>active.outputNames?.[pin]).filter(Boolean):[];
    // Device status is presentation only. Hardware approval remains native.
    const activeText=state?.message||(active.open?
      "ACTIVE · "+active.backend+" · "+active.rate+" Hz / "+active.buffer+" samples"+
      (mainPins.length===2?" · MAIN "+mainPins.join(" / "):""):
      "No verified audio device. Select a supported input and output.");
    const selectedPins=(draftPins,enabled)=>Array.isArray(draftPins)?
      draftPins:Array.isArray(enabled)?enabled:[];
    return {
      activeText,
      selectedInputPins:selectedPins(draft?.inputPins,active.enabledInputs),
      selectedOutputPins:selectedPins(draft?.outputPins,active.enabledOutputs),
      backendChoices:(inventory?.types||[]).map(t=>t.name),
      captureChoices:type?.separate===true?["",...(type.inputs||[])]:
        type?.inputs||[],
      playbackChoices:type?.outputs||[],
      rateChoices:["0",...(same?active.rates||[]:[]).map(String)],
      bufferChoices:["0",...(same?active.buffers||[]:[]).map(String)],
      same,busy,
      asio4allHint:draft?.backend==="ASIO"&&
        /asio4all/i.test(draft.capture||""),
      canApply:!busy&&setupValidSelection(inventory,draft),
      canAssignMain:!busy&&same&&draft?.outputPins?.length===2,
      canRemap:!busy&&same&&state?.remapGeneration!==null&&
        state?.remapGeneration===active.deviceGeneration
    };
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
    if(!setupValidSelection(inventory,draft))return null;
    const same=setupSameEndpoint(inventory,draft);
    if(confirmRetainedPinMapping &&
       (!same||state.remapGeneration!==generation))return null;
    if(assignMain&&(!same||draft.outputPins?.length!==2))return null;
    const rate=confirmRetainedPinMapping?active.rate:same?draft.rate:0;
    const buffer=confirmRetainedPinMapping?active.buffer:same?draft.buffer:0;
    const request={action:"audioDeviceApply",requestId,
      expectedDeviceGeneration:generation,
      config:{backend:draft.backend,captureDevice:draft.capture,
        playbackDevice:draft.playback,sampleRate:rate,bufferSize:buffer,
        enabledInputs:same?draft.inputPins:[],
        enabledOutputs:same?draft.outputPins:[],
        ...(confirmRetainedPinMapping?{confirmRetainedPinMapping:true}:{}),
        ...(assignMain?{assignSelectedPlaybackPinsToMain:true}:{})}};
    const pending={requestId,expected:{backend:draft.backend,
      captureDevice:draft.capture,playbackDevice:draft.playback,
      generation,rate,buffer},baselineEpoch:
      typeof baselineEpoch==="string"&&/^(0|[1-9]\d*)$/.test(baselineEpoch)?
        baselineEpoch:null,ack:null};
    return {request,state:{phase:"APPLYING",pending,message:"",
      remapGeneration:null}};
  }
  function setupAck(state,ack) {
    if(!state.pending||ack?.requestId!==state.pending.requestId)return state;
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
    const equalPins=(a,b)=>Array.isArray(a)&&Array.isArray(b)&&
      a.length===b.length&&a.every((n,i)=>n===b[i]);
    const same=d.backend===expected.backend&&
      d.captureDevice===expected.captureDevice&&
      d.playbackDevice===expected.playbackDevice&&
      d.sampleRate===expected.rate&&d.bufferFrames===expected.buffer&&
      equalPins(d.enabledInputs,expected.enabledInputs)&&
      equalPins(d.enabledOutputs,expected.enabledOutputs);
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
  return Object.freeze({activeInputPins,inputChoices,inputLabel,outputChoices,
    externalPresentation,setupInitialDraft,setupEditDraft,setupUiModel,
    setupInitial,setupBegin,setupAck,setupReadback,
    setupTimeout,setupClearResult});
});
