// SPDX-License-Identifier: AGPL-3.0-only
// Opt-in measurement of the ONE approved /preview/ GUI and its native WebView2.
// No CSS, overlay, polling, rAF loop or performance observer while inactive.
// This is a JS frame/event proxy, NOT hardware input-to-photon or GPU paint timing.
(function () {
  'use strict';
  const LIMIT = 2400;
  const ZONES = ['mixer', 'chrome', 'other', 'idle'];
  let session = null;
  let lastReport = null;

  function percentile(sorted, p) {
    if (!sorted.length) return null;
    const index = (sorted.length - 1) * p;
    const lo = Math.floor(index), hi = Math.ceil(index);
    return sorted[lo] + (sorted[hi] - sorted[lo]) * (index - lo);
  }
  function stats(values) {
    const sorted = values.slice().sort((a, b) => a - b);
    const round = value => value === null ? null : Math.round(value * 1000) / 1000;
    return {
      samples: sorted.length,
      medianMs: round(percentile(sorted, .5)),
      p95Ms: round(percentile(sorted, .95)),
      p99Ms: round(percentile(sorted, .99)),
      maxMs: round(sorted.length ? sorted[sorted.length - 1] : null),
      over33Pct: sorted.length
        ? round(sorted.filter(value => value > 33.333).length * 100 / sorted.length) : null
    };
  }
  function nativeEvent(name, payload) {
    try {
      const backend = window.__JUCE__?.backend;
      if (backend && typeof backend.emitEvent === 'function') {
        backend.emitEvent(name, payload);
        return true;
      }
    } catch (_) { /* A public Pages preview is not a native JUCE host. */ }
    return false;
  }
  function record(values, value) {
    if (Number.isFinite(value) && value >= 0 && values.length < LIMIT) values.push(value);
  }
  function classify(target) {
    if (target?.closest?.('.console-body')) return 'mixer';
    if (target?.closest?.('.window-title,.command-bar,.status-bar')) return 'chrome';
    return 'other';
  }
  function onPointer(event) {
    const s = session;
    if (!s) return;
    const zone = classify(event.target);
    s.zone = zone;
    s.lastPointerAt = performance.now();
    s.pendingPointerAt = s.lastPointerAt;
    s.pointerMoves[zone]++;
  }
  function onVisibility() {
    if (session && document.hidden) session.hiddenTransitions++;
  }
  function tick(now) {
    const s = session;
    if (!s) return;
    if (s.lastFrameAt && now > s.lastFrameAt) {
      const zone = !document.hidden && now - s.lastPointerAt < 280 ? s.zone : 'idle';
      record(s.frames[zone], now - s.lastFrameAt);
    }
    s.lastFrameAt = now;
    if (s.pendingPointerAt !== null) {
      // Event-handled-to-next-rAF proxy, NOT physical input or photon latency.
      record(s.pointerToRaf, Math.max(0, now - s.pendingPointerAt));
      s.pendingPointerAt = null;
    }
    s.raf = requestAnimationFrame(tick);
  }
  function start(options) {
    if (session) return false;
    options = options || {};
    const requested = Number(options.durationMs);
    const durationMs = Number.isFinite(requested)
      ? Math.min(60000, Math.max(500, requested)) : 15000;
    const s = {
      label: String(options.label || 'operator-pointer-sweep').slice(0, 80),
      startAt: performance.now(), durationMs, raf: 0, timeout: 0,
      lastFrameAt: 0, lastPointerAt: -Infinity, pendingPointerAt: null,
      zone: 'idle', hiddenTransitions: 0, pointerMoves: {mixer: 0, chrome: 0, other: 0},
      frames: {mixer: [], chrome: [], other: [], idle: []},
      pointerToRaf: [], samples: Object.create(null),
      longTaskAvailable: false, longTasks: 0, longTaskMs: 0, observer: null,
      nativeConnected: false
    };
    session = s;
    const supports = window.PerformanceObserver?.supportedEntryTypes;
    if (Array.isArray(supports) && supports.includes('longtask')) {
      try {
        s.observer = new PerformanceObserver(list => {
          for (const entry of list.getEntries()) {
            s.longTasks++;
            s.longTaskMs += entry.duration;
          }
        });
        s.observer.observe({entryTypes: ['longtask']});
        s.longTaskAvailable = true;
      } catch (_) { s.observer = null; }
    }
    document.addEventListener('pointermove', onPointer, {capture: true, passive: true});
    document.addEventListener('visibilitychange', onVisibility);
    s.nativeConnected = nativeEvent('arMixPerfControl', {action: 'start'});
    s.raf = requestAnimationFrame(tick);
    s.timeout = setTimeout(stop, durationMs);
    console.info('[ArSonMix P0] opt-in profiler started:', s.label);
    return true;
  }
  function sample(name, elapsedMs) {
    const s = session;
    if (!s || typeof name !== 'string' || name.length > 60) return;
    if (!Object.prototype.hasOwnProperty.call(s.samples, name)) {
      if (Object.keys(s.samples).length >= 12) return;
      s.samples[name] = [];
    }
    record(s.samples[name], elapsedMs);
  }
  function stop() {
    const s = session;
    if (!s) return lastReport;
    session = null;
    cancelAnimationFrame(s.raf);
    clearTimeout(s.timeout);
    document.removeEventListener('pointermove', onPointer, true);
    document.removeEventListener('visibilitychange', onVisibility);
    s.observer?.disconnect();
    const perZone = {};
    for (const zone of ZONES) perZone[zone] = stats(s.frames[zone]);
    const operations = {};
    for (const key of Object.keys(s.samples)) operations[key] = stats(s.samples[key]);
    const report = {
      schema: 'arsonmix.gui-perf-stage-a.v1',
      label: s.label,
      capturedAt: new Date().toISOString(),
      durationMs: Math.round(performance.now() - s.startAt),
      platform: navigator.platform || 'unknown',
      renderer: window.__JUCE__?.backend ? 'JUCE-WebView2' : 'browser-preview',
      display: {width: innerWidth, height: innerHeight, devicePixelRatio: devicePixelRatio},
      measurement: 'JS rAF pacing and handled-pointer-to-rAF only; not OS cursor, GPU paint or input-to-photon',
      zoneFrames: perZone,
      pointerMoves: s.pointerMoves,
      handledPointerToRafMs: stats(s.pointerToRaf),
      operations,
      hiddenTransitions: s.hiddenTransitions,
      longTaskAvailable: s.longTaskAvailable,
      longTasks: s.longTaskAvailable ? s.longTasks : null,
      longTaskMs: s.longTaskAvailable ? Math.round(s.longTaskMs * 1000) / 1000 : null,
      native: s.nativeConnected ? 'separate diagnostic log / native event' : 'not connected'
    };
    lastReport = report;
    // Native control thread emits its own independent timing summary at STOP.
    if (s.nativeConnected) {
      nativeEvent('arMixPerfControl', {action: 'stop'});
      nativeEvent('arMixPerfTrace', report);
    }
    console.info('[ArSonMix P0] GUI profile (copy ArSonMixPerf.exportJSON()):', report);
    return report;
  }
  function download() {
    if (!lastReport) return false;
    const blob = new Blob([JSON.stringify(lastReport, null, 2)], {type: 'application/json'});
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = 'arsonmix-gui-perf-stage-a.json';
    link.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
    return true;
  }
  function exportJSON() {
    return lastReport ? JSON.stringify(lastReport, null, 2) : null;
  }
  const api = Object.freeze({
    get active() { return session !== null; },
    get last() { return lastReport; },
    start, stop, sample, download, exportJSON
  });
  Object.defineProperty(window, 'ArSonMixPerf', {value: api, configurable: false});

  try {
    window.__JUCE__?.backend?.addEventListener?.('arMixPerfNativeResult', raw => {
      if (lastReport && raw?.schema === 'arsonmix.native-message-thread.v1') {
        lastReport.native = raw;
        console.info('[ArSonMix P0] native JUCE message-thread profile:', raw);
      }
    });
  } catch (_) { /* WebView integration is optional in the public Pages view. */ }

  document.addEventListener('keydown', event => {
    if (!(event.ctrlKey && event.altKey && event.shiftKey && event.code === 'F9')) return;
    event.preventDefault();
    if (session) stop();
    else start({label: 'Windows pointer A/B'});
  }, {capture: true});

  if (new URLSearchParams(location.search).get('armixperf') === '1') {
    start({label: 'opt-in-url'});
  }
})();
