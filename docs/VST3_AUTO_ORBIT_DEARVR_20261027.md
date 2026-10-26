# VST3 Auto Orbit — ArSonMix native host (experimental)

## Use with dearVR PRO 2

1. Load dearVR PRO 2 into the **real Channel 1** VST3 FX Rack in the Windows desktop app.
2. Open the vendor editor (**EDIT**) and switch **POSITION** from CARTESIAN to **POLAR**. Keep elevation and distance fixed. Set a comfortable initial azimuth. Disable dearVR's Performance Mode if it optimises for static sources.
3. In the native FX Rack select **ORBIT** next to dearVR PRO 2. Inspect the live list of vendor-exposed VST3 parameters (not the mock HTML plugin inspector).
4. Select **Azimuth** (not **Azimuth Scale**). Check the displayed minimum/maximum represent a complete circle. Default: 20 seconds for one revolution.
5. Click **START ORBIT**; choose direction or period and start again to update the setting; **STOP** returns the parameter to manual control without resetting its current angle.

The separate JUCE control timer runs only when orbit is enabled at 60 updates/sec; no extra work is added to the audio callback and the vendor editor need not be open. This is *control-rate* modulation, not sample-accurate DAW automation. Smoothing and wrap seam at 0/1 depend on the VST3 vendor. This first version supports **one selected, automatable parameter** and **one active orbit at a time**. If dearVR exports Cartesian X/Y rather than full-cycle Azimuth, the one-parameter orbit is insufficient; true Cartesian orbit requires a paired sin/cos parameter driver.

## Safety and lifecycle

- Parameter names and 0/1 endpoint strings come from the instantiated VST3 via JUCE. No guessed parameter IDs or direct GUI mouse movement.
- The modulator calls `AudioProcessorParameter::setValueNotifyingHost` only from JUCE's message thread; it never calls it in the audio callback.
- Begin/end edit gestures are paired; the previous orbit stops before a new start.
- Orbit is disabled before FX add/remove/reorder, preset save/recall, independent clock changes and host shutdown, protecting borrowed plugin parameter pointers.
- Changing azimuth on live microphone monitoring can be disorienting. Start with low headphone volume; verify the plugin’s polar mode and correct parameter range.
- **Physical hardware/FAT has not been performed.** A passing Windows build confirms compatibility with JUCE, not VST3 vendor thread safety or seamless surround perception.

## Engineering acceptance

- `ARMixOrbitPhaseTests` asserts positive and reverse progress, wrapping, no-start jump, invalid configurations.
- Windows CI builds the actual native host and orbit test and runs CTest.
- Physical FAT: with dearVR PRO 2 loaded, confirm it exposes a usable Azimuth parameter, the editor reflects active orbit when open, the sound moves continuously through four cardinal bearings, STOP releases automation, and no gaps/crackles with editor closed.
- Confirm 0/1 boundary at several speeds (2s, 10s, 20s); some VST3s may wrap spatial angle discontinuously or smooth along the wrong path. Only claim fully smooth orbit after the physical test.
