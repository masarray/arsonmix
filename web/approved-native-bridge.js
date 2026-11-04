// SPDX-License-Identifier: AGPL-3.0-only
// Thin transport seam for the ONE already-approved Pages GUI.
// IMPORTANT: this is an M1 source/visual integration bridge, NOT a PCM driver.
// Native ChannelId/EdgeId ACK/readback binding must be added before product UAT.
(() => {
  "use strict";
  const backend = window.__JUCE__?.backend;
  const descriptor = Object.freeze({
    source: "site/preview + site/preview-buttons",
    pagesRoute: "/preview/",
    nativeTransport: Boolean(backend?.emitEvent && backend?.addEventListener),
    acceptedGraphConnected: false,
    productionCallbackLive: false,
    status: "M1_VISUAL_BUNDLE_ONLY__NOT_AUDIO_READY"
  });
  Object.defineProperty(window, "__ARMIX_APPROVED_NATIVE_GUI__", {
    value: descriptor, configurable: false, writable: false
  });
  // The native startup signal certifies ONLY that the approved DOM loaded.
  // It never reports VST3, PCM, hardware pins or graph capabilities as live.
  if (!descriptor.nativeTransport) return;
  const ready = () => backend.emitEvent("arMixGuiReady", {
    gui: "APPROVED_PAGES_PREVIEW_BUTTONS",
    visualReady: true,
    audioReady: false
  });
  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", ready, {once:true});
  } else ready();
})();
