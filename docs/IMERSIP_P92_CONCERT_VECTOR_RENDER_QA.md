# ImerSip Concert GUI v2: scalable render, speaker safety

**Owner defect (Windows screenshot 2026-11-16):** the earlier native 1100×680 editor embedded a 256×320 paletted PNG atlas, expanded the 256×144 room to 1100×461, and expanded tiny (39×73) PA fragments. The original speaker sheet also had noisy alpha, and cropped atlas rectangles could cut off speakers. A 768×960 optional atlas was never packaged, so the fallback was always visibly broken. This was a source/compositing failure, not Windows DPI.

## Correct engineering contract

- **Actual source assets** now live separately in \`assets/imersip/vector/\` as text SVGs: clean empty room, blank three-section metal desk, uncropped flown line-array, independent subwoofer and front-fill PA. These reproduce the design direction of the owner's original PNGs, with scalable geometry and no corrupted alpha; the original legacy PNG remains in the repo only as a historical asset, not in the built-in pack.
- Embedded as five independent \`juce_add_binary_data(ARMixAssets)\` resources. UI decodes SVG into \`juce::Drawable\` *once in the editor constructor*, renders at target rectangle using \`drawWithin\` at device scale. **No upscaled paletted atlases, no arbitrary sprite cropping, no optional sidecar pack required.**
- Layout dimensions are exactly 1100×680 with room viewport y0..461 and control desk y461..680. \`imersipP92ConcertBoxes()\` computes six independent PA rectangles for every valid Stage (30–180°) and Distance (1.6–7.4m) and constrains them inside safe room x16..1084/y100..444. Narrow-stage fill/sub overlap is explicitly vetoed. Mirroring flips **only the right rendered graphic**, never channel phase.
- Track, thumb and master knob are drawn at device resolution via JUCE vector primitives. Existing preset selector, room control, native ACK queue, host FX power/bypass, IMRS v6 state and P9.1/P9.2 audio are unchanged. No DSP, spatial bass, or HRTF path was edited.
- GUI native test asserts **every SVG parses**, snapshots 1100×680 and checks distinct room/panel pixels, checks slider controls, and traverses stage/distance corners and intermediate points plus invalid finite-guard fallback. The original SonKu PCM parity and state validation remain required.
- Fix is production-adjacent, **not yet public release**. Real Windows screenshot QA at 100% and 150% DPI and owner audition should precede merge.

The visual SVGs are editable source assets. Do not replace them with an artist sheet at quarter resolution, raw opaque PNG rectangles, or a \`stretchToFit\` path that distorts cabinet proportions. As needed, improve material appearance *within separate SVG files* without changing geometry or audio.
