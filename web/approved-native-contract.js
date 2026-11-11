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
    const internalCode=/\b[A-Z][A-Z0-9]{2,}(?:_[A-Z0-9]+)+\b/.test(raw);
    if(internalCode ||
       /\b(FAT|PCM|ACK|readback|admission|admitted|graph|roster|epoch|revision|M[0-9]|JUCE|callback|worker|fixture|reference|mockup|simulated|demo|legacy|native|authoritative)\b/i.test(raw)){
      if(/\b(device|driver|audio interface)\b|DEVICE_/i.test(raw) &&
         /\b(disconnect|unavailable|not ready|not open|not found|failed)\b|_(FAILED|ERROR|DISCONNECTED|UNAVAILABLE)/i.test(raw))
        return "Audio device unavailable · Open Audio I/O";
      if(/\b(WIRE|route|routing)\b/i.test(raw)){
        if(/\b(reject|fail|error|timed out|not applied|unavailable)\b/i.test(raw))
          return "Routing not changed · Check the connection and try again";
        if(/\b(pending|await|verif|applying|completing)\w*/i.test(raw))
          return "Updating channel routing…";
        return "Channel routing updated";
      }
      if(/\b(VST3|plugin|effect|FX)\b/i.test(raw)){
        if(/\b(reject|fail|error|timed out)\b/i.test(raw))
          return "Effect change failed · Previous settings kept";
        if(/\b(pending|await|verif|loading|applying)\w*/i.test(raw))
          return "Applying effect…";
        return "Effect settings updated";
      }
      if(/\b(error|failed|rejected|timed out|invalid|unavailable|not applied)\b|_(FAILED|ERROR|REJECTED|STALE|INVALID|TIMEOUT|MISSING)(?:\b|_)/i.test(raw))
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
        !Number.isFinite(e.gainLinear) || e.gainLinear < 0)) return null;
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
      productLive:admitted && raw.productionCallbackLive,
      identity: d.epoch + ":" + g.revision + ":" + g.rosterRevision };
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
    else if(d.outputArmed!==true)nextAction="Patch accepted; lower volume then ARM output.";
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
        if (pending && (epochCompare !== 0 || next.retained)) {
          pending=null;lastError="STALE_DEVICE";
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
      if(operation==="SET_WIRE_GAIN") {
        const edge=current.graph.edges.find(e=>e.enabled&&e.edgeId===payload.edgeId&&e.from===targetChannelId);
        if(!edge||edge.primaryOutput!==false||!Number.isFinite(payload.gainLinear)||
           payload.gainLinear<0||payload.gainLinear>4) {
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
  return Object.freeze({decimal,cmp,normalizeSnapshot,readbackProvesIntent,logicalReadbackProves,diagnoseFirstSound,createController,validPresentationName,validPresentationColor,validPresentationIcon,operatorMessage});
});
