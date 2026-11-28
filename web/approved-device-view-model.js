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
  // Canonical single physical selection; never materialize all N*(N-1)
  // stereo permutations. The custom editor and installed source projection
  // both use this helper; actual native epoch and PCM authority stay separate.
  function physicalInputChoice(snapshot,mode,left,right){
    if(mode==="NONE"&&left===-1&&right===-1)return {label:"None",mode,left,right};
    if(!Number.isInteger(left)||!Number.isInteger(right))return null;
    const pins=activeInputPins(snapshot);
    return physicalInputChoiceFromPins(pins.find(p=>p.port===left),
      pins.find(p=>p.port===right),mode,left,right);
  }
  function physicalInputChoiceFromPins(a,b,mode,left,right){
    if(!a)return null;
    if(mode==="MONO"&&right===-1)return {
      label:"Mono · "+a.label+" [IN "+(left+1)+"]",mode,left,right};
    if(mode!=="STEREO"||!b||left===right)return null;
    const generic=p=>new RegExp("^\\s*(?:in|input)\\s*"+(p.port+1)+"\\s*$","i")
      .test(String(p.label||""));
    const pair="Stereo · IN "+(left+1)+" + "+(right+1);
    return {label:generic(a)&&generic(b)?pair:pair+" · "+a.label+" / "+b.label,
      mode,left,right};
  }
  function inputChoices(snapshot) {
    const pins=activeInputPins(snapshot).slice().sort((a,b)=>a.port-b.port);
    // One call-local index of this accepted inventory; never cache across
    // native snapshots, device epochs or remaps. Preserve first match.
    const pinByPort=new Map();
    for(const pin of pins)if(!pinByPort.has(pin.port))pinByPort.set(pin.port,pin);
    const entries=[{label:"None",mode:"NONE",left:-1,right:-1}];
    const keys=new Set(["NONE:-1:-1"]);
    const add=(mode,left,right)=>{
      const key=mode+":"+left+":"+right;
      if(keys.has(key))return;
      if(!Number.isInteger(left)||!Number.isInteger(right))return;
      const choice=physicalInputChoiceFromPins(pinByPort.get(left),pinByPort.get(right),mode,left,right);
      if(choice){entries.push(choice);keys.add(key);}
    };
    for(const p of pins)add("MONO",p.port,-1);
    for(let i=0;i+1<pins.length;++i)
      if(pins[i+1].port===pins[i].port+1)add("STEREO",pins[i].port,pins[i+1].port);
    // Keep custom installed pairs selectable and correctly labelled after
    // ACK, restart or mixer projection; no phantom option on missing pins.
    if(snapshot?.retained!==true&&snapshot?.admitted===true)
      for(const ch of snapshot.graph?.channels||[]){
        const src=ch.source;
        if(src?.sourceFormat!=="STEREO"||src.bindingState==="NEEDS_REMAP")continue;
        const l=/^in:(\d+)$/.exec(src.portId||""),r=/^in:(\d+)$/.exec(src.portIdRight||"");
        if(l&&r)add("STEREO",Number(l[1]),Number(r[1]));
      }
    if(snapshot?.retained!==true&&snapshot?.raw?.mixerSession?.authority==="M4")
      for(const ch of snapshot.raw.mixerSession.channels||[]){
        const src=ch.source;
        if(src?.mode==="STEREO")add("STEREO",src.left,src.right);
      }
    return entries;
  }
  // Physical crosspoints are a projection of installed ChannelId PCM only.
  // The existing source and external-output planners remain command authority.
  function busOutputChoices(snapshot,key){
    const d=snapshot?.device,raw=snapshot?.raw;
    if(!snapshot?.admitted||snapshot.retained||d?.backend!=="ASIO"||
       raw?.physicalBusPatchSupported!==true||
       !["physicalMainOutputs","physicalMonitorOutputs"].includes(key)||
       !Array.isArray(raw.physicalMainOutputs)||raw.physicalMainOutputs.length!==2||
       !Array.isArray(raw.physicalMonitorOutputs)||!Array.isArray(raw.externalOutputs))return [];
    const own=raw[key],other=raw[key==="physicalMainOutputs"?"physicalMonitorOutputs":"physicalMainOutputs"];
    if(own.length!==2)return [];
    const pins=new Map(d.outputEndpoints.filter(p=>p.active===true&&d.enabledOutputs.includes(p.port)&&p.label).map(p=>[p.port,p.label]));
    const reserved=new Set([...other,...raw.externalOutputs.flatMap(p=>[p.physicalLeft,p.physicalRight]),
      ...logicalReservedPins(snapshot)]);
    const make=(left,right)=>({label:"OUT "+(left+1)+"/"+(right+1)+" · "+pins.get(left)+" + "+pins.get(right),left,right});
    if(!own.every(p=>pins.has(p))||own[0]===own[1])return [];
    const result=[make(own[0],own[1])];
    for(const [left] of pins){
      const right=left+1;
      if(left%2||!pins.has(right)||reserved.has(left)||reserved.has(right)||
         (left===own[0]&&right===own[1]))continue;
      result.push(make(left,right));
    }
    return result;
  }
  function matrixCrosspointChoice(row,candidates,port,side){
    if(!row?.editable||!Array.isArray(candidates)||!Number.isInteger(port))return null;
    const installed=candidates.find(x=>x.label===row.selected);
    const hits=x=>x.left===port||x.right===port;
    if(installed&&hits(installed)){
      if(row.bus)return null; // MAIN / admitted physical monitor cannot be removed here
      return candidates.find(x=>x.left===-1&&x.right===-1)||null;
    }
    const stereo=side==="output"||installed?.mode==="STEREO"||
      ((!installed||installed.mode==="NONE")&&row.format==="STEREO");
    return candidates.find(x=>(side==="output"||x.mode===(stereo?"STEREO":"MONO"))&&hits(x))||null;
  }
  function patchMatrix(snapshot,side) {
    const input=side==="input", d=snapshot?.device;
    const names=input?d?.inputEndpoints:d?.outputEndpoints;
    const enabled=input?d?.enabledInputs:d?.enabledOutputs;
    const pins=Array.isArray(names)&&validPinMask(enabled)?
      names.filter(p=>p?.active===true&&enabled.includes(p.port))
        .map(p=>({port:p.port,label:String(p.label||"")})).sort((a,b)=>a.port-b.port):[];
    const live=snapshot?.admitted===true&&snapshot?.retained!==true&&
      snapshot?.raw?.mixerSession?.authority==="CHANNEL_ID"&&
      snapshot.raw.mixerSession.deviceEpoch===d?.epoch&&
      typeof snapshot.hardwareConfiguration==="string"&&!!snapshot.hardwareConfiguration;
    const session=snapshot?.raw?.mixerSession,m4=snapshot?.raw?.legacyM4;
    const legacy=input&&snapshot?.admitted===false&&snapshot?.retained!==true&&
      !!snapshot.hardwareConfiguration&&typeof d?.epoch==="string"&&
      /^[1-9][0-9]*$/.test(d.epoch)&&Number.isSafeInteger(Number(d.epoch))&&
      m4?.active===true&&Number.isSafeInteger(Number(m4.routingRevision))&&
      Number(m4.routingRevision)>0&&
      ((session?.authority==="NONE"&&m4.routeAccepted===false)||
       (session?.authority==="M4"&&session.deviceEpoch===d.epoch&&
        m4.routeAccepted===true&&session.routeRevision===m4.routingRevision&&
        Array.isArray(session.channels)&&session.channels.length===10));
    const choices=input?inputChoices(snapshot):outputChoices(snapshot);
    const installedChoices=input?choices:outputChoices(snapshot,true);
    const rows=[];
    const map=(left,right,mono=false)=>pins.map(p=>p.port===left?(mono?"M":"L"):p.port===right?"R":"");
    if(!input)for(const [key,name] of [["physicalMainOutputs","MAIN"],["physicalMonitorOutputs","MONITOR"]]){
      const pair=snapshot?.raw?.[key];
      if(Array.isArray(pair)&&pair.length===2){
        const busChoices=busOutputChoices(snapshot,key);
        const logical=snapshot.raw?.logicalOutputRegistry;
        const ref=logical?.references?.find?.(r=>r.role===name);
        const port=logical?.ports?.find?.(p=>p.id===ref?.portId);
        const editable=!port&&busChoices.length>1&&snapshot.raw.physicalBusOutputObserved===true;
        const pending=snapshot.raw.physicalBusPatchSupported===true&&
          snapshot.raw.physicalBusOutputObserved!==true;
        rows.push({id:key,name,bus:true,editable,pending,choices:busChoices,
          pair:pair.slice(),cells:map(pair[0],pair[1]),selected:busChoices[0]?.label||"",
          state:port?port.name+" · Logical output":pending?"Awaiting callback":editable?"Remap available · verified pair":
            !pair.every(p=>pins.some(q=>q.port===p))?"Needs remap":
            busChoices.length>1?"Awaiting verified routing":"Fixed bus"});
      }
    }
    if(!input&&live&&snapshot.raw.physicalBusPatchSupported===true&&
       Array.isArray(snapshot.raw.physicalMonitorOutputs)&&snapshot.raw.physicalMonitorOutputs.length===0)
      rows.push({id:"physicalMonitorOutputs",name:"MONITOR",bus:true,editable:false,pending:false,
        choices:[],pair:[],cells:pins.map(()=>""),selected:"",
        state:"No separate ASIO MON bus · see MON / STREAM endpoint"});
    if(!input&&live&&snapshot.raw.secondaryMainOnly===true&&
       snapshot.raw.physicalMainOutputs?.length===0&&snapshot.raw.physicalMonitorOutputs?.length===2)
      rows.push({id:"secondaryMainOnly",name:"MAIN",bus:true,editable:false,pending:false,
        choices:[],pair:[],cells:pins.map(()=>""),selected:"",
        state:snapshot.raw.physicalBusSecondaryArmed===true?"WASAPI STREAM · armed":"WASAPI STREAM · disarmed"});
    for(const ch of live&&Array.isArray(snapshot.graph?.channels)?snapshot.graph.channels:[]){
      if(input?ch.role!=="INPUT":!["INPUT","GROUP","FX_RETURN"].includes(ch.role))continue;
      if(input){
        const src=ch.source;
        const choice=!src?choices[0]:src.bindingState==="NEEDS_REMAP"?null:
          choices.find(x=>x.mode===src.sourceFormat&&src.portId==="in:"+x.left&&
            (x.mode==="MONO"?src.portIdRight===undefined:src.portIdRight==="in:"+x.right));
        const logical=snapshot.raw?.logicalInputRegistry;
        const ref=logical?.references?.find?.(r=>r.channelId===ch.id);
        const port=logical?.ports?.find?.(p=>p.id===ref?.portId);
        const state=port?port.name+(ref.state==="NEEDS_REMAP"?" · Needs remap":""):
          src?.bindingState==="NEEDS_REMAP"?"Needs remap":
          src?.sourceFormat==="PLAYER"?"Music Player":choice?"":"Other source";
        rows.push({id:ch.id,name:ch.name,editable:true,
          format:ch.processingFormat,selected:choice?.label||null,
          cells:choice?map(choice.left,choice.right,choice.mode==="MONO"):map(-1,-1),state});
      }else{
        const src=snapshot.raw.externalOutputs?.find?.(p=>p.channelId===ch.id);
        const choice=src?installedChoices.find(x=>x.left===src.physicalLeft&&x.right===src.physicalRight):choices[0];
        const logical=snapshot.raw?.logicalOutputRegistry;
        const ref=logical?.references?.find?.(r=>r.channelId===ch.id);
        const port=logical?.ports?.find?.(p=>p.id===ref?.portId);
        rows.push({id:ch.id,name:ch.name,editable:d.backend==="ASIO"&&!port,
          selected:choice?.label||null,cells:choice&&src?.status!=="NEEDS_REMAP"?map(choice.left,choice.right):map(-1,-1),
          state:port?port.name+(ref.state==="NEEDS_REMAP"?" · Needs remap":ref.state==="PENDING"?" · Pending":""):src&&!choice?"Needs remap":src?.status||""});
      }
    }
    if(legacy)for(let index=0;index<10;++index){
      const ch=session.authority==="M4"?session.channels[index]:null;
      if(ch&&ch.role!=="INPUT")continue;
      const src=ch?.source;
      const choice=!src?choices[0]:choices.find(x=>x.mode===src.mode&&
        x.left===src.left&&x.right===src.right);
      rows.push({id:"m4:"+index,name:ch?.name||"CH "+(index+1),
        editable:true,format:src?.channelFormat||"MONO",
        selected:choice?.label||null,
        cells:choice?map(choice.left,choice.right,choice.mode==="MONO"):map(-1,-1),
        state:choice?"":"Needs remap"});
    }
    return {live:live||legacy,authority:live?"CHANNEL_ID":legacy?"M4":"NONE",
      revision:live?snapshot.graph?.revision:legacy?String(m4.routingRevision):"",
      roster:live?snapshot.graph?.rosterRevision:"",
      pins,rows,choices};
  }
  function inputLabel(snapshot,row,choices) {
    if(!row||row.mode==="NONE")return "None";
    if(row.mode==="BUS")return INTERNAL_BUS;
    const found=(choices??inputChoices(snapshot)).find(x=>x.mode===row.mode &&
      x.left===row.left && x.right===row.right);
    return found?.label||"None";
  }
  // A resolver belongs to ONE synchronous presentation pass only. Lazy so
  // unpatched / BUS-only passes do not enumerate any physical choices.
  function inputLabelResolver(snapshot) {
    let choices;
    return row=>{
      if(!row||row.mode==="NONE"||row.mode==="BUS")return inputLabel(snapshot,row);
      return inputLabel(snapshot,row,choices??=inputChoices(snapshot));
    };
  }
  // The installed route and new assignment are different views of the
  // same physical inventory; reserved ports must remain visible in readback.
  function logicalReservedPins(snapshot) {
    const d=snapshot?.device;
    return (Array.isArray(snapshot?.raw?.logicalOutputRegistry?.ports)?
      snapshot.raw.logicalOutputRegistry.ports:[]).filter(p=>p.bound===true&&
      p.playback===d?.playbackDevice&&(!p.backend||p.backend===d.backend))
      .flatMap(p=>[p.left,p.right]);
  }
  function outputChoices(snapshot,includeReserved=false) {
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
    const reserved=new Set(logicalReservedPins(snapshot));
    for(const [left,a] of map) {
      if(left%2!==0||!map.has(left+1)||
         (!includeReserved&&(reserved.has(left)||reserved.has(left+1))))continue;
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
  function externalPresentation(snapshot,row,choices=outputChoices(snapshot,true)) {
    const match=row&&choices.find(p=>p.left===row.physicalLeft &&
      p.right===row.physicalRight);
    const installed=row?.status==="LIVE"&&match;
    const state=installed?(snapshot?.device?.outputArmed===true?"LIVE":"MUTED"):
      row?.status==="ASSIGNED_SILENT"&&match?"ASSIGNED_SILENT":
      row?.status==="PENDING"&&match?"PENDING":row?"NEEDS_REMAP":"NONE";
    const value=row?((state==="LIVE"||state==="MUTED"||state==="ASSIGNED_SILENT")?match.label:
      (state==="PENDING"?"Pending · ":"Needs Remap · ")+
       "OUT "+(row.physicalLeft+1)+"/"+(row.physicalRight+1)):"None";
    const marker=state==="NONE"?"":state==="NEEDS_REMAP"?"REMAP":state==="ASSIGNED_SILENT"?"SILENT":state;
    const title=state==="ASSIGNED_SILENT"?"Output assigned · silent until a signal path is available":
      state==="PENDING"?"Native PCM pending · wait for audio callback":
      state==="NEEDS_REMAP"?
      "Saved physical output disarmed; choose a verified ASIO pair":
      state==="MUTED"?"ASIO pair connected · output muted, settings retained":
      state==="LIVE"?"LIVE · independent native post-fader PCM":
      "None · internal WIRE remains unchanged";
    return {match,state,value,marker,title};
  }

  // One transient draft for the existing Device Setup dialog. All native
  // inventory and installed PCM remain external readback, never owned here.
  // Pure presentation policy: only a positive native startup request can
  // open the existing Audio I/O surface. Closing or manually opening it
  // consumes the request for this application session.
  function startupAudioSetupDecision(live,{presented=false,busy=false,blocked=false}={}){
    return live?.startupAudioSetupRequired===true&&!presented&&!busy&&!blocked;
  }
  function setupSameEndpoint(active,draft) {
    return active?.open===true&&active.backend===draft?.backend&&
      active.captureDevice===draft.capture&&active.playbackDevice===draft.playback;
  }
  // Pin discovery belongs to the accepted driver, never a candidate endpoint.
  // A new rate/buffer also needs driver negotiation before routing.
  function setupRoutingReady(inventory,draft,state=setupInitial()) {
    const a=inventory?.active;
    return !state.pending && !["RECOVERY_REQUIRED","STALE"].includes(state.phase) &&
      setupSameEndpoint(a,draft) &&
      draft.rate===a.rate && draft.buffer===a.buffer &&
      pinInventory(a,"input")!==null && pinInventory(a,"output")!==null;
  }
  // Compare fresh JUCE inventory with independent installed device snapshot.
  // Generation and route epoch need not be identical (STOP/START advances epoch).
  function setupMatrixDeviceMatches(inventory,snapshot){
    const a=inventory?.active,d=snapshot?.device;
    return a?.open===true&&a.authorized===true&&snapshot?.retained!==true&&
      Number.isSafeInteger(a.deviceGeneration)&&a.deviceGeneration>=0&&
      typeof d?.epoch==="string"&&/^[1-9][0-9]*$/.test(d.epoch)&&
      BigInt(d.epoch)>=BigInt(a.deviceGeneration)&&
      d.backend===a.backend&&d.captureDevice===a.captureDevice&&
      d.playbackDevice===a.playbackDevice&&d.sampleRate===a.rate&&
      d.bufferFrames===a.buffer&&samePinMask(d.enabledInputs,a.enabledInputs)&&
      samePinMask(d.enabledOutputs,a.enabledOutputs)&&
      ["input","output"].every(side=>{
        const input=side==="input",names=a[input?"inputNames":"outputNames"],
          enabled=d[input?"enabledInputs":"enabledOutputs"],
          endpoints=d[input?"inputEndpoints":"outputEndpoints"];
        return Array.isArray(names)&&Array.isArray(endpoints)&&enabled.every(port=>
          endpoints.some(p=>p.port===port&&p.active===true&&p.label===names[port]));
      });
  }
  function setupPinsApplied(inventory,draft){
    const a=inventory?.active;
    return setupSameEndpoint(a,draft)&&samePinMask(a.enabledInputs,draft.inputPins)&&
      samePinMask(a.enabledOutputs,draft.outputPins);
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
  // P0: propose verified Windows capture pins, never installed PCM.
  function captureRecoveryDraft(inventory,draft,state=setupInitial()) {
    const a=inventory?.active;
    if(!a||a.authorized!==true||!setupRoutingReady(inventory,draft,state)||
       !String(a.backend).startsWith("Windows Audio")||!a.captureDevice||
       !inventory.types?.some(t=>t.name===a.backend&&t.separate===true)||
       !Array.isArray(a.enabledInputs)||a.enabledInputs.length!==0||
       !Array.isArray(a.inputNames)||!a.inputNames.length||
       !Array.isArray(draft.inputPins)||draft.inputPins.length!==0||
       !Array.isArray(a.outputNames)||!validPinMask(draft.outputPins,a.outputNames)||
       draft.outputPins.length<2||!samePinMask(draft.outputPins,a.enabledOutputs))
      return null;
    const proposed={...draft,inputPins:a.inputNames.slice(0,2).map((_,i)=>i)};
    return setupPresentation(inventory,proposed,state).canApply?proposed:null;
  }
  function signalPathHint(a) {
    if(!a?.open)return "No open audio device.";
    if(a.authorized!==true)return "Device route not authorized. Copy Audio Report.";
    if(a.captureDevice&&Array.isArray(a.enabledInputs)&&!a.enabledInputs.length)
      return a.inputNames?.length?
        "Capture selected, but ZERO input pins enabled. Review capture pins.":
        "Capture selected, but driver reports no input pins. Copy Audio Report.";
    return ({NATIVE_ROUTE_UNBOUND:"Mixer route not bound. Check routing or copy report.",
      OUTPUT_PINS_MISSING:"Playback pins unavailable. Copy Audio Report.",
      OPERATOR_OUTPUT_MUTED:"Output explicitly muted.",
      OUTPUT_NOT_ARMED:"Output not armed. Check channel input and route.",
      MASTER_MUTED:"Master mute is active.",
      CAPTURE_CALLBACK_NO_INPUT:"Driver pins exist, callback reports no input. Copy Audio Report."
    })[a.audioPathCode]||"Device readback only; actual audio is not certified.";
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
      if(value===draft.backend)return draft;
      const type=inventory.types?.find(t=>t.name===value);
      if(!type)return draft;
      const capture=type.inputs?.[0]||"";
      const next={...draft,backend:value,capture,
        playback:type.separate?type.outputs?.[0]||"":capture,
        inputPins:null,outputPins:null,rate:0,buffer:0};
      return setupSameEndpoint(inventory.active,next)?setupDraft(inventory):next;
    }
    if(field==="capture") {
      if(!view.inputOptions.includes(value))return draft;
      if(value===draft.capture)return draft;
      const next={...draft,capture:value,
        playback:view.type?.separate?draft.playback:value,
        inputPins:null,outputPins:null,rate:0,buffer:0};
      return setupSameEndpoint(inventory.active,next)?setupDraft(inventory):next;
    }
    if(field==="playback") {
      if(!view.outputOptions.includes(value)||
         (view.type?.separate!==true&&!view.inputOptions.includes(value)))
        return draft;
      // A JUCE non-separate backend has one duplex endpoint identity.
      // Never present an input/output mismatch that native must reject.
      if(value===draft.playback)return draft;
      const next={...draft,playback:value,
        capture:view.type?.separate?draft.capture:value,
        inputPins:null,outputPins:null,rate:0,buffer:0};
      return setupSameEndpoint(inventory.active,next)?setupDraft(inventory):next;
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
    const inspectNewAsio=expected.backend==="ASIO"&&
      state.pending.expected.requestedPins===null;
    return {phase:"ACCEPTED",pending:null,remapGeneration:null,
      ...(inspectNewAsio?{inspectNewAsio:true}:{}),
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
  // Named shortcuts are presentation of native disk inventory, not source
  // bindings. Resolve the current exact physical choice before dispatch.
  function namedPatchChoices(inventory,snapshot,side){
    const data=inventory?.namedPatches;
    if(!Array.isArray(data?.entries)||data.readOnly===true||
       !Number.isSafeInteger(data.revision)||data.revision<0||
       !setupMatrixDeviceMatches(inventory,snapshot)||
       (side!=="input"&&side!=="output"))return [];
    const d=snapshot.device;
    const pins=side==="input"?activeInputPins(snapshot):
      d.outputEndpoints.filter(p=>p.active===true&&d.enabledOutputs.includes(p.port));
    const physical=side==="output"?outputChoices(snapshot):[];
    const pinMatches=(port,label)=>typeof label==="string"&&label.length>0&&
      pins.some(p=>p.port===port&&p.label===label);
    const result=[];
    for(const p of data.entries){
      if(!p||p.side!==side||p.available!==true||typeof p.id!=="string"||!p.id||
         typeof p.name!=="string"||!p.name.trim()||
         !pinMatches(p.left,p.leftName)||
         (p.right!==-1&&!pinMatches(p.right,p.rightName)))continue;
      const choice=side==="input"?physicalInputChoice(snapshot,p.mode,p.left,p.right):
        p.mode==="STEREO"?physical.find(x=>x.left===p.left&&x.right===p.right):null;
      if(!choice||choice.left<0)continue;
      result.push({id:p.id,name:p.name,choice,label:"Preset · "+p.name+" · "+choice.label});
    }
    const ids=new Map(),labels=new Map();
    for(const p of result){
      ids.set(p.id,(ids.get(p.id)||0)+1);labels.set(p.label,(labels.get(p.label)||0)+1);
    }
    return result.filter(p=>ids.get(p.id)===1&&labels.get(p.label)===1);
  }
  return Object.freeze({activeInputPins,inputChoices,inputLabel,inputLabelResolver,outputChoices,patchMatrix,matrixCrosspointChoice,physicalInputChoice,namedPatchChoices,busOutputChoices,
    externalPresentation,startupAudioSetupDecision,setupDraft,setupPresentation,setupLatencyReadback,setupEditDraft,setupRoutingReady,setupPinsApplied,setupMatrixDeviceMatches,
    pinInventory,setupSelectAllPins,setupBufferPeriod,setupHealthReadback,captureRecoveryDraft,signalPathHint,
    setupInitial,setupBegin,setupAck,setupReadback,
    setupTimeout,setupClearResult,nativeLifecycleLease,nativeLifecycleDecision,
    nativeTransientReadbackPolicy});
});
