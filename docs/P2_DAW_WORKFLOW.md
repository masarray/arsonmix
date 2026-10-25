# P2 DAW Workflow & Progress UX — Windows Trial

## What changed

- **Mixer-first add:** click **ADD FX** directly on channel 1. The FX selector opens an indexed VST3 catalog with search and categories. Click **+ ADD** to insert into the real native chain; existing mixer insert buttons open plugin editors. Do not confuse the other 11 visual channel strips with native I/O.
- **Plugin Manager:** **PLUGINS** in the toolbar opens plugin administration. System scan, custom folder scan and individual `.vst3` import exist exclusively here, not inside the effects picker.
- **Real scanning progress:** while finding `.vst3` candidates the total is unknown (animated indeterminate bar). Once the scanner has enumerated candidate modules, the bar becomes determinate using **processed candidate count / total candidates**. It shows current module and effects found; no invented percentages.
- **Audio Setup progress:** opening from toolbar or SETUP immediately shows a busy dialog while JUCE initializes the selector. First-open ASIO driver enumeration may still take time. The app reports actual setup construction time in milliseconds; the native audio setup window is reused for later opens.
- **Background:** VST3 scan runs on a dedicated worker. Metadata discovery can still run untrusted third-party plugin code; there is no crash isolation in this version.
- **Monitor safety:** scanning and chain structural changes require MONITOR OFF. Live bypass uses atomic flags in the native engine.

## Quick trial

1. Download and unzip **AR-MIX-Studio-P2-DAW-Workflow-Windows-x64** from a successful GitHub Actions run.
2. Launch the EXE and click **AUDIO SETUP**. Observe a graphical progress indicator; configure your ASIO or WASAPI input/output. Close settings, then reopen to verify faster cached window reuse.
3. Click **PLUGINS** and **SCAN STANDARD FOLDERS**. Watch the scan change from discovery (indeterminate) to candidate inspection (% with real totals). Import a single .vst3 with **IMPORT INDIVIDUAL .VST3** or add a folder if needed.
4. Return to **MIX**, click **ADD FX** on Channel 1, search the catalog, and choose **+ ADD**. The effect should appear in the real channel rack. Click the inserted plugin to open its native editor.
5. With monitoring off, build your chain. Enable monitoring only after you have wired headphones at low volume and have a stable driver setup.

## Known limits

- First ASIO driver enumeration may still be slow or blocked by an external driver. Reporting an indeterminate busy state is honest; claiming a fake numeric percentage is not. Timing data supports the next performance audit.
- Scanner currently loads third-party plugin metadata in the application process. Crashes/hangs remain possible.
- The catalog indexes VST3 only. VST2 *.dll loading is unsupported.
- P2 is one native Channel 1 input path with up to eight serial stereo VST3 effects, not a complete 12-channel live mixer.
- GUI and scanner features compile in Windows CI but hardware scan/audio/editor behavior must be validated on a physical Windows PC.

## Acceptance

- Windows CI GREEN including `node --check web/bridge.js`, C++ Release build and RT tests.
- Opening Audio Setup displays busy feedback immediately; subsequent opens use the cached native window.
- Plugin Manager displays real progress with total after discovery.
- ADD FX on mixer opens selector without navigation to manager.
- No fake scan percentages, no plugin scan operations accidentally triggered from FX selection.
