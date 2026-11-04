// SPDX-License-Identifier: AGPL-3.0-only
// M2 transport seam for EXACT approved /preview/ HTML. No GUI redesign.
// Never infer ChannelId from demo-strip order; never run JS PCM or promote
// optimistic intent to accepted state. Owner ENGINE #266 supplies native ops.
(() => {
  "use strict";
  const backend = window.__JUCE__?.backend;
  const connected = Boolean(backend?.emitEvent && backend?.addEventListener);
  const model = window.ArSonMixNativeM2;
  const controller = connected && model?.createController
    ? model.createController({
        emit: (event, payload) => backend.emitEvent(event, payload),
        session: "native-m2-" + Math.floor(Date.now() * 1000).toString(36) +
          "-" + Math.floor(Math.random() * 0x100000000).toString(36),
        notify: status => {
          document.documentElement.dataset.armixNativeStatus = status;
        }
      })
    : null;
  const descriptor = Object.freeze({
    source: "scripts/build_pages.py => /preview/",
    pagesRoute: "/preview/",
    nativeTransport: connected,
    get acceptedGraphConnected() { return false; }, // M2 has no visual slot<->ID mapping yet.
    get productionCallbackLive() { return Boolean(controller?.live()); },
    get status() { return controller?.status() || "NATIVE_TRANSPORT_UNAVAILABLE"; },
    get acceptedSnapshot() { return controller?.snapshot() || null; },
    get pendingIntent() { return controller?.pendingRequest() || null; },
    request(operation, targetChannelId, payload) {
      return controller?.request(operation, targetChannelId, payload) || null;
    }
  });
  Object.defineProperty(window, "__ARMIX_APPROVED_NATIVE_GUI__", {
    value: descriptor, configurable: false, writable: false
  });
  if (!connected || !controller) return;

  backend.addEventListener("arMixConsoleSnapshot", message => {
    controller.receiveSnapshot(message);
  });
  backend.addEventListener("arMixConsoleAck", message => {
    controller.receiveAck(message);
  });

  // The public Pages GUI contains interactive demonstration state for /preview/.
  // M2 MUST NOT allow those audio controls to mutate browser-only state inside
  // an installed native mixer. Presentation/navigation/selection remain intact.
  // No HTML/CSS/layout edits, overlay or additional audio callback.
  const mutatingActions = new Set([
    "mute", "solo", "cue", "phase", "fxOn", "slotPower", "removeFx",
    "openPlugin", "wireGain", "wireRoute", "resetClip",
    "rename", "format", "fxReorder", "route"
  ]);
  const mutatingFields = new Set([
    "level", "pan", "trim", "input", "output", "sendLevel", "sendPan",
    "mute", "solo", "format", "activeSend"
  ]);
  const guard = event => {
    const node = event.target?.closest?.(
      "[data-action],[data-field],.fader,.fx-grip,#preview"
    );
    if (!node) return;
    const action = node.dataset?.action;
    const field = node.dataset?.field;
    const audio = node.matches?.("#preview,.fader,.fx-grip") ||
      mutatingActions.has(action) || mutatingFields.has(field);
    if (!audio) return;
    event.preventDefault();
    event.stopImmediatePropagation();
  };
  for (const type of ["click", "input", "change", "pointerdown", "wheel",
                      "keydown", "dragstart", "drop"])
    document.addEventListener(type, guard, {capture:true, passive:false});

  const ready = () => {
    // Ready signifies WebView DOM only, NOT real PCM, native routing or FX.
    backend.emitEvent("arMixGuiReady", {
      gui: "APPROVED_PAGES_PREVIEW_BUTTONS",
      visualReady: true,
      audioReady: false,
      schemaVersion: 6
    });
    backend.emitEvent("arMixConsoleSubscribe", {
      protocol: "v6", schemaVersion: 6,
      source: "APPROVED_PAGES_NATIVE_M2"
    });
  };
  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", ready, {once:true});
  } else ready();
})();
