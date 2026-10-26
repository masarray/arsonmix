# Native JUCE canvas research and proof of concept (2026-10-27)

Reference: https://www.soundcraft.com/ui24-software-demo/mixer.html

## Technical findings
The Soundcraft Ui24 demo uses HTML5 Canvas. A rendered canvas is a pixel surface, not an exportable collection of JUCE widgets. PNG screenshots cannot restore drawing logic, hit regions, channel state, or DSP. The original Soundcraft page blocked direct server-side fetch (403), while the connected browser tab showed an HTML5 Canvas fallback. No exact live-page pixel parity or original asset inventory has been verified.

## Implemented
- Separate, original JUCE paint-based MixerCanvas and standalone demo, not a copy of Soundcraft image/code assets.
- 12 input strips and master. Dedicated fader scale (+10 to -60 dB), separate meter scale (0 to -60 dB). Thumb index line matches gain position.
- Relative fader dragging without click-jump, double click to 0 dB, wheel adjustment, relative PAN dragging with centre detent, MUTE/SOLO toggles.
- Six visibly EMPTY FX placeholders per strip, explicitly not connected to VST3/audio DSP.
- Meter levels default dark/off until the native host supplies real data via setMeter. No fabricated signal.
- Production WebView2, the approved v0.8 layout, native audio callbacks, CSS and existing application build are untouched.

## Build (C++20 / JUCE >= 8.0.15)

    cmake -S native/canvas-preview -B build/native-canvas -DJUCE_ROOT=C:/JUCE
    cmake --build build/native-canvas --config Release --target ARMixNativeMixerCanvasPreview

JUCE SDK / Windows compiler is not present in the analysis environment, so this initial PR has NOT been compile-tested or hardware-tested.

## Obtain true pixel captures from the referenced website
Open the working Ui24 demo in Chrome/Edge. Once fully rendered, open DevTools Console and review/paste the source from tools/capture_ui24_canvas.js. It downloads the local canvas image(s) and a JSON manifest. Browser cross-origin tainting and WebGL restrictions may prevent export; errors are reported. Capturing a canvas does not convert it to native widgets. No third-party Soundcraft assets are bundled, and captures should stay as private local references unless redistribution permission is confirmed.

## Further engineering before production
1. Compare obtained captures with native JUCE preview at 1440x820 and 1680x920, plus DPI 125/150%.
2. Implement a typed native message-thread command/state adapter. Never call JUCE GUI methods on DSP audio callback.
3. Bind real FX rows to VST catalog/state, authoritative bypass, real PAN/gain, preset recall and rollback.
4. Add real-meter coalescing with peak hold, clip latch and source labels. Do not invent signal readings.
5. Build/test on Windows JUCE; add hit-region, relative drag, fader/meter scale, resize and screenshot tests.
6. Require explicit replacement/design approval against the GUI v0.8 lock before integration.
7. Native multi-channel audio and hardware crackling P0 acceptance remain separate and incomplete.

This prototype is NOT a byte-identical or pixel-perfect proprietary Soundcraft clone, a production VST mixer GUI, or an audio-ready 13-channel host.
