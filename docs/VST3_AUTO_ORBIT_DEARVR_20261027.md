# VST3 Auto Orbit — ArSonMix native host (experimental)

## Use with dearVR PRO 2

1. Load dearVR PRO 2 into the **real Channel 1** VST3 FX Rack in the Windows desktop app.
2. Open the vendor editor (**EDIT**) and switch **POSITION** from CARTESIAN to **POLAR**. Keep elevation and distance fixed. Set a comfortable initial azimuth. Disable dearVR's Performance Mode if it optimises for static sources.
3. In the native FX Rack select **ORBIT** next to dearVR PRO 2. Inspect the live list of vendor-exposed VST3 parameters (not the mock HTML plugin inspector).
4. Select **Azimuth** (not **Azimuth Scale**). Read the vendor-reported minimum/maximum. Confirm they span a complete circle by checking the verification checkbox. If the native plugin does not expose a full-cycle automatable azimuth, do **not** enable orbit.
5. Set the speed (default 20 seconds per revolution) and forward/reverse direction. Start remains disabled until an actual parameter is selected and the range is confirmed.
6. Click **START**; change direction/period and start again to update. **STOP** returns the parameter to manual control without resetting its current angle.

The separate JUCE control timer runs only when orbit is enabled at 60 updates/sec; no extra work is added to the audio callback and the vendor editor need not be open. This is *control-rate* modulation, not sample-accurate DAW automation. Smoothing and wrap seam at 0/1 depend on the VST3 vendor. This first version supports **one selected, automatable parameter** and **one active orbit at a time**. If dearVR exports Cartesian X/Y rather than full-cycle Azimuth, the one-parameter orbit is insufficient; true Cartesian orbit requires a paired sin/cos parameter driver.

## Safety and lifecycle

- Parameter names and 0/1 endpoint strings come from the instantiated VST3 via JUCE. No guessed parameter IDs or direct GUI mouse movement. Discrete parameters with fewer than 72 resolution steps are excluded.
- The UI requires explicit full-circle range confirmation because a parameter name alone does not prove periodic mapping. Start/Stop state is synchronized to the native bridge on FX graph changes.
- The modulator calls `AudioProcessorParameter::setValueNotifyingHost` only from JUCE's message thread; it never calls it in the audio callback.
- Begin/end edit gestures are paired; the previous orbit stops before a new start.
- Orbit is disabled before FX add/remove/reorder, preset save/recall, independent clock changes and host shutdown, protecting borrowed plugin parameter pointers.
- Changing azimuth on live microphone monitoring can be disorienting. Start with low headphone volume; verify the plugin’s polar mode and correct parameter range.
- **Physical hardware/FAT has not been performed.** A passing Windows build confirms compatibility with JUCE, not VST3 vendor thread safety or seamless surround perception.

## Engineering acceptance

- `ARMixOrbitPhaseTests` asserts positive and reverse progress, wrapping, phase, speed endpoints and invalid configurations. Unlike the original test, checks remain active in C++ **Release** builds even with `NDEBUG` enabled.
- Windows CI builds the actual native host and orbit test and runs CTest. Chromium native-bridge smoke verifies the actual FX Rack dialog, 360° confirmation gate, START/STOP event payload, compact geometry and records a screenshot.
- Physical FAT: with dearVR PRO 2 loaded, confirm it exposes a usable Azimuth parameter, the editor reflects active orbit when open, the sound moves continuously through four cardinal bearings, STOP releases automation, and no gaps/crackles with editor closed.
- Confirm 0/1 boundary at several speeds (2s, 10s, 20s); some VST3s may wrap spatial angle discontinuously or smooth along the wrong path. Only claim fully smooth orbit after the physical test.

## Audit notes (2026-10-27)

This PR is an experimental capability, NOT a complete production-grade spatial automation engine. Its continuous ramp is time-based, not sample accurate; under heavy UI load the 60 Hz JUCE timer can jitter. It has no independent orbit audible seam validation, per-vendor calibration profile, parameter state persistence, spatial Cartesian dual-axis fallback, or multi-plugin/multi-channel independent LFO scheduler. Each of those should be based on actual user requirements and tested vendor behavior rather than naive generalisation.

There is a hard logical wrap at the normalized 0/1 endpoint. That mathematically returns to the same azimuth only if the VST3 parameter truly encodes a full circle. The vendor may smooth/interpolate the seam poorly, causing audible fast panning. The earliest manual hardware acceptance is **dearVR PRO 2 POLAR, Azimuth confirmed -180°..180°, Binaural 2CH, editor open/closed, 2/10/20 second revolutions, no perceptible wrap jump**. If seam fails, do not merge as mature: a vendor-aware dual-axis (Cartesian sin/cos) path may be required instead.
