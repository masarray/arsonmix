// SPDX-License-Identifier: AGPL-3.0-only
// Pure, deterministic v8/schemaVersion=6 accepted-state reconciler.
// NO DSP, mock meter, ordinal ChannelId, optimistic graph, or second GUI.
// Also loaded by node:test; no browser APIs required by the core.
(function (root, factory) {
  const api = factory();
  if (typeof module === "object" && module.exports) module.exports = api;
  if (root) root.ArSonApprovedNativeContract = api;
})(typeof globalThis === "object" ? globalThis : undefined, function () {
  "use strict";
  const MAX64 = (1n << 64n) - 1n;
  const decimal = value => {
    if (typeof value !== "string" || !/^(0|[1-9]\d*)$/.test(value)) return null;
    try { return BigInt(value) <= MAX64 ? value : null; } catch { return null; }
  };
  const cmp = (a, b) => BigInt(a) < BigInt(b) ? -1 : BigInt(a) > BigInt(b) ? 1 : 0;
  const object = value => value !== null && typeof value === "object" && !Array.isArray(value);
  const fields = (item, names) => object(item) && names.every(key => Object.prototype.hasOwnProperty.call(item, key));
  const isBool = x => typeof x === "boolean";
  const unique = values => new Set(values).size === values.length;
  // G-MV20: ONE canonical installed-hardware fingerprint for both the v6
  // ACK/readback controller and GUI lifecycle ViewModel. Pin masks are SETS:
  // enumeration order and MONITOR/outputArmed are deliberately excluded.
  // No driver name, PCM state, mutable pin array or graph revision is copied.
  function nativeHardwareConfiguration(d) {
    if(!object(d)||!Number.isFinite(d.sampleRate)||d.sampleRate<0||
       !Number.isSafeInteger(d.bufferFrames)||d.bufferFrames<0||
       !Array.isArray(d.enabledInputs)||!Array.isArray(d.enabledOutputs)||
       ![d.enabledInputs,d.enabledOutputs].every(pins=>
         pins.length<=256&&pins.every(pin=>
           Number.isInteger(pin)&&pin>=0&&pin<=255)&&unique(pins)))
      return null;
    const canonical=pins=>pins.slice().sort((a,b)=>a-b);
    return JSON.stringify([d.sampleRate,d.bufferFrames,
      canonical(d.enabledInputs),canonical(d.enabledOutputs)]);
  }
  // Pure native UI presentation policy: no DOM, physical status rewrite,
  // optimistically accepted graph or alternative mixer renderer.
  const validPresentationName=name=>typeof name==="string"&&name.length>0&&name.length<=30&&
    name===name.trim()&&!/[\x00-\x1f\x7f]/.test(name);
  const validPresentationColor=color=>typeof color==="string"&&/^#[0-9a-fA-F]{6}$/.test(color);
  const validPresentationIcon=icon=>typeof icon==="string"&&
    ["♩","♫","♬","🎙","🎸","🎹","🥁","🎧","▥","◉","◎","⊞"].includes(icon);

  const operatorMessage = message => {
    const raw=String(message??"").trim();
    if(!raw)return "";
    // A product never advertises unfinished internal QA/FAT certification to
    // operators. This is a display-only adapter, not an audio-state override.
    if(/\b(?:HARDWARE\s+)?FAT\s+(?:PENDING|PASSED|FAILED)\b|\b(?:NATIVE\s+)?PCM\s+(?:GRAPH|HARDWARE|FAT)\b|\b(?:PENDING_ACCEPTED_SNAPSHOT|HARDWARE_FAT_PENDING)\b/i.test(raw)){
      if(/\b(?:output muted|monitor off)\b/i.test(raw))return "Output muted";
      if(/\b(?:monitor on|output enabled|output active)\b/i.test(raw))return "Output enabled";
      return "Mixer ready";
    }
    // U1: native output validation already emits operator-actionable reasons.
    // Do not normalize those into a generic success/failure toast.
    if(/^EXTERNAL OUT:|^BUS OUTPUT:|^DMX:|^Secondary output requires|^MUTE hardware OUTPUT/i.test(raw))return raw;
    const failure=/\b(reject(?:ed|ion)?|fail(?:ed|ure)?|error|timed out|not applied|unavailable|invalid)\b|(?:^|[^A-Z0-9])(FAILED|ERROR|REJECTED|STALE|INVALID|TIMEOUT|MISSING|UNSUPPORTED|UNKNOWN|MISMATCH|CONFLICTING)(?:\b|_)/i.test(raw);
    // Editor navigation cannot be reported as a successful FX edit.
    if(/\b(?:FX|VST3|plugin|effect|built-in)\b.*\beditor\b|\beditor\b.*\b(?:FX|VST3|plugin|effect|built-in)\b/i.test(raw)){
      if(failure||/\b(refresh|changed|expired|retry|close|finish|needs|could not|no native|does not|held|disconnected)\b/i.test(raw))
        return "FX editor unavailable · Reopen from FX Rack";
      if(/\b(opened|visible|focused)\b/i.test(raw))return "FX editor opened";
      return "Opening FX editor…";
    }
    const internalCode=/\b[A-Z][A-Z0-9]{2,}(?:_[A-Z0-9]+)+\b/.test(raw);
    if(internalCode ||
       /\b(FAT|PCM|ACK|readback|admission|admitted|graph|roster|epoch|revision|M[0-9]|JUCE|callback|worker|fixture|reference|mockup|simulated|demo|legacy|native|authoritative)\b/i.test(raw)){
      if(/\b(device|driver|audio interface)\b|DEVICE_/i.test(raw) &&
         /\b(disconnect|unavailable|not ready|not open|not found|failed)\b|_(FAILED|ERROR|DISCONNECTED|UNAVAILABLE)/i.test(raw))
        return "Audio device unavailable · Open Audio I/O";
      if(/\b(WIRE|route|routing)\b|(?:^|_)(?:WIRE|ROUTING)(?:_|$)/i.test(raw)){
        if(failure)
          return "Routing not changed · Check the connection and try again";
        if(/\b(pending|await|verif|applying|completing)\w*/i.test(raw))
          return "Updating channel routing…";
        return "Channel routing updated";
      }
      if(/\b(VST3|plugin|effect|FX)\b|(?:^|_)(?:VST3|PLUGIN|FX)(?:_|$)/i.test(raw)){
        if(failure)
          return "Effect change failed · Previous settings kept";
        if(/\b(pending|await|verif|loading|applying)\w*/i.test(raw))
          return "Applying effect…";
        return "Effect settings updated";
      }
      if(failure)
        return "Change not applied · Previous mixer settings kept";
      if(/\b(pending|awaiting|verifying|saving|processing|applying|loading|deleting)\b|_PENDING\b|PENDING_/i.test(raw))
        return "Applying mixer changes…";
      return "Mixer settings updated";
    }
    return raw;
  };

  function normalizeSnapshot(raw) {
    if (!fields(raw, ["schemaVersion","productionCallbackLive","device","graph","telemetry"]) ||
        raw.schemaVersion !== 6 || !isBool(raw.productionCallbackLive)) return null;
    const d = raw.device, g = raw.graph, t = raw.telemetry;
    if (!fields(d, ["epoch","backend","captureDevice","playbackDevice","inputEndpoints",
                     "outputEndpoints","enabledInputs","enabledOutputs","sampleRate","bufferFrames","outputArmed"]) ||
        !fields(g, ["revision","rosterRevision","channels","destinations","capabilities"]) ||
        !object(t) || decimal(d.epoch) === null || decimal(g.revision) === null ||
        decimal(g.rosterRevision) === null || !Array.isArray(g.channels) ||
        !Array.isArray(g.destinations) || !Array.isArray(d.inputEndpoints) ||
        !Array.isArray(d.outputEndpoints) || !Array.isArray(d.enabledInputs) ||
        !Array.isArray(d.enabledOutputs) || !isBool(d.outputArmed) ||
        !Number.isFinite(d.sampleRate) || !Number.isInteger(d.bufferFrames)) return null;
    const caps = g.capabilities;
    if (!fields(caps, ["callbackOwnsChannelIds","supportedOps","dynamicChannels",
                       "groups","fxReturns","monitor"]) || !isBool(caps.callbackOwnsChannelIds) ||
        !Array.isArray(caps.supportedOps) || !caps.supportedOps.every(s => typeof s === "string")) return null;
    const enabled = arr => unique(arr) && arr.every(n => Number.isInteger(n) && n >= 0 && n <= 255);
    if (!enabled(d.enabledInputs) || !enabled(d.enabledOutputs)) return null;
    const hardwareConfiguration=nativeHardwareConfiguration(d);
    if(hardwareConfiguration===null)return null;
    // Physical endpoint numbers are safe *port indices*; graph ChannelIds are never numbers.
    const goodEndpoints = (entries,pins,prefix) => entries.every(e =>
      object(e) && Number.isInteger(e.port) && pins.includes(e.port) &&
      typeof e.id === "string" && e.id === prefix + e.port &&
      typeof e.label === "string" && Number.isInteger(e.callback) &&
      e.callback >= 0 && typeof e.active === "boolean");
    if (!goodEndpoints(d.inputEndpoints,d.enabledInputs,"in:") ||
        !goodEndpoints(d.outputEndpoints,d.enabledOutputs,"out:")) return null;
    const ids = [];
    for (const ch of g.channels) {
      if (!object(ch) || decimal(ch.id) === null || !ch.id || !["INPUT","GROUP","FX_RETURN","MAIN","MONITOR"].includes(ch.role) ||
          typeof ch.name !== "string" || !["MONO","STEREO"].includes(ch.processingFormat)) return null;
      ids.push(ch.id);
      if (ch.primaryOutId !== undefined &&
          (decimal(ch.primaryOutId) === null || ch.primaryOutId === ch.id)) return null;
      if (ch.controls !== undefined &&
          (!object(ch.controls) ||
            (ch.controls.faderDb !== undefined && !Number.isFinite(ch.controls.faderDb)) ||
            (ch.controls.inputGainDb !== undefined && !Number.isFinite(ch.controls.inputGainDb)) ||
          (ch.controls.wireSumGainDb !== undefined && (!Number.isFinite(ch.controls.wireSumGainDb)||ch.controls.wireSumGainDb< -60||ch.controls.wireSumGainDb>6.03)) ||
            (ch.controls.pan !== undefined && !Number.isFinite(ch.controls.pan)) ||
            (ch.controls.mute !== undefined && !isBool(ch.controls.mute)) ||
            (ch.controls.solo !== undefined && !isBool(ch.controls.solo)))) return null;
    }
    if (!unique(ids)) return null;
    for (const ch of g.channels) {
      if (ch.primaryOutId !== undefined && !ids.includes(ch.primaryOutId)) return null;
    }
    const edges = Array.isArray(g.edges) ? g.edges : [];
    if (edges.some(e => !object(e) || decimal(e.edgeId) === null ||
        decimal(e.from) === null || decimal(e.to) === null ||
        !ids.includes(e.from) || !ids.includes(e.to) || e.from === e.to ||
        e.signalKind !== "AUDIO" || !isBool(e.enabled) ||
        !Number.isFinite(e.gainLinear) || e.gainLinear < 0 ||
        (e.sendEnabled!==undefined && !isBool(e.sendEnabled)))) return null;
    if (!unique(edges.map(e => e.edgeId))) return null;
    if (g.mainChannelId !== undefined &&
        (decimal(g.mainChannelId) === null || !ids.includes(g.mainChannelId))) return null;
    // Advertised LIVE is gated by real native callback, valid graph, and actual hardware.
    // A dishonest producer cannot be "fixed" by manufacturing Channels in JS.
    const mainCount = g.channels.filter(ch=>ch.role==="MAIN").length;
    // Real accepted ChannelId PCM may exist before vendor/Show/hardware product
    // FAT. Never equate an installed PCM graph with a release-ready product.
    // Conversely, callbackOwnsChannelIds is ONLY advertised by the real
    // installed callback; the legacy M4/Pages demo must remain unadmitted.
    const hasAcceptedGraph =
      cmp(d.epoch,"0") > 0 && cmp(g.revision,"0") > 0 &&
      cmp(g.rosterRevision,"0") > 0 && Array.isArray(g.edges) && g.channels.length > 0 && mainCount === 1 &&
      typeof g.mainChannelId === "string" &&
      g.channels.some(ch => ch.id === g.mainChannelId && ch.role === "MAIN") &&
      g.channels.every(ch => fields(ch,["controls","inserts","sends"]) &&
        fields(ch.controls,["faderDb","pan","mute","solo"]) &&
        Number.isFinite(ch.controls.faderDb) &&
        Number.isFinite(ch.controls.pan) && isBool(ch.controls.mute) &&
        isBool(ch.controls.solo) && Array.isArray(ch.inserts) &&
        Array.isArray(ch.sends));
    // Native insert instance tokens are live FX lease identities, not a
    // browser slot ordinal or catalog name. A malformed slot/duplicate token
    // must never enter the accepted renderer: MOVE/REMOVE and vendor editors
    // dereference these entries after native ACK.
    const validInsert = fx => fields(fx,["id","catalogId"]) &&
      typeof fx.id === "string" && fx.id.length > 0 &&
      typeof fx.catalogId === "string" && fx.catalogId.length > 0 &&
      (fx.name === undefined || typeof fx.name === "string") &&
      (fx.bypassed === undefined || isBool(fx.bypassed));
    if (hasAcceptedGraph) {
      const tokens=[];
      for (const ch of g.channels) {
        if (ch.inserts.length > 8 || !ch.inserts.every(validInsert)) return null;
        for (const fx of ch.inserts) tokens.push(fx.id);
      }
      if (!unique(tokens)) return null;
    }
    const retained = g.bindingState === "NEEDS_REMAP";
    if (retained && (!hasAcceptedGraph || caps.callbackOwnsChannelIds ||
        caps.supportedOps.length !== 0 || d.outputArmed ||
        raw.mixerSession?.authority !== "CHANNEL_ID" ||
        raw.mixerSession?.bindingState !== "NEEDS_REMAP" ||
        raw.mixerSession?.deviceEpoch !== d.epoch)) return null;
    const admitted = caps.callbackOwnsChannelIds === true && hasAcceptedGraph &&
      d.sampleRate > 0 && d.bufferFrames > 0 &&
      d.enabledOutputs.length > 0 && d.outputEndpoints.length > 0;
    // A producer may stage PCM without claiming full release/UAT. A producer
    // must NEVER claim product LIVE without an admitted PCM graph.
    if (raw.productionCallbackLive && !admitted) return null;
    if (caps.callbackOwnsChannelIds && !admitted) return null;
    return { raw, device:d, graph:g, telemetry:t, admitted, retained,
      hardwareConfiguration,productLive:admitted && raw.productionCallbackLive,
      identity: d.epoch + ":" + g.revision + ":" + g.rosterRevision };
  }
  // MV-3: select exactly one existing native read authority for /preview/.
  // The reconciler already validated schema-v6, epoch and accepted PCM.
  // This is a projection of THAT immutable control-thread readback, never
  // an alternative session, acceptance controller or JS-owned graph.
  function selectNativeReadModel(next,verifiedM4=false) {
    const invalid=Object.freeze({kind:"INVALID",view:null});
    if(!next || next.raw?.schemaVersion!==6 ||
       next.device!==next.raw.device || next.graph!==next.raw.graph ||
       !next.device || !next.graph || typeof next.admitted!=="boolean" ||
       typeof next.retained!=="boolean")return invalid;
    const session=next.raw.mixerSession,offline=next.raw.logicalMixer;
    // The physical installed producer (including NEEDS_REMAP) takes
    // priority over a saved logical .smix, even if stale metadata is present.
    // Do NOT use outputArmed, GUI labels, or an ACK to invent PCM admission.
    const logicalSchema=offline?.schema==="arsonmix.native.logical-mixer.v1"&&
      offline.authority==="LOGICAL_ONLY"&&
      typeof offline.workspaceToken==="string"&&offline.workspaceToken.length>0&&
      offline.physicalOutputAdmitted===false&&offline.outputArmed===false&&
      offline.graph?.capabilities?.callbackOwnsChannelIds===false&&
      decimal(offline.revision)!==null&&offline.revision!=="0"&&
      offline.revision===offline.graph?.revision&&
      offline.revision===offline.graph?.rosterRevision&&
      Array.isArray(offline.graph?.channels)&&Array.isArray(offline.graph?.edges);
    const retainedOffline=logicalSchema&&!next.admitted&&next.retained&&
      verifiedM4!==true&&session?.authority==="CHANNEL_ID"&&
      session?.bindingState==="NEEDS_REMAP"&&
      next.device.outputArmed===false&&
      offline.retainedOffline===true&&
      offline.retainedDeviceEpoch===next.device.epoch&&
      offline.retainedGraphRevision===next.graph.revision&&
      session.deviceEpoch===next.device.epoch;
    const offlineEligible=logicalSchema&&!next.admitted&&!next.retained&&
      verifiedM4!==true&&session?.authority!=="M4"&&
      session?.authority!=="CHANNEL_ID";
    if(offlineEligible||retainedOffline) {
      // A logical view is a read-only DISPLAY projection of the same native
      // envelope, not a second accepted mixer or physical output authority.
      const view={...next,graph:offline.graph,
        workspaceToken:offline.workspaceToken,
        identity:"logical:"+offline.workspaceToken+":"+offline.revision,
        logical:true,readOnly:offline.readOnly===true,
        physicalRetained:retainedOffline,
        admitted:false,retained:false,productLive:false,telemetry:{}};
      return Object.freeze({kind:retainedOffline?"RETAINED_OFFLINE":"LOGICAL_ONLY",view});
    }
    if(!session || session.deviceEpoch!==next.device.epoch)
      return Object.freeze({kind:"STALE",view:null});
    if(session.authority==="CHANNEL_ID"&&next.admitted===true&&
       next.graph.capabilities?.callbackOwnsChannelIds===true)
      return Object.freeze({kind:"CHANNEL_ID",view:next});
    if(next.retained===true&&session.authority==="CHANNEL_ID")
      return Object.freeze({kind:"NEEDS_REMAP",view:next});
    if(session.authority==="M4"&&verifiedM4===true&&!next.admitted&&!next.retained)
      return Object.freeze({kind:"M4",view:next});
    return Object.freeze({kind:"UNADMITTED",view:null});
  }

  // Read-only physical OUTPUTS projection from the authoritative JUCE snapshot.
  // No guessed ports, UI-owned routing, monitor activation or audible claims.
  // Strict native timing projection. No inferred monitor from UI labels.
  function monitorTimingView(snapshot) {
    const raw=snapshot?.raw,p=raw?.monitorTiming,d=snapshot?.device;
    if(!snapshot?.admitted||snapshot.retained||!p||d?.backend!=="ASIO"||
       !["GLOBAL_ALIGNED","INDEPENDENT_MONITOR"].includes(p.policy)||
       ![p.mainSamples,p.monitorSamples,p.potentialSaving].every(n=>Number.isInteger(n)&&n>=0)||
       p.monitorSamples>p.mainSamples||p.potentialSaving>p.mainSamples||
       (p.policy==="GLOBAL_ALIGNED"&&p.monitorSamples!==p.mainSamples)||
       (p.policy==="INDEPENDENT_MONITOR"&&p.mainSamples-p.monitorSamples!==p.potentialSaving))return null;
    const main=raw.physicalMainOutputs,monitor=raw.physicalMonitorOutputs;
    if(!Array.isArray(main)||!Array.isArray(monitor)||main.length!==2||monitor.length!==2||
       new Set([...main,...monitor]).size!==4||
       ![...main,...monitor].every(pin=>Number.isInteger(pin)&&d.enabledOutputs.includes(pin)&&
          d.outputEndpoints.some(e=>e.port===pin&&e.active&&e.label&&
              d.outputEndpoints.filter(x=>x.label===e.label).length===1)))return null;
    return {policy:p.policy,mainSamples:p.mainSamples,monitorSamples:p.monitorSamples,
      potentialSaving:p.potentialSaving,main:[...main],monitor:[...monitor],
      observed:raw.physicalBusOutputObserved===true,
      editable:raw.physicalBusOutputObserved===true&&raw.operatorOutputMuted===true&&
        raw.physicalBusSecondaryArmed===false&&d.outputArmed===false,
      epoch:d.epoch,revision:snapshot.graph.revision,rosterRevision:snapshot.graph.rosterRevision};
  }
  // One secondary destination projection for the quick output controller and
  // unified workspace. No ACK, friendly name or browser draft proves PCM.
  function secondaryMixMode(snapshot,live) {
    const raw=snapshot?.raw,d=snapshot?.device,g=snapshot?.graph;
    const output=secondaryOutputView(snapshot,live);
    const split=output.split;
    if(!snapshot?.admitted||snapshot.retained||d?.backend!=="ASIO"||
       d.enabledOutputs?.length!==2||raw?.physicalBusOutputObserved!==true||
       [d.epoch,g?.revision,g?.rosterRevision].some(v=>decimal(v)===null)||
       !Array.isArray(raw.physicalMainOutputs)||!Array.isArray(raw.physicalMonitorOutputs))
      return {available:false,split,reason:"Independent mix requires a verified two-output ASIO route."};
    if(raw.externalOutputs?.length||raw.logicalOutputRegistry?.references?.length||
       raw.logicalOutputRegistry?.ports?.some(p=>p.bound===true))
      return {available:false,split,reason:"Existing output assignments are preserved."};
    if(!split&&(raw.physicalMainOutputs.length!==2||raw.physicalMonitorOutputs.length!==0))
      return {available:false,split,reason:"Existing monitor mapping is preserved."};
    // Returning to Same mix unbinds the physical MONITOR sink but preserves
    // the authored native graph/FX. It is not a destructive channel edit.
    if(!output.armed||output.recovering)
      return {available:false,split,reason:"Enable the additional output first."};
    return {available:true,split,reason:split?"OBS receives MAIN. Your headphone routing and effects are preserved when you select Same mix.":"Separate mix creates a quiet PRE-fader starter on first use; existing headphone settings are preserved.",
      value:{enabled:!split,smart:true,deviceEpoch:d.epoch,graphRevision:g.revision,rosterRevision:g.rosterRevision}};
  }
  function secondaryMonitorStarter(snapshot,live) {
    const output=secondaryOutputView(snapshot,live),d=snapshot?.device,g=snapshot?.graph;
    if(!snapshot?.admitted||snapshot.retained||!output.split||!output.armed||
       snapshot.raw?.physicalBusOutputObserved!==true||!g||!d||
       [d.epoch,g.revision,g.rosterRevision].some(v=>decimal(v)===null))return null;
    const monitor=g.channels?.find(ch=>ch.role==="MONITOR");
    if(!monitor||!g.mainChannelId||!Array.isArray(g.edges)||
       g.edges.some(e=>e.to===monitor.id||e.from===monitor.id))return null;
    if(!g.edges.some(e=>e.to===g.mainChannelId&&e.primaryOutput===true&&e.enabled===true))return null;
    return {deviceEpoch:d.epoch,graphRevision:g.revision,rosterRevision:g.rosterRevision};
  }
  function secondaryOutputSelection(options,{active="",selected="",preferred="",hasSaved=false}={}) {
    if(options.includes(active))return active;
    if(options.includes(selected))return selected;
    if(options.includes(preferred))return preferred;
    if(hasSaved)return ""; // never replace a lost verified destination with a guess
    const cables=options.filter(n=>/^CABLE Input(?:\s|\(|$)/i.test(n));
    return cables.length===1?cables[0]:"";
  }
  function secondaryOutputView(snapshot,live) {
    const raw=snapshot?.raw;
    const liveEpoch=live?.dryRoutingLab?.epoch, snapshotEpoch=snapshot?.device?.epoch;
    const current=live&&typeof live.secondaryOutputReady==="boolean"&&
      typeof live.secondaryOutputArmed==="boolean"&&
      (liveEpoch==null||snapshotEpoch==null||String(liveEpoch)===String(snapshotEpoch));
    const split=current&&typeof live?.dryRoutingLab?.secondaryMainOnly==="boolean"?
      live.dryRoutingLab.secondaryMainOnly:raw?.secondaryMainOnly===true;
    const fault=current?live.secondaryOutputFault===true:raw?.secondaryOutputFault===true;
    const name=current?String(live.secondaryOutputName||""):String(raw?.secondaryOutputName||raw?.secondaryMainEndpoint||"");
    const ready=current?live.secondaryOutputReady===true:raw?.secondaryMainReady===true;
    const armed=current?live.secondaryOutputArmed===true:raw?.physicalBusSecondaryArmed===true;
    const recovering= current?live.secondaryOutputRecovering===true:raw?.secondaryOutputRecovering===true;
    const recoveryName=String((current?live.secondaryOutputRecoveryName:raw?.secondaryOutputRecoveryName)||"");
    const state=recovering&&!armed?"RECOVERING":fault?"FAULT":!ready||!name?(split?"NEEDS_REMAP":"DISCONNECTED"):
      armed?"ENABLED":"MUTED";
    return {split,name:name||recoveryName,ready:ready&&!fault,armed:ready&&!fault&&!!name&&armed,fault:!!fault,recovering:recovering&&!armed,state};
  }
  function secondaryOutputRow(snapshot,live) {
    const v=secondaryOutputView(snapshot,live);
    if(!v.split&&v.state==="DISCONNECTED")return null;
    return {pair:"STREAM",pins:v.name||"Windows playback endpoint disconnected",
      role:v.split?"MAIN":"MAIN MIRROR",owners:"MAIN",status:v.state,shared:0};
  }
  function physicalOutputOverview(snapshot) {
    const d=snapshot?.device,raw=snapshot?.raw;
    if(d?.backend!=="ASIO" || !Array.isArray(d.enabledOutputs) ||
       !Array.isArray(d.outputEndpoints) || !raw) return [];
    const enabled=new Set(d.enabledOutputs);
    const ports=new Map(d.outputEndpoints.filter(p=>p?.active===true &&
      Number.isInteger(p.port) && enabled.has(p.port) &&
      typeof p.label==="string" && p.label.length>0).map(p=>[p.port,p.label]));
    const good=(l,r)=>Number.isInteger(l)&&Number.isInteger(r)&&l!==r&&
      ports.has(l)&&ports.has(r);
    const pair=(l,r)=>"OUT "+(l+1)+"/"+(r+1);
    const pins=(l,r)=>ports.get(l)+" + "+ports.get(r);
    const used=new Set(),rows=[];
    const names=new Map((snapshot.graph?.channels||[]).map(ch=>[ch.id,ch.name]));
    const assigned=Array.isArray(raw.externalOutputs)?
      raw.externalOutputs.filter(p=>p && typeof p.channelId==="string" &&
        Number.isInteger(p.physicalLeft) && Number.isInteger(p.physicalRight) &&
        p.physicalLeft>=0 && p.physicalRight>=0):[];
    const owners=items=>items.map(p=>names.get(p.channelId)||
      "Channel "+p.channelId).join(" + ");
    for(const [key,role] of [["physicalMainOutputs","MAIN"],
                             ["physicalMonitorOutputs","MONITOR"]]){
      const selected=raw[key];
      if(!Array.isArray(selected)||selected.length!==2)continue;
      const [left,right]=selected;
      if(!Number.isInteger(left)||!Number.isInteger(right))continue;
      if(good(left,right)){
        used.add(left);used.add(right);
        rows.push({pair:pair(left,right),pins:pins(left,right),role,
          owners:role,status:d.outputArmed?"ENABLED":"MUTED",shared:0});
      }else rows.push({pair:pair(left,right),pins:"Saved pins unavailable",
        role,owners:role,status:"NEEDS_REMAP",shared:0});
    }
    if(raw.secondaryMainOnly===true&&raw.physicalMainOutputs?.length===0&&
       raw.physicalMonitorOutputs?.length===2&&good(...raw.physicalMonitorOutputs)){
      const ready=raw.secondaryMainReady===true&&typeof raw.secondaryMainEndpoint==="string"&&!!raw.secondaryMainEndpoint;
      rows.push({pair:"STREAM",pins:ready?raw.secondaryMainEndpoint:"WASAPI endpoint disconnected",role:"MAIN",
        owners:"MAIN",status:!ready?"NEEDS_REMAP":raw.physicalBusSecondaryArmed===true?"ENABLED":"MUTED",shared:0});
    }
    const logicalPorts=(Array.isArray(raw.logicalOutputRegistry?.ports)?
      raw.logicalOutputRegistry.ports:[]).filter(p=>p.bound===true&&
      p.playback===d.playbackDevice&&(!p.backend||p.backend===d.backend));
    const activeExternal=new Set();
    for(const left of [...ports.keys()].sort((a,b)=>a-b)){
      const right=left+1;
      // External stereo pair policy is even/odd physical pins only.
      if(left%2 || !ports.has(right) || used.has(left) || used.has(right))continue;
      used.add(left);used.add(right);
      const items=assigned.filter(p=>p.physicalLeft===left&&p.physicalRight===right);
      activeExternal.add(left+":"+right);
      const applied=items.filter(p=>snapshot.admitted===true&&p.status==="LIVE");
      const logical=logicalPorts.find(p=>[p.left,p.right].some(pin=>pin===left||pin===right));
      const state=applied.length?(d.outputArmed?"LIVE":"MUTED"):
        items.some(p=>p.status==="PENDING")?"PENDING":
        items.length?"NEEDS_REMAP":logical?"RESERVED":"FREE";
      rows.push({pair:pair(left,right),pins:pins(left,right),
        role:items.length?"EXTERNAL":logical?"LOGICAL":"AVAILABLE",
        owners:items.length?owners(items):logical?logical.name:"Unassigned",status:state,
        shared:applied.length});
    }
    for(const port of [...ports.keys()].sort((a,b)=>a-b)){
      if(!used.has(port))rows.push({pair:"OUT "+(port+1),
        pins:ports.get(port),role:"UNPAIRED",owners:"No verified stereo pair",
        status:"NOT_PATCHABLE",shared:0});
    }
    // Retained pairs from another ASIO device remain visible, never LIVE.
    const remembered=new Map();
    for(const item of assigned){
      const key=item.physicalLeft+":"+item.physicalRight;
      if(activeExternal.has(key))continue;
      if(!remembered.has(key))remembered.set(key,[]);
      remembered.get(key).push(item);
    }
    for(const [key,items] of remembered){
      const [left,right]=key.split(":").map(Number);
      rows.push({pair:pair(left,right),pins:"Saved pair not available",
        role:"EXTERNAL",owners:owners(items),status:"NEEDS_REMAP",shared:0});
    }
    return rows;
  }
  // ACK alone is never physical evidence. Readback must prove that a known
  // control/route actually reached the accepted native graph. No invented UI
  // updates and no optimistic fader/edge commits.
  function readbackProvesIntent(next, pending) {
    // Also exposed as a pure test/helper API. A rejected/unadmitted native
    // snapshot cannot be used to prove an FX or WIRE command.
    if (!next?.admitted || !pending || !object(pending.payload)) return false;
    const {operation,targetChannelId,payload} = pending;
    const ch = next.graph.channels.find(c=>c.id===targetChannelId);
    if (!ch) return false;
    if (operation === "SET_INPUT_GAIN")
      return ch.role==="INPUT" && Number.isFinite(payload.valueDb) &&
        Number.isFinite(ch.controls?.inputGainDb) &&
        Math.abs(ch.controls.inputGainDb-payload.valueDb)<=0.12;
    if (operation === "SET_WIRE_SUM_GAIN")
      return ["INPUT","GROUP","FX_RETURN"].includes(ch.role) && Number.isFinite(payload.valueDb) &&
        Number.isFinite(ch.controls?.wireSumGainDb) &&
        Math.abs(ch.controls.wireSumGainDb-payload.valueDb)<=0.12;
    if (operation === "SET_FADER")
      return Number.isFinite(payload.valueDb) &&
        Math.abs(ch.controls.faderDb-payload.valueDb)<=0.11;
    if (operation === "SET_PAN")
      return Number.isFinite(payload.value) &&
        Math.abs(ch.controls.pan-payload.value)<=0.015;
    if (operation === "SET_MUTE" || operation === "SET_SOLO")
      return typeof payload.value==="boolean" &&
        ch.controls[operation==="SET_MUTE"?"mute":"solo"]===payload.value;
    if (operation === "SET_PROCESSING_FORMAT")
      return ["MONO","STEREO"].includes(payload.format) &&
        cmp(next.graph.revision,pending.baseGraphRevision)>0 &&
        cmp(next.graph.rosterRevision,pending.baseRosterRevision)>0 &&
        ch.processingFormat===payload.format;
    if (operation === "LOAD_INSERT")
      return cmp(next.graph.revision,pending.baseGraphRevision)>0 &&
        cmp(next.graph.rosterRevision,pending.baseRosterRevision)>0 &&
        Number.isInteger(payload.slot)&&payload.slot>=0 &&
        typeof payload.catalogId==="string" &&
        Array.isArray(ch.inserts)&&ch.inserts.length===payload.slot+1 &&
        ch.inserts[payload.slot]?.catalogId===payload.catalogId &&
        typeof ch.inserts[payload.slot]?.id==="string" &&
        ch.inserts[payload.slot].id.length>0 &&
        !ch.inserts.slice(0,payload.slot).some(insert=>
          insert.id===ch.inserts[payload.slot].id);
    if(operation==="CONVERT_REVERB_RETURN"){
      const expected=pending.fxExpected;
      if(!expected||cmp(next.graph.revision,pending.baseGraphRevision)<=0||
         cmp(next.graph.rosterRevision,pending.baseRosterRevision)<=0)return false;
      const ids=ch.inserts.map(x=>x.id);
      if(ids.length!==expected.source.length||ids.some((x,i)=>x!==expected.source[i]))return false;
      const created=next.graph.channels.filter(c=>!expected.previous.includes(c.id)&&
        c.role==="FX_RETURN"&&c.inserts?.length===1&&
        c.inserts[0].catalogId==="armix.builtin.vocal-reverb-return.v1");
      if(created.length!==1||created[0].primaryOutId!==next.graph.mainChannelId)return false;
      return next.graph.edges.some(e=>e.enabled&&e.from===targetChannelId&&e.to===created[0].id&&
        e.primaryOutput===false&&e.tap==="POST_FADER") &&
        !next.graph.channels.some(c=>c.inserts.some(x=>x.id===payload.instanceId));
    }
    if(operation==="REMOVE_INSERT"||operation==="MOVE_INSERT") {
      const expected=pending.fxExpected;
      if(!expected||cmp(next.graph.revision,pending.baseGraphRevision)<=0||
         cmp(next.graph.rosterRevision,pending.baseRosterRevision)<=0)return false;
      const ids=id=>(next.graph.channels.find(c=>c.id===id)?.inserts||[]).map(x=>x.id);
      const equal=(a,b)=>a.length===b.length&&a.every((x,i)=>x===b[i]);
      if(!equal(ids(targetChannelId),expected.source))return false;
      if(expected.to&&!equal(ids(expected.to),expected.destination))return false;
      const copies=next.graph.channels.reduce((n,c)=>n+c.inserts.filter(x=>x.id===payload.instanceId).length,0);
      return copies===(operation==="REMOVE_INSERT"?0:1);
    }
    if (operation === "SET_SEND_ENABLED") {
      const before=pending.wireIdentity;
      const edge=next.graph.edges.find(e=>e.enabled&&e.edgeId===payload.edgeId&&e.from===targetChannelId);
      return !!before && !!edge && edge.primaryOutput===false &&
        edge.to===before.to && edge.tap===before.tap &&
        typeof payload.enabled==="boolean" && edge.sendEnabled===payload.enabled;
    }
    if (operation === "SET_WIRE_GAIN") {
      const before=pending.wireIdentity;
      const edge=next.graph.edges.find(e=>e.enabled&&e.edgeId===payload.edgeId&&e.from===targetChannelId);
      return !!before && !!edge && edge.primaryOutput===false &&
        edge.to===before.to && edge.tap===before.tap &&
        Number.isFinite(payload.gainLinear)&&Number.isFinite(edge.gainLinear)&&
        Math.abs(edge.gainLinear-payload.gainLinear)<=0.0001;
    }
    if (operation !== "APPLY_WIRE_TRANSACTION" ||
        !Array.isArray(payload.changes) || !payload.changes.length) return false;
    // A structural WIRE transaction must advance the actual PCM graph.
    if (cmp(next.graph.revision,pending.baseGraphRevision)<=0) return false;
    const edges=next.graph.edges.filter(e=>e.enabled);
    return payload.changes.every(edit=>{
      if (!object(edit) || decimal(edit.from)===null ||
          edit.from!==targetChannelId) return false;
      if (edit.op==="REMOVE")
        return decimal(edit.edgeId)!==null &&
          !edges.some(e=>e.edgeId===edit.edgeId);
      if (edit.op==="SET_TAP") {
        const edge=edges.find(e=>e.edgeId===edit.edgeId&&e.from===targetChannelId);
        return decimal(edit.edgeId)!==null && !!edge &&
          edge.primaryOutput===false &&
          (edit.tap==="PRE_FADER"||edit.tap==="POST_FADER") &&
          edge.tap===edit.tap;
      }
      if (edit.op==="CONNECT" || edit.op==="ROUTE_ONLY") {
        if (decimal(edit.to)===null) return false;
        // A cable painted at the requested destination is not proof that
        // the actual native tap/gain was accepted. Verify readback whenever
        // the request names those fields; older v6 CONNECT still works.
        const matching=edges.filter(e=>e.from===edit.from&&e.to===edit.to);
        const effect=matching.some(e=>{
          if (edit.tap!==undefined &&
              e.tap!==edit.tap &&
              !(edit.tap==="POST_FADER"&&e.tap==="POST") &&
              !(edit.tap==="PRE_FADER"&&e.tap==="PRE"))
            return false;
          if (edit.gainLinear!==undefined &&
              (!Number.isFinite(edit.gainLinear) ||
               !Number.isFinite(e.gainLinear) ||
               Math.abs(edit.gainLinear-e.gainLinear)>0.0001))
            return false;
          // v8 makes channel.primaryOutId authoritative; primaryOutput on
          // an edge is optional, but an explicitly false flag cannot be
          // interpreted as a physical primary cable.
          return edit.op!=="ROUTE_ONLY" ||
            (ch.primaryOutId===edit.to&&e.primaryOutput!==false);
        });
        if (!effect) return false;
        if (edit.op==="ROUTE_ONLY") {
          // A unique primary cable must match both the channel-level
          // identity and requested destination. Do not accept a second
          // matching SEND cable as proof of exclusive output.
          if (ch.primaryOutId!==edit.to||matching.length!==1||
              matching[0].primaryOutput===false) return false;
          if (edges.some(e=>e.from===edit.from&&
              e.primaryOutput===true&&e.to!==edit.to))
            return false;
          // Preserve the original primary destination in the pending
          // control transaction so even an old GROUP cable without an edge
          // primaryOutput flag cannot survive Route Only unnoticed.
          if (pending.originalPrimaryOutId &&
              pending.originalPrimaryOutId!==edit.to &&
              edges.some(e=>e.from===edit.from&&
                e.to===pending.originalPrimaryOutId&&
                // A remembered parallel send is NOT the former primary.
                // Unknown/legacy edge identity stays fail-closed.
                e.primaryOutput!==false))
            return false;
          if (edit.to!==next.graph.mainChannelId &&
              edges.some(e=>e.from===edit.from&&
                e.to===next.graph.mainChannelId&&
                // ROUTE_ONLY replaces only the primary. A deliberate
                // independent parallel MAIN send must remain audible.
                e.primaryOutput!==false))
            return false;
        }
        return true;
      }
      if (edit.op==="RETARGET")
        return decimal(edit.edgeId)!==null && decimal(edit.to)!==null &&
          edges.some(e=>e.edgeId===edit.edgeId&&e.from===edit.from&&e.to===edit.to);
      // Gain/tap controls remain unsupported by ENGINE #266's native
      // transaction decoder. Never announce an applied JS-only edit.
      return false;
    });
  }
  // Read-only operator diagnosis from verified Windows callback evidence.
  // UI-only: this routine cannot patch, ARM, create PCM or certify speakers.
  // A novice shortcut is offered only for an empty physical input graph.
  // Do not take over a saved logical input, PLAYER, or authored source.
  function firstSoundPlan(snapshot) {
    const d=snapshot?.device,g=snapshot?.graph,raw=snapshot?.raw;
    if(!snapshot?.admitted||snapshot.retained||raw?.mixerSession?.authority!=="CHANNEL_ID"||
       raw.mixerSession.deviceEpoch!==d?.epoch||!snapshot.hardwareConfiguration||
       raw.operatorOutputMuted===true||raw.secondaryMainOnly===true||
       raw.waitingAsioInputs>0||raw.logicalInputRegistry?.references?.length||
       !Array.isArray(g?.channels)||!Array.isArray(g.edges))return null;
    const inputs=g.channels.filter(ch=>ch.role==="INPUT");
    if(inputs.some(ch=>ch.source!=null))return null;
    const directMain=new Set(g.edges.filter(e=>e.to===g.mainChannelId&&
      e.primaryOutput===true&&e.enabled===true).map(e=>e.from));
    const target=inputs.find(ch=>ch.controls?.mute===false&&directMain.has(ch.id));
    const pins=(d.inputEndpoints||[]).filter(p=>p.active===true&&d.enabledInputs?.includes(p.port)&&
      Number.isInteger(p.port)&&typeof p.label==="string"&&p.label.length>0);
    if(!target||!pins.length)return null;
    return {channelId:target.id,channelName:target.name,epoch:d.epoch,revision:g.revision,
      config:snapshot.hardwareConfiguration,pins:pins.map(p=>({port:p.port,label:p.label}))};
  }

  function diagnoseFirstSound(snapshot, selectedPhysicalPort=null) {
    const d=snapshot?.device, t=snapshot?.telemetry, m4=snapshot?.raw?.legacyM4;
    const opened=Boolean(d && d.sampleRate>0 && Array.isArray(d.enabledOutputs) &&
      d.enabledOutputs.length>=2 && Array.isArray(d.enabledInputs) && d.enabledInputs.length>0);
    const measured=x=>typeof x==="number"&&Number.isFinite(x)&&x>=0&&x<=16?x:null;
    const dbfs=x=>x===null?"N/A":x===0?"−∞ dBFS":
      (20*Math.log10(x)).toFixed(1)+" dBFS";
    const endpoints=Array.isArray(d?.inputEndpoints)?d.inputEndpoints:[];
    const physical=selectedPhysicalPort===null||selectedPhysicalPort===""?
      null:Number(selectedPhysicalPort);
    const pin=Number.isInteger(physical)?endpoints.find(p=>
      p.port===physical && p.active===true && d.enabledInputs.includes(p.port)):null;
    const observed=t?.captureObserved===true;
    const portPeak=pin && Array.isArray(t?.captureCallbackPeaks)?
      measured(t.captureCallbackPeaks[pin.callback]):null;
    const raw=observed?(pin?portPeak:measured(t?.rawCapturePeak)):null;
    const mainObserved=t?.mainPcmObserved===true;
    const main=mainObserved?measured(t?.mainPcmPeak):null;
    let nextAction;
    if(!opened)nextAction="Choose verified input and output devices in Audio Setup.";
    else if(m4?.active!==true)nextAction="Native mixer not ready; check verified DIRECT device.";
    else if(!observed)nextAction="No current-epoch capture callback evidence; verify driver and inputs.";
    else if(pin && raw===0)nextAction="Selected input is silent; check physical source and input gain.";
    else if(raw===null)nextAction="Capture peak unavailable; do not assume an active source.";
    else if(m4.routeAccepted!==true)nextAction="PATCH CH1 to the selected hardware input.";
    else if(d.outputArmed!==true)nextAction=snapshot?.raw?.operatorOutputMuted===true?
      "Emergency stop active; use More > Resume after emergency stop.":
      "Output recovering automatically; if unavailable, check Audio I/O and MAIN route.";
    else if(!mainObserved)nextAction="No MAIN PCM measurement; check routing and output state.";
    else if(main===0)nextAction="MAIN PCM silent; check CH1 fader/MUTE and MASTER MUTE.";
    else nextAction="MAIN PCM measured; verify headphone/speaker sound physically.";
    const label=pin?"IN "+(pin.port+1):"ALL INPUTS";
    return Object.freeze({captureObserved:observed,mainPcmObserved:mainObserved,
      rawPeak:raw,mainPeak:main,rawDbfs:dbfs(raw),mainDbfs:dbfs(main),
      selectedPin:pin?.port??null,nextAction,
      readout:label+" "+dbfs(raw)+" · MAIN PCM "+dbfs(main)});
  }

  // LOGICAL_ONLY has no physical graph/PCM authority. An offline .smix ACK
  // proves a committed revision, not the requested edit. Verify exactly the
  // accepted logical graph effect before revealing a persisted edit in /preview/.
  // Keep this deterministic alongside installed-native readback, outside DOM.
  function logicalReadbackProves(p,graph) {
    if (!object(p) || !object(p.payload) || !object(graph) ||
        !Array.isArray(graph.channels) || !Array.isArray(graph.edges) ||
        decimal(p.ackRevision) === null || graph.revision !== p.ackRevision) return false;
    const v=p.payload, row=graph.channels.find(ch=>ch.id===p.channelId);
    const validId=id=>decimal(id)!==null && id!=="0";
    const catalogs=()=>Array.isArray(row?.inserts) ?
      row.inserts.map(fx=>fx?.catalogId):null;
    const equal=(a,b)=>Array.isArray(a)&&Array.isArray(b)&&
      a.length===b.length&&a.every((value,i)=>value===b[i]);
    switch(p.operation) {
      case "CREATE_CHANNEL":
      case "CREATE_GROUP":
        return validId(p.createdId) && graph.channels.some(ch=>
          ch.id===p.createdId && ch.role===(p.operation==="CREATE_CHANNEL"?"INPUT":"GROUP"));
      case "DELETE_CHANNEL":
      case "REMOVE_BUS":
        return validId(p.channelId) && p.beforeChannelExists===true && !row;
      case "RENAME_CHANNEL":
        return !!row && typeof v.name==="string" && row.name===v.name;
      case "SET_PROCESSING_FORMAT":
        return !!row && ["MONO","STEREO"].includes(v.format) &&
          row.processingFormat===v.format;
      case "SET_INPUT_GAIN":
        return !!row && row.role==="INPUT" && Number.isFinite(v.valueDb) &&
          Number.isFinite(row.controls?.inputGainDb) &&
          Math.abs(row.controls.inputGainDb-v.valueDb)<0.12;
      case "SET_WIRE_SUM_GAIN":
        return !!row && ["INPUT","GROUP","FX_RETURN"].includes(row.role) && Number.isFinite(v.valueDb) &&
          Number.isFinite(row.controls?.wireSumGainDb) &&
          Math.abs(row.controls.wireSumGainDb-v.valueDb)<0.12;
      case "SET_FADER":
        return !!row && Number.isFinite(v.valueDb) &&
          Number.isFinite(row.controls?.faderDb) &&
          Math.abs(row.controls.faderDb-v.valueDb)<0.11;
      case "SET_PAN":
        return !!row && Number.isFinite(v.value) &&
          Number.isFinite(row.controls?.pan) &&
          Math.abs(row.controls.pan-v.value)<0.011;
      case "SET_MUTE":
      case "SET_SOLO":
        return !!row && isBool(v.enabled) &&
          row.controls?.[p.operation==="SET_MUTE"?"mute":"solo"]===v.enabled;
      case "SET_FX_RACK_BYPASS":
        return !!row && isBool(v.enabled) && row.fxRackBypassed===v.enabled;
      case "ADD_FX_REFERENCE":
        // A pre-existing insert of the same catalog is NOT proof that a new
        // slot was appended. The full ordered rack must match the intended edit.
        return !!row && typeof v.catalogId==="string" && v.catalogId.length>0 &&
          Array.isArray(p.beforeInsertCatalogs) &&
          p.beforeInsertCatalogs.length<8 &&
          equal(catalogs(),[...p.beforeInsertCatalogs,v.catalogId]);
      case "REMOVE_FX_REFERENCE": {
        const before=p.beforeInsertCatalogs,slot=v.slot;
        if(!row || !Array.isArray(before) || !Number.isInteger(slot) ||
           slot<0 || slot>=before.length || v.beforeCount!==before.length)return false;
        const expected=before.slice();expected.splice(slot,1);
        return equal(catalogs(),expected);
      }
      case "SET_FX_SLOT_BYPASS":
        return !!row && Number.isInteger(v.slot) && v.slot>=0 &&
          isBool(v.enabled) && row.inserts?.[v.slot]?.bypassed===v.enabled;
      case "SET_SEND_TAP":
      case "SET_SEND_ENABLED": {
        // Both are exact EdgeId edits to an existing PARALLEL send.
        // A successful disk ACK alone is insufficient: only the matching
        // saved graph, with the new tap/enable state, proves the edit.
        // Never mistake old PRE_FX SPLIT_PRE for post-FX PRE_FADER.
        if(!row || !validId(v.edgeId) || !Array.isArray(p.beforeEdges))return false;
        const old=p.beforeEdges.filter(e=>e.edgeId===v.edgeId &&
          e.primaryOutput===false);
        const updated=graph.edges.filter(e=>e.edgeId===v.edgeId &&
          e.from===p.channelId && e.primaryOutput===false && e.enabled!==false);
        if(old.length!==1 || updated.length!==1 ||
           old[0].to!==updated[0].to)return false;
        if(p.operation==="SET_SEND_TAP")
          return ["PRE_FADER","POST_FADER"].includes(v.tap) &&
            (old[0].tapStage||old[0].tap)!==v.tap &&
            updated[0].tapStage===v.tap;
        return isBool(v.enabled) && isBool(old[0].sendEnabled) &&
          old[0].sendEnabled!==v.enabled &&
          updated[0].sendEnabled===v.enabled;
      }
      case "WIRE_TRANSACTION": {
        // The JUCE logical workspace accepts EXACTLY one WIRE edit per
        // transaction. More edits can never be silently accepted from [0].
        if(!row || !Array.isArray(v.changes) || v.changes.length!==1 ||
           !Array.isArray(p.beforeEdges))return false;
        const change=v.changes[0];
        if(!object(change) || !validId(change.from) ||
           change.from!==p.channelId)return false;
        const edges=graph.edges.filter(e=>e.from===p.channelId&&e.enabled!==false);
        const before=p.beforeEdges;
        const present=id=>before.some(e=>e.edgeId===id);
        if(change.op==="REMOVE")
          return validId(change.edgeId) && present(change.edgeId) &&
            !edges.some(e=>e.edgeId===change.edgeId);
        if(change.op==="DISCONNECT")
          return before.some(e=>e.primaryOutput===true) &&
            !edges.some(e=>e.primaryOutput===true) && !row.primaryOutId;
        if(change.op==="ROUTE_ONLY")
          return validId(change.to) && row.primaryOutId===change.to &&
            edges.some(e=>e.to===change.to && e.primaryOutput===true);
        if(change.op==="CONNECT")
          return validId(change.to) && edges.some(e=>
            e.to===change.to && e.primaryOutput===false &&
            validId(e.edgeId) && !present(e.edgeId) &&
            (change.tap===undefined || change.tap===e.tap) &&
            (change.gainLinear===undefined ||
              (Number.isFinite(change.gainLinear) &&
               Number.isFinite(e.gainLinear) &&
               Math.abs(e.gainLinear-change.gainLinear)<=0.0001)));
        if(change.op==="RETARGET")
          return validId(change.edgeId) && validId(change.to) &&
            before.some(e=>e.edgeId===change.edgeId && e.to!==change.to) &&
            edges.some(e=>e.edgeId===change.edgeId && e.to===change.to);
        return false;
      }
      default:return false;
    }
  }

  function createController({emit, onStatus=()=>{}, onAccepted=()=>{}}) {
    if (typeof emit !== "function") throw Error("Native emitter required");
    let current = null, pending = null, lastRequest = 0, lastError = "NATIVE_NOT_SUBSCRIBED";
    let sessionCounter = 0;
    // Per-WebView-session token. Never claim cryptographic authentication.
    const session = "m2-" + Date.now().toString(36) + "-" +
      Math.floor(Math.random() * 0x100000000).toString(36);
    const state = () => ({current, pending:pending && {requestId:pending.requestId,operation:pending.operation},
      lastError, accepted:!!current?.admitted});
    const notify = () => onStatus(state());
    function subscribe() { emit("arMixConsoleSubscribe",{schemaVersion:6,requestId:session+"-sub-"+(++sessionCounter)}); }
    function onSnapshot(raw) {
      const next = normalizeSnapshot(raw);
      if (!next) { lastError="INVALID_NATIVE_SNAPSHOT";notify();return false; }
      if (current) {
        const epochCompare = cmp(next.device.epoch,current.device.epoch);
        if (epochCompare < 0) {lastError="OLD_DEVICE_EPOCH";notify();return false;}
        if (epochCompare === 0 &&
            (cmp(next.graph.revision,current.graph.revision) < 0 ||
             (cmp(next.graph.revision,current.graph.revision) === 0 &&
              cmp(next.graph.rosterRevision,current.graph.rosterRevision) < 0))) {
          lastError="OLD_NATIVE_REVISION";notify();return false;
        }
        // A same-epoch physical pin/format reconfiguration invalidates the
        // current control lease even before ACK. Otherwise an older pending
        // v6 edit would suppress the new admitted snapshot indefinitely.
        // G-MV25: an accepted ChannelId producer can relinquish the PCM
        // graph to M4/UNADMITTED without a hardware or epoch change. A
        // no-ACK local gesture must not freeze that newer authoritative
        // snapshot until its old 8-second timer fires. Fence it at the SAME
        // schema-v6 owner switch as the physical pin/epoch fence, before
        // the pending ACK projection gate below. The native graph revision
        // check above remains monotonic: never resurrect older PCM graphs.
        const authorityChanged=
          next.admitted!==current.admitted ||
          next.raw?.mixerSession?.authority!==
            current.raw?.mixerSession?.authority;
        const physicalStale=epochCompare!==0||next.retained||
          next.hardwareConfiguration!==current.hardwareConfiguration;
        if (pending && (physicalStale||authorityChanged)) {
          pending=null;
          // Preserve existing NEEDS_REMAP/physical-error contracts even if
          // their PCM authority also changes. Distinguish only a pure
          // same-device ChannelId → M4/UNADMITTED owner switch.
          lastError=physicalStale?"STALE_DEVICE":"STALE_NATIVE_AUTHORITY";
        }
      }
      if (pending && !pending.ack) {
        // Unsolicited snapshot must never repaint an un-ACKed local edit.
        notify();return true;
      }
      if (pending && pending.ack && !next.admitted) {
        pending=null;lastError="CALLBACK_UNADMITTED";
        notify();return false;
      }
      if (pending && pending.ack) {
        const ack=pending.ack;
        if (next.device.epoch !== ack.deviceEpoch) {
          pending=null;lastError="STALE_DEVICE";
        } else if (next.graph.revision === ack.graphRevision &&
                   next.graph.rosterRevision === ack.rosterRevision) {
          if (!readbackProvesIntent(next,pending)) {
            pending=null;lastError="ACK_READBACK_EFFECT_MISMATCH";
            subscribe();notify();return false;
          }
          const confirmed=pending;
          pending=null;lastError="ACCEPTED_READBACK";
          current=next;onAccepted(next,confirmed);
          notify();return true;
        } else if (cmp(next.graph.revision,ack.graphRevision) > 0 ||
                   (cmp(next.graph.revision,ack.graphRevision) === 0 &&
                    cmp(next.graph.rosterRevision,ack.rosterRevision) > 0)) {
          pending=null;lastError="CONFLICTING_NATIVE_READBACK";
          subscribe();
        }
      }
      // No local optimistic edit: existing accepted UI projection is frozen
      // until its exact matching native readback arrives.
      if (!pending || !pending.ack) {
        current=next;onAccepted(next);
      }
      notify();return true;
    }
    // One negative-intent path: a rejected UI command MUST update the same
    // observable controller status as a native rejection, without emitting
    // an intent, changing the accepted graph, or stranding a pending request.
    const refuseIntent = error => {
      lastError=error;notify();
      return {ok:false,error};
    };
    function intent(operation,targetChannelId,payload={}) {
      if (!current || !current.admitted || !current.graph.capabilities.supportedOps.includes(operation)) {
        return refuseIntent("UNSUPPORTED_OR_UNADMITTED");
      }
      if (pending) return refuseIntent("PENDING_NATIVE_ACK");
      if (decimal(targetChannelId) === null || !current.graph.channels.some(ch => ch.id === targetChannelId)) {
        return refuseIntent("UNKNOWN_CHANNEL_ID");
      }
      if (!object(payload)) return refuseIntent("INVALID_PAYLOAD");
      let fxExpected=null;
      if(operation==="CONVERT_REVERB_RETURN"){
        const source=current.graph.channels.find(c=>c.id===targetChannelId);
        const slot=payload.slot,token=payload.instanceId;
        if(source?.role!=="INPUT"||source.primaryOutId!==current.graph.mainChannelId||
           !Number.isInteger(slot)||slot<0||slot+1!==source.inserts?.length||
           source.inserts[slot]?.id!==token||
           source.inserts[slot]?.catalogId!=="armix.builtin.vocal-reverb.v1")
          return refuseIntent("DMX_CONVERSION_REQUIRES_LAST_REVERB_DIRECT_MAIN_INPUT");
        fxExpected={source:source.inserts.slice(0,slot).map(x=>x.id),
          previous:current.graph.channels.map(c=>c.id)};
      }
      if(operation==="REMOVE_INSERT"||operation==="MOVE_INSERT") {
        const source=current.graph.channels.find(c=>c.id===targetChannelId);
        const slot=payload.slot,token=payload.instanceId;
        if(!Number.isInteger(slot)||slot<0||source?.inserts?.[slot]?.id!==token)
          return refuseIntent("STALE_FX_INSTANCE");
        const ids=source.inserts.map(x=>x.id);ids.splice(slot,1);
        fxExpected={source:ids};
        if(operation==="MOVE_INSERT") {
          const dest=current.graph.channels.find(c=>c.id===payload.toChannelId),pos=payload.toSlot;
          const same=dest?.id===targetChannelId;
          const list=same?ids:dest?.inserts?.map(x=>x.id);
          if(!dest||!["INPUT","GROUP","FX_RETURN","MAIN"].includes(dest.role)||
             !list||list.length>=8||!Number.isInteger(pos)||pos<0||pos>list.length)
            return refuseIntent("INVALID_FX_DESTINATION");
          list.splice(pos,0,token);
          if(!same){fxExpected.to=dest.id;fxExpected.destination=list;}
        }
      }
      let wireIdentity=null;
      if(operation==="SET_WIRE_GAIN"||operation==="SET_SEND_ENABLED") {
        const edge=current.graph.edges.find(e=>e.enabled&&e.edgeId===payload.edgeId&&e.from===targetChannelId);
        if(!edge||edge.primaryOutput!==false||
           (operation==="SET_WIRE_GAIN"&&(!Number.isFinite(payload.gainLinear)||
            payload.gainLinear<0||payload.gainLinear>4))||
           (operation==="SET_SEND_ENABLED"&&typeof payload.enabled!=="boolean")) {
          return refuseIntent("INVALID_WIRE_GAIN_TARGET");
        }
        wireIdentity={to:edge.to,tap:edge.tap};
      }
      const requestId=session+"-"+(++lastRequest);
      const msg={requestId,deviceEpoch:current.device.epoch,
        baseGraphRevision:current.graph.revision,
        baseRosterRevision:current.graph.rosterRevision,
        operation,targetChannelId,payload};
      pending={requestId,operation,targetChannelId,payload,wireIdentity,fxExpected,
        originalPrimaryOutId:current.graph.channels.find(
          ch=>ch.id===targetChannelId)?.primaryOutId||null,
        deviceEpoch:msg.deviceEpoch,baseGraphRevision:msg.baseGraphRevision,
        baseRosterRevision:msg.baseRosterRevision,ack:null};
      lastError="PENDING_NATIVE_ACK";
      emit("arMixConsoleIntent",msg);notify();return {ok:true,requestId};
    }
    function onAck(raw) {
      // G-MV21: the first correlated native ACK owns this request's proof
      // lease. A duplicate positive, negative or malformed ACK cannot
      // overwrite its graph revision, demand another subscribe, or reject
      // a command already waiting for independent installed PCM readback.
      if (pending?.ack) return false;
      if (!pending || !fields(raw,["requestId","status","deviceEpoch","graphRevision","rosterRevision","errorCode"]) ||
          typeof raw.requestId !== "string" || raw.requestId !== pending.requestId) return false;
      if ([raw.deviceEpoch,raw.graphRevision,raw.rosterRevision].some(x=>decimal(x)===null)) {
        pending=null;lastError="INVALID_NATIVE_ACK";notify();return false;
      }
      if (raw.deviceEpoch !== pending.deviceEpoch ||
          !current || raw.deviceEpoch !== current.device.epoch) {
        pending=null;lastError="STALE_DEVICE";subscribe();notify();return false;
      }
      if (raw.status !== "ACCEPTED" || raw.errorCode !== "NONE") {
        lastError=typeof raw.errorCode==="string"?raw.errorCode:"NATIVE_REJECTED";
        pending=null;subscribe();notify();return false;
      }
      if (cmp(raw.graphRevision,pending.baseGraphRevision) < 0 ||
          cmp(raw.rosterRevision,pending.baseRosterRevision) < 0) {
        pending=null;lastError="STALE_NATIVE_ACK";subscribe();notify();return false;
      }
      pending.ack={deviceEpoch:raw.deviceEpoch,graphRevision:raw.graphRevision,
        rosterRevision:raw.rosterRevision};
      lastError="PENDING_ACCEPTED_SNAPSHOT";notify();
      // Reject ACK-only "success": explicitly request a LATER independent readback.
      subscribe();
      return true;
    }
    function expire(requestId) {
      if (!pending || pending.requestId!==requestId) return false;
      pending=null;lastError="NATIVE_ACK_OR_READBACK_TIMEOUT";
      subscribe();notify();return true;
    }
    return Object.freeze({subscribe,onSnapshot,onAck,intent,state,expire});
  }

  // Native health strip, explicitly separated from process/DSP authority.
  // Data arrives on the ALREADY EXISTING 12 Hz arMixStatus event, tagged
  // only once per second. Neither this pure function nor its caller polls,
  // schedules timers or samples CPU/memory from the WebView.
  function footerHealthFromLive(live) {
    if(!object(live)||!Number.isSafeInteger(live.footerHealthSequence)||
       live.footerHealthSequence<=0)return null;
    const n=value=>typeof value==="number"&&Number.isFinite(value)?value:null;
    const whole=value=>{const v=n(value);return v!==null&&Number.isSafeInteger(v)&&v>=0?v:null;};
    const rate=n(live.rate),period=n(live.periodUs);
    const inFrames=whole(live.driverReportedInputLatencyFrames);
    const outFrames=whole(live.driverReportedOutputLatencyFrames);
    const fxFrames=whole(live.latencySamples);
    // A driver latency *hint* can include its own buffering; never add the
    // callback buffer again or label this measured acoustic roundtrip.
    const oneClock=live.connected===true &&
      live.directRouteAuthorized===true && live.adaptiveClockActive===false;
    const latencyFrames=oneClock && rate!==null && rate>0 &&
      inFrames!==null && outFrames!==null && fxFrames!==null
        ? inFrames+outFrames+fxFrames : null;
    const latency=latencyFrames!==null && Number.isSafeInteger(latencyFrames)
      ? "LAT ~"+(1000*latencyFrames/rate).toFixed(1)+" ms":"LAT —";
    const cpu=n(live.footerProcessCpuPercent);
    const working=whole(live.footerProcessWorkingSetMiB);
    const cpuLabel=cpu!==null&&cpu>=0&&cpu<=100
      ? "CPU App "+cpu.toFixed(1)+"%":"CPU App —";
    const ramLabel=working!==null
      ? "RAM "+working+" MiB":"RAM —";
    // Last completed callback's duration / actual period, NOT CPU App and
    // NOT p95, OS DPC load, system CPU or actual driver XRUN.
    const lastCallback=n(live.callbackDurationUs);
    const rtLabel=oneClock && period!==null && period>0 &&
      lastCallback!==null && lastCallback>=0
      ? "RT last "+Math.min(999,Math.round(100*lastCallback/period))+"%"
      : "RT —";
    return Object.freeze({
      sequence:live.footerHealthSequence,
      deviceGeneration:whole(live.deviceGeneration)===null?
        null:String(live.deviceGeneration),
      // Device starts and accepted routing epochs diverge after STOP.
      routingEpoch:whole(live.dryRoutingLab?.epoch)===null?
        null:String(live.dryRoutingLab.epoch),
      latency,performance:cpuLabel+" · "+rtLabel+" · "+ramLabel,
      // The presentation colors classify *only* the observable signals:
      // CPU App = process CPU on all logical cores; RT = last callback
      // budget. A deadline miss is NOT itself a confirmed hardware XRUN.
      metrics:Object.freeze({
        cpu:Object.freeze({label:cpuLabel,tone:cpu===null||cpu<0||cpu>100?"unknown":cpu>=90?"critical":cpu>=75?"attention":"normal"}),
        rt:Object.freeze({label:rtLabel,tone:!oneClock||period===null||period<=0||
          lastCallback===null||lastCallback<0?"unknown":
          lastCallback>=period?"critical":lastCallback>=period*0.8?"attention":"normal"}),
        ram:Object.freeze({label:ramLabel,tone:working===null?"unknown":"neutral"})
      }),
      provenance:"DRIVER_HINT_NOT_LOOPBACK__PROCESS_CPU_ALL_CORES__LAST_CALLBACK_BUDGET__PROCESS_WORKING_SET"
    });
  }

  // M4 Smart Performance Assistant. CONTROL/JS presentation only. The 13
  // histogram buckets are cumulative, so calculate an *epoch-fenced* 1 Hz
  // difference, never describe a lifetime peak as current DSP usage.
  // This is host-render evidence, NEVER an ASIO/driver xrun measurement.
  function smartPerformanceView(live,accepted,prior=null) {
    const integer=n=>Number.isSafeInteger(n)&&n>=0?n:null;
    const finite=n=>typeof n==="number"&&Number.isFinite(n)?n:null;
    const unavailable=reason=>Object.freeze({
      state:"UNAVAILABLE",reason,headroom:null,callbacks:null,over:null,
      latency:null,fxPeak:null,reviews:[],recommendations:[],cursor:null,
      correlation:null
    });
    if(!accepted?.admitted||accepted.retained||!accepted.device ||
       !live||live.connected!==true||live.directRouteAuthorized!==true||
       live.adaptiveClockActive!==false||
       integer(live.dryRoutingLab?.epoch)===null||
       accepted.device.epoch!==String(live.dryRoutingLab.epoch)||
       accepted.device.sampleRate!==live.rate ||
       accepted.device.bufferFrames!==live.buffer ||
       !accepted.graph?.revision ||
       accepted.graph.revision!==live.dryRoutingLab?.nativeBus?.graphRevision ||
       !(finite(live.periodUs)>0))
      return unavailable("No matching verified native render / device epoch");
    const seq=integer(live.footerHealthSequence);
    const counts=live.callbackBudgetBuckets;
    if(!seq||!Array.isArray(counts)||counts.length!==13||
       !counts.every(n=>integer(n)!==null))
      return unavailable("Waiting for the native 1 Hz callback histogram");
    const epoch=accepted.device.epoch,graphRevision=accepted.graph.revision;
    const armed=accepted.device.outputArmed===true;
    const token=armed&&decimal(live.performanceRenderToken)!==null&&
      live.performanceRenderToken!=="0"&&live.performanceRenderEpoch===epoch&&
      live.performanceRenderRevision===graphRevision?live.performanceRenderToken:null;
    const cursor=Object.freeze({epoch,graphRevision,seq,token,armed,buckets:Object.freeze([...counts])});
    const same=token!==null&&prior?.token===token&&prior.armed===true&&
      prior.epoch===epoch&&prior.graphRevision===graphRevision&&
      integer(prior.seq)!==null&&prior.seq+1===seq&&
      Array.isArray(prior.buckets)&&prior.buckets.length===13&&
      prior.buckets.every(n=>integer(n)!==null);
    const delta=same?counts.map((n,i)=>n-prior.buckets[i]):null;
    const valid=delta!==null&&delta.every(n=>integer(n)!==null);
    const total=valid?delta.reduce((sum,n)=>sum+n,0):null;
    const callbacks=integer(total);
    const enough=callbacks!==null&&callbacks>=64;
    const buckets=Object.freeze([1,2,4,8,12,20,30,40,55,70,85,100,null]);
    const pct=(percentage)=>percentage===null?">100%":percentage+"%";
    const p99=()=>{
      if(!enough)return null;
      const target=Math.ceil(callbacks*.99);
      let scanned=0;
      for(let i=0;i<delta.length;i++){
        scanned+=delta[i];
        if(scanned>=target)return buckets[i];
      }
      return null;
    };
    const ceiling=p99();
    const over=callbacks!==null?delta[12]:null;
    const headroom=enough?Object.freeze({
      p99:pct(ceiling),margin:ceiling===null?"none verified":(100-ceiling)+"%",
      confidence:"CONTINUOUS_ARMED_NATIVE_RENDER_NOMINAL_1HZ_DELTA_P99_BUCKET_CEILING_NOT_XRUN"
    }):null;
    const pairCounts=["renderPressureWithinThenLate","renderPressureOverThenLate",
       "renderPressureWithinThenOnTime","renderPressureOverThenOnTime"].map(k=>integer(live[k]));
    const correlation=token!==null&&live.renderPressureWindowValid===true&&
      pairCounts.every(n=>n!==null&&n<=64)&&pairCounts.reduce((a,b)=>a+b,0)===64?
      Object.freeze({withinLate:live.renderPressureWithinThenLate,
        overLate:live.renderPressureOverThenLate,
        provenance:"LAST_COMPLETE_64_PAIRS_MAY_BE_OLD_NOT_CAUSAL"}):null;
    const peakRatio=finite(live.nativeFxWorkloadWorstPermille);
    const fxName=typeof live.nativeFxWorkloadWorstName==="string"?
      live.nativeFxWorkloadWorstName.trim().slice(0,90):"";
    const fxPeak=peakRatio!==null&&peakRatio>=0&&fxName?Object.freeze({
      name:fxName,percent:(peakRatio/10).toFixed(1)+"%",
      sampled:live.nativeFxWorkloadWorstSampledOnly===true,
      provenance:"MAX_OBSERVED_INVOCATION_RATIO_LIFETIME_NOT_CURRENT_LOAD"
    }):null;
    const actionText=Object.freeze({
      REBUILD_LATENCY_WHILE_DISARMED:"Latency changed; inspect with outputs disarmed",
      REVIEW_LATENT_BYPASS:"Bypassed insert still retains alignment latency",
      REVIEW_SAMPLED_SILENT_COST:"Review cost during measured silence",
      REVIEW_BLOCK_BUDGET:"Review processor workload and preset"
    });
    const reviews=Array.isArray(live.nativeFxWorkloadReviews)?
      live.nativeFxWorkloadReviews.filter(r=>r&&
        typeof r.name==="string"&&typeof r.recommendation==="string"&&
        Object.hasOwn(actionText,r.recommendation)).slice(0,3).map(r=>Object.freeze({
          name:r.name.slice(0,90),channel:String(r.channel??"").slice(0,30),
          action:actionText[r.recommendation],
          window:r.windowValid===true&&finite(r.windowMeanPermille)!==null?
            (r.windowMeanPermille/10).toFixed(1)+"%":"—",
          sampled:r.sampledOnly===true
        })):[];

    const rate=finite(live.rate);
    const ms=samples=>integer(samples)!==null&&rate>0?
      (1000*samples/rate).toFixed(2)+" ms":null;
    const latency=live.nativeLatencyPlanValid===true?Object.freeze({
      main:ms(live.nativeLatencyPlanAlignedSamples),
      monitor:live.nativeLatencyPlanHasMonitor===true?
        ms(live.nativeLatencyPlanMonitorOutputSamples):null,
      policy:live.nativeLatencyPlanMonitorPolicy==="INDEPENDENT_MONITOR"?
        "Independent singer monitor":"Globally aligned",
      largest:ms(live.nativeMaxInsertLatencySamples),
      largestName:typeof live.nativeMaxInsertLatencyName==="string"?
        live.nativeMaxInsertLatencyName.slice(0,90):"",
      provenance:"DECLARED_FX_GRAPH_SAMPLES_NOT_DRIVER_OR_ACOUSTIC_ROUNDTRIP"
    }):null;
    const recommendations=[];
    let state=armed?"COLLECTING":"MUTED";
    if(!armed)recommendations.push("Outputs disarmed. Inspect routing before enabling monitoring.");
    else if(!enough)recommendations.push("Collect at least 64 render callbacks within one uninterrupted nominal 1-second interval.");
    else if(over>0)recommendations.push("Render deadline exceeded in "+over+
      "/"+callbacks+" measured blocks. Review FX workload before any operator-approved change.");
    else if(ceiling===null||ceiling>=85)
      recommendations.push("Limited measured render headroom. Review FX workload and device scheduling.");
    else recommendations.push("No over-budget render in this observed interval; continue monitoring.");

    if(enough&&correlation?.withinLate>0)
      recommendations.push("Late callback starts also followed in-budget processing; investigate driver / OS scheduling (not a confirmed cause).");
    else if(enough&&correlation?.overLate>0)
      recommendations.push("Some late starts followed an over-budget render; investigate DSP pressure (not a confirmed xrun).");
    if(integer(live.nativeLatentBypassRequestedCount)>0)
      recommendations.push("Bypassing latent FX retains PDC. To remove latency, save then deactivate explicitly while outputs are disarmed.");
    else if(latency?.largest&&latency.largestName)
      recommendations.push("Largest declared insert latency: "+latency.largestName+
        " ("+latency.largest+"); only a disarmed graph edit can remove its PDC.");
    if(fxPeak&&enough&&peakRatio>=700)
      recommendations.push("Inspect "+fxPeak.name+"; peak workload is historical"+
        (fxPeak.sampled?" and sampled":"")+", not proof of present crackle.");
    if(armed&&enough)state=over>0||ceiling===null?"PRESSURE":
      ceiling>=85?"TIGHT":"OBSERVED_MARGIN";
    return Object.freeze({state,reason:armed?"Native render evidence":"Outputs disarmed",
      headroom,callbacks,over,latency,fxPeak,reviews,correlation,
      recommendations:recommendations.slice(0,5),cursor});
  }

  return Object.freeze({smartPerformanceView,footerHealthFromLive,decimal,cmp,normalizeSnapshot,
    nativeHardwareConfiguration,selectNativeReadModel,readbackProvesIntent,
    logicalReadbackProves,firstSoundPlan,diagnoseFirstSound,createController,
    validPresentationName,validPresentationColor,validPresentationIcon,
    operatorMessage,physicalOutputOverview,secondaryMixMode,secondaryMonitorStarter,secondaryOutputSelection,secondaryOutputView,secondaryOutputRow,monitorTimingView});
});
