// SPDX-License-Identifier: AGPL-3.0-only
// The standalone HTML still works as a demo. This runs only inside JUCE WebView2.
(() => {
  const backend = window.__JUCE__?.backend;
  if (!backend?.emitEvent || !backend?.addEventListener) return;
  const send = (action, value) => backend.emitEvent("arMixAction", { action, value });
  const target = document.querySelector(".status-help");
  if (!target) return;
  const strip = document.createElement("div");
  strip.id = "nativeP1";
  strip.innerHTML = '<button data-p1="setup">AUDIO SETUP</button><button data-p1="load">LOAD REAL VST3</button><button data-p1="unload">UNLOAD</button><button data-p1="monitor">MONITOR OFF</button><button data-p1="bypass">BYPASS OFF</button><span data-p1-status>Starting audio...</span>';
  target.replaceChildren(strip);
  const style = document.createElement("style");
  style.textContent = `
    #nativeP1{display:flex;align-items:center;gap:5px;white-space:nowrap;font-size:10px;max-width:100%;overflow:auto}
    #nativeP1 button{font:inherit;color:#cde5ed;border:1px solid #426273;border-radius:4px;padding:4px 7px;
      background:linear-gradient(175deg,#42596b 0%,#233745 42%,#101e29 100%);cursor:pointer}
    #nativeP1 button[aria-pressed=true]{background:linear-gradient(#24aadd,#10617d 58%,#063247);
      border-color:#71d8f2;color:#fff}
    #nativeP1 [data-p1-status]{color:#95b9c6;min-width:110px;overflow:hidden;text-overflow:ellipsis}
    .status-help{min-width:0;flex:1}
  `;
  document.head.appendChild(style);
  let monitor = false, bypass = false, warned = false;
  strip.addEventListener("click", (e) => {
    const b = e.target.closest("button[data-p1]");
    if (!b) return;
    if (b.dataset.p1 === "setup") send("audioSetup");
    if (b.dataset.p1 === "load") send("chooseVst3");
    if (b.dataset.p1 === "unload") send("unloadVst3");
    if (b.dataset.p1 === "monitor") send("monitor", !monitor);
    if (b.dataset.p1 === "bypass") send("bypass", !bypass);
  });

  // Actual output level from the native callback. Remaining mock channels
  // are forced dark to avoid implying the P1 host has 12 live audio inputs.
  function level(peak) {
    const db = 20 * Math.log10(Math.max(1e-6, Number(peak) || 0));
    return Math.max(1, Math.min(100, (db + 60) * 100 / 60));
  }
  function paintMeter(el,value){
    if(!el)return;
    const level=Math.max(0,Math.min(100,Number(value)||0));
    if(typeof window.__arMixSetLedMeterLevel==="function")window.__arMixSetLedMeterLevel(el,level);
    else el.style.setProperty("--level",level+"%");
  }
  backend.addEventListener("arMixStatus", (s) => {
    monitor = !!s.monitor;
    bypass = !!s.bypass;
    const mon = strip.querySelector('[data-p1="monitor"]');
    const by = strip.querySelector('[data-p1="bypass"]');
    mon.textContent = monitor ? "MONITOR ON" : "MONITOR OFF";
    mon.setAttribute("aria-pressed", String(monitor));
    by.textContent = bypass ? "BYPASS ON" : "BYPASS OFF";
    by.setAttribute("aria-pressed", String(bypass));
    strip.querySelector("[data-p1-status]").textContent =
      s.error ? "ERROR: " + s.error :
      (s.connected ? s.driver + " · " + Math.round(s.rate) + "Hz / " + s.buffer + " · " +
       (s.plugin || "Dry mic") + " · +" + (s.pluginLatencySamples || 0) + " samples plugin latency" :
       "NO AUDIO DEVICE");
    const status = document.getElementById("audioStatus");
    if (status) status.textContent = s.connected
      ? "P1 NATIVE · " + (monitor ? "MONITOR ARMED" : "MONITOR MUTED (safe)") +
        " · Live VST3: " + (s.plugin || "none")
      : "P1 NATIVE · No audio device";
    const p = level(s.peak);
    document.querySelectorAll("[data-meter]").forEach((meter) =>
      paintMeter(meter,meter.dataset.meter === "0" ? p : 1));
    ["masterMeterL","masterMeterR","masterMeterAux"].forEach((id) => {
      const el = document.getElementById(id);
      paintMeter(el,p);
    });
    const cpu = document.querySelector(".cpu strong");
    if (cpu) cpu.textContent = "P1 LIVE";
    if (!warned && s.error) {
      warned = true;
      console.warn("AR MIX native audio:", s.error);
    }
  });
})();
