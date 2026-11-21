"""FUX-1 Chromium component harness for the ORIGINAL native FX modules.

Real JUCE opens a separate top-level HWND when the mixer dispatches
floatingBuiltinOpen. Headless Playwright has no JUCE HWND; this helper
mirrors that verified dispatch into the OLD editor DOM solely to keep
testing actual sliders, knobs, SVG and audio ACK semantics. Product
code NEVER installs this shim; native popup is tested independently by
approved_floating_fx_window_smoke.py and Windows MSVC/field FAT.
"""
from __future__ import annotations


def project_approved_editor_for_component_smoke(page):
    page.evaluate(r"""() => {
      const backend=window.__JUCE__?.backend;
      if(!backend||typeof backend.emitEvent!=="function"||
         backend.__fux1ComponentProjection)
        throw Error("Expected native WebView2 control test double");
      backend.__fux1ComponentProjection=true;
      const original=backend.emitEvent.bind(backend);
      const editorHandles=Object.freeze({
        "armix.builtin.channel-eq.v1":"ArSonMixNativeEq",
        "armix.builtin.channel-compressor.v1":"ArSonMixNativeCompressor",
        "armix.builtin.graphic-eq31.v1":"ArSonMixGraphicEq31",
        "armix.builtin.arson-harmony.v1":"ArSonMixNativeHarmony",
        "armix.builtin.vocal-throw.v1":"ArSonMixNativeVocalThrow",
        "armix.builtin.8d-sonku.v1":"ArSonMixNativeSonKu"
      });
      window.__fux1FloatingDispatches=[];
      backend.emitEvent=function(name,payload) {
        original(name,payload); // keep exact native-ACK test capture unchanged
        if(name!=="arMixAction"||payload?.action!=="floatingBuiltinOpen")return;
        const key=editorHandles[payload.catalogId];
        if(!key)return;
        const view=window.__ARMIX_NATIVE_M2__,scope=window.__ARMIX_NATIVE_SCOPE__;
        const track=[...(scope?.tracks||[]),scope?.master]
          .find(t=>t && view?.nativeIdFor?.(t)===payload.channelId);
        window.__fux1FloatingDispatches.push({...payload});
        if(!track||typeof window[key]?.open!=="function")
          throw Error("Missing original FX UI or accepted rack ChannelId");
        window.queueMicrotask(()=>window[key].open(track,payload.slot));
      };
    }""")
