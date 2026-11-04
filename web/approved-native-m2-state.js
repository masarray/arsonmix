// SPDX-License-Identifier: AGPL-3.0-only
// M2: the one Pages GUI's native-accepted console transaction controller.
// Only control-plane. Never an audio engine, browser-generated ChannelId,
// hardware pin source, or optimistic success indicator.
(function (root, factory) {
  const exported = factory();
  if (typeof module !== "undefined" && module.exports) module.exports = exported;
  if (root) root.ArSonMixNativeM2 = exported;
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  "use strict";
  const MAX = (1n << 64n) - 1n;
  const ASSERT = (ok, why) => { if (!ok) throw Error("M2_" + why); };
  const DECIMAL = /^(0|[1-9][0-9]*)$/;
  const OPERATIONS = /^[A-Z_]+$/;
  const REQUIRED_CAPS = [
    "callbackOwnsChannelIds", "fullVendorFxMigration", "showAndOutputParity",
    "physicalCaptureMapped", "dynamicChannels", "groups", "fxReturns",
    "monitor", "sendsOnFaders"
  ];
  function uint64(value, positive = false) {
    ASSERT(typeof value === "string" && DECIMAL.test(value), "BAD_UINT64");
    const number = BigInt(value);
    ASSERT(number <= MAX && (!positive || number > 0n), "UINT64_RANGE");
    return number;
  }
  function fields(obj, names) {
    ASSERT(obj && typeof obj === "object" && !Array.isArray(obj), "BAD_OBJECT");
    for (const key of names) ASSERT(Object.prototype.hasOwnProperty.call(obj, key), "MISSING_" + key);
  }
  function validateSnapshot(input) {
    fields(input, ["schemaVersion", "productionCallbackLive", "device", "graph", "telemetry"]);
    ASSERT(input.schemaVersion === 6 && typeof input.productionCallbackLive === "boolean", "VERSION");
    const d = input.device, g = input.graph, t = input.telemetry;
    fields(d, ["epoch", "backend", "captureDevice", "playbackDevice", "inputEndpoints",
      "outputEndpoints", "enabledInputs", "enabledOutputs", "sampleRate", "bufferFrames", "outputArmed"]);
    fields(g, ["revision", "rosterRevision", "channels", "destinations", "capabilities"]);
    fields(t, ["rawCapturePresent", "rawCapturePeak", "mainPcmPresent", "monitorPcmPresent"]);
    uint64(d.epoch, true);
    uint64(g.revision);
    uint64(g.rosterRevision);
    ASSERT(typeof d.outputArmed === "boolean", "OUTPUT_ARM_FLAG");
    ASSERT(typeof t.rawCapturePresent === "boolean" &&
      typeof t.mainPcmPresent === "boolean" &&
      typeof t.monitorPcmPresent === "boolean" &&
      typeof t.rawCapturePeak === "number" && Number.isFinite(t.rawCapturePeak) &&
      t.rawCapturePeak >= 0, "TELEMETRY");
    for (const key of ["enabledInputs", "enabledOutputs"]) {
      ASSERT(Array.isArray(d[key]), "PORT_MASK");
      ASSERT(d[key].every(x => Number.isSafeInteger(x) && x >= 0 && x <= 255), "PORT");
      ASSERT(new Set(d[key]).size === d[key].length, "DUPLICATE_PIN");
    }
    for (const [kind, items, pins] of [
      ["in", d.inputEndpoints, d.enabledInputs],
      ["out", d.outputEndpoints, d.enabledOutputs]
    ]) {
      ASSERT(Array.isArray(items), "ENDPOINTS");
      const ids = new Set(), callbacks = new Set();
      for (const e of items) {
        ASSERT(e && Number.isSafeInteger(e.port) && pins.includes(e.port) &&
          e.id === kind + ":" + e.port && e.active === true &&
          Number.isSafeInteger(e.callback) && e.callback >= 0 &&
          typeof e.label === "string" && e.label.length > 0, "ENDPOINT");
        ASSERT(!ids.has(e.id) && !callbacks.has(e.callback), "DUPLICATE_ENDPOINT");
        ids.add(e.id); callbacks.add(e.callback);
      }
    }
    ASSERT(Array.isArray(g.channels) && Array.isArray(g.destinations), "GRAPH_ARRAY");
    fields(g.capabilities, [...REQUIRED_CAPS, "supportedOps"]);
    for (const key of REQUIRED_CAPS) ASSERT(typeof g.capabilities[key] === "boolean", "CAPABILITY");
    ASSERT(Array.isArray(g.capabilities.supportedOps) &&
      g.capabilities.supportedOps.every(op => typeof op === "string" && OPERATIONS.test(op)) &&
      new Set(g.capabilities.supportedOps).size === g.capabilities.supportedOps.length, "OPS");
    const ids = new Set();
    for (const channel of g.channels) {
      ASSERT(channel && typeof channel === "object", "CHANNEL");
      uint64(channel.id, true);
      ASSERT(!ids.has(channel.id), "DUPLICATE_CHANNEL");
      ids.add(channel.id);
    }
    for (const dest of g.destinations) {
      uint64(dest.id, true);
      ASSERT(ids.has(dest.id), "UNKNOWN_DESTINATION");
    }
    // A non-LIVE producer can expose diagnostic fields, but these cannot
    // authorize operator actions or claim the callback owns the graph.
    if (input.productionCallbackLive) {
      ASSERT(["callbackOwnsChannelIds", "fullVendorFxMigration",
        "showAndOutputParity", "physicalCaptureMapped"].every(k => g.capabilities[k]),
        "FALSE_LIVE_CLAIM");
    }
    return JSON.parse(JSON.stringify(input)); // no external mutable reference
  }
  function validRequestId(s) {
    return typeof s === "string" && s.length <= 128 &&
      /^[^\s:]+:[1-9][0-9]*$/.test(s);
  }
  function matching(ack, snapshot) {
    return snapshot && snapshot.device.epoch === ack.deviceEpoch &&
      snapshot.graph.revision === ack.graphRevision &&
      snapshot.graph.rosterRevision === ack.rosterRevision;
  }
  function createController({emit, notify = () => {}, session = "gui-m2"} = {}) {
    ASSERT(typeof emit === "function" && validRequestId(session + ":1"), "SESSION");
    let accepted = null, pending = null, candidate = null, sequence = 0n;
    let state = "WAITING_NATIVE_SNAPSHOT";
    function status() { return state; }
    function snapshot() { return accepted ? JSON.parse(JSON.stringify(accepted)) : null; }
    function pendingRequest() { return pending ? {...pending.intent} : null; }
    function live() { return Boolean(accepted?.productionCallbackLive); }
    function publish(s) {
      accepted = s;
      state = s.productionCallbackLive ? "ACCEPTED_NATIVE_CALLBACK" : "M2_NATIVE_READBACK_ONLY";
      notify(state, snapshot());
    }
    function receiveSnapshot(raw) {
      let next;
      try { next = validateSnapshot(raw); }
      catch (e) { state = "INVALID_NATIVE_SNAPSHOT"; notify(state, e.message); return false; }
      if (accepted) {
        if (uint64(next.device.epoch) < uint64(accepted.device.epoch)) return false;
        if (next.device.epoch === accepted.device.epoch &&
          uint64(next.graph.revision) < uint64(accepted.graph.revision)) return false;
      }
      if (pending) {
        if (accepted && next.device.epoch !== accepted.device.epoch) {
          // Real hardware changed: invalidate all uncommitted UI intentions.
          pending = null; candidate = null; publish(next);
          notify("DEVICE_CHANGED_INTENT_CANCELLED"); return true;
        }
        if (pending.ack) {
          if (!matching(pending.ack, next)) return false;
          pending = null; candidate = null; publish(next); return true;
        }
        candidate = next; // Native may deliver the snapshot before its ACK.
        return false;
      }
      publish(next); return true;
    }
    function request(operation, targetChannelId, payload = {}) {
      if (!accepted || pending || !live() ||
          !accepted.graph.capabilities.supportedOps.includes(operation)) {
        notify("M2_OPERATION_NOT_NATIVE_ADMITTED", operation); return null;
      }
      if (typeof operation !== "string" || !OPERATIONS.test(operation)) return null;
      if (targetChannelId !== null && targetChannelId !== undefined) {
        try { uint64(targetChannelId, true); }
        catch (_) { return null; }
        if (!accepted.graph.channels.some(x => x.id === targetChannelId)) return null;
      }
      if (!payload || typeof payload !== "object" || Array.isArray(payload)) return null;
      if (operation === "SET_SOURCE" &&
          !accepted.device.inputEndpoints.some(x => x.id === payload.portId && x.active)) return null;
      ++sequence;
      const id = session + ":" + sequence;
      const intent = {
        requestId: id,
        deviceEpoch: accepted.device.epoch,
        baseGraphRevision: accepted.graph.revision,
        baseRosterRevision: accepted.graph.rosterRevision,
        operation,
        targetChannelId: targetChannelId ?? null,
        payload: JSON.parse(JSON.stringify(payload))
      };
      pending = {intent, ack:null}; candidate = null;
      state = "PENDING_NATIVE_ACK";
      try { emit("arMixConsoleIntent", intent); }
      catch (e) {
        pending = null; state = "NATIVE_TRANSPORT_FAILED";
        notify(state, e.message); return null;
      }
      notify(state, intent);
      return id;
    }
    function receiveAck(raw) {
      if (!pending || !raw || raw.requestId !== pending.intent.requestId)
        return false;
      let epoch,rev,roster;
      try {
        epoch=uint64(raw.deviceEpoch,true);
        rev=uint64(raw.graphRevision);
        roster=uint64(raw.rosterRevision);
      } catch (_) { return false; }
      if (raw.deviceEpoch !== pending.intent.deviceEpoch ||
          !["ACCEPTED","REJECTED","REQUIRES_DEVICE_CHANGE"].includes(raw.status))
        return false;
      if (raw.status !== "ACCEPTED") {
        // Rejection, stale, or unsupported may NEVER mutate accepted UI graph.
        pending = null; candidate = null;
        state = "NATIVE_REJECTED";
        notify(state, typeof raw.errorCode === "string" ? raw.errorCode : "NO_REASON");
        return true;
      }
      if (accepted && epoch === uint64(accepted.device.epoch) &&
          rev < uint64(accepted.graph.revision)) return false;
      if (roster < 0n) return false;
      pending.ack = {...raw};
      state = "AWAITING_MATCHED_NATIVE_SNAPSHOT";
      notify(state, raw);
      if (candidate && matching(raw, candidate)) {
        const confirmed = candidate;
        candidate = null; pending = null; publish(confirmed);
      }
      return true;
    }
    function disconnect() {
      accepted = null; pending = null; candidate = null;
      state = "NATIVE_DISCONNECTED"; notify(state);
    }
    return Object.freeze({status, snapshot, live, pendingRequest, receiveSnapshot,
      receiveAck, request, disconnect});
  }
  return Object.freeze({uint64, validateSnapshot, createController});
});
