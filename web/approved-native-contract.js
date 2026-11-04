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
    const admitted = caps.callbackOwnsChannelIds === true &&
      cmp(d.epoch,"0") > 0 && cmp(g.revision,"0") > 0 &&
      d.sampleRate > 0 && d.bufferFrames > 0 &&
      d.enabledOutputs.length > 0 && d.outputEndpoints.length > 0 &&
      Array.isArray(g.edges) && g.channels.length > 0 && mainCount === 1 &&
      typeof g.mainChannelId === "string" &&
      g.channels.some(ch => ch.id === g.mainChannelId && ch.role === "MAIN") &&
      g.channels.every(ch => fields(ch,["controls","inserts","sends"]) &&
        fields(ch.controls,["faderDb","pan","mute","solo"]) &&
        Number.isFinite(ch.controls.faderDb) &&
        Number.isFinite(ch.controls.pan) && isBool(ch.controls.mute) &&
        isBool(ch.controls.solo) && Array.isArray(ch.inserts) &&
        Array.isArray(ch.sends));
    // A producer may stage PCM without claiming full release/UAT. A producer
    // must NEVER claim product LIVE without an admitted PCM graph.
    if (raw.productionCallbackLive && !admitted) return null;
    if (caps.callbackOwnsChannelIds && !admitted) return null;
    return { raw, device:d, graph:g, telemetry:t, admitted,
      productLive:admitted && raw.productionCallbackLive,
      identity: d.epoch + ":" + g.revision + ":" + g.rosterRevision };
  }
  // ACK alone is never physical evidence. Readback must prove that a known
  // control/route actually reached the accepted native graph. No invented UI
  // updates and no optimistic fader/edge commits.
  function readbackProvesIntent(next, pending) {
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
          return edit.op!=="ROUTE_ONLY" || e.primaryOutput===true;
        });
        if (!effect) return false;
        if (edit.op==="ROUTE_ONLY") {
          // Route Only must REMOVE the previous primary dry path. An ACK
          // with both new BUS and original MAIN remains double-dry even
          // when the graph revision advanced.
          if (ch.primaryOutId!==edit.to) return false;
          if (edges.some(e=>e.from===edit.from&&e.primaryOutput===true&&e.to!==edit.to))
            return false;
          if (edit.to!==next.graph.mainChannelId &&
              edges.some(e=>e.from===edit.from&&e.to===next.graph.mainChannelId))
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
        if (pending && epochCompare !== 0) {
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
          pending=null;lastError="ACCEPTED_READBACK";
          current=next;onAccepted(next);
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
    function intent(operation,targetChannelId,payload={}) {
      if (!current || !current.admitted || !current.graph.capabilities.supportedOps.includes(operation)) {
        lastError="UNSUPPORTED_OR_UNADMITTED";notify();return {ok:false,error:lastError};
      }
      if (pending) {lastError="PENDING_NATIVE_ACK";notify();return {ok:false,error:lastError};}
      if (decimal(targetChannelId) === null || !current.graph.channels.some(ch => ch.id === targetChannelId)) {
        lastError="UNKNOWN_CHANNEL_ID";notify();return {ok:false,error:lastError};
      }
      if (!object(payload)) {lastError="INVALID_PAYLOAD";notify();return {ok:false,error:lastError};}
      const requestId=session+"-"+(++lastRequest);
      const msg={requestId,deviceEpoch:current.device.epoch,
        baseGraphRevision:current.graph.revision,
        baseRosterRevision:current.graph.rosterRevision,
        operation,targetChannelId,payload};
      pending={requestId,operation,targetChannelId,payload,
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
  return Object.freeze({decimal,cmp,normalizeSnapshot,readbackProvesIntent,createController});
});
