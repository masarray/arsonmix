# ArSonMix Preview V2 → V5: Visual Recovery & Plugin UX Contract
Date: 2026-11-01 · PR #278 · Isolated reference GUI, NOT a running native mixer.

## Owner's actual screenshots and correction
The old `/preview/` fader/meter was visually richer; V4 `/preview-v2/` had a broken draggable fader, blurred/oversized thumb, cyan-illuminated machined slot, overcompressed IN/OUT, docked Plugin Manager that obscured channels, Add FX opening the wrong screen, and redundant TRIM.

V5 makes the **existing** live-console workflow less noisy without reintroducing a DAW editor/timeline:
1. **Fixed fader:** one source of truth for gain `-60…+10 dB`, cap `--fader-y`, original silver/rose PNG, 29×62 proportional size, pointer drag/capture and keyboard-accessible input, matched gain ruler. The current demo dBFS meter is simulated, with 0…−60 dBFS scale, independent of fader gain. The old 36×77 original PNG is embedded once (rendered proportionally smaller). Physical slot uses black graphite only; RAIL3 cyan glow explicitly disabled.
2. **Visual hierarchy:** FX rack height responsive to screen, MUTE/SOLO/PFL direct and same BTN-180 manual material, slim fader, no TRIM/EQ row/Input I/Send stereo PAN. Source IN/OUT separate 24px rows with full-height selects and clearer contrast. Ten fixed-width channel slots plus one equal-width pinned Master; a one-channel last bank does **not** stretch.
3. **Add FX fast picker:** clicking an insert opens a short floating contextual list anchored to that specific channel and insert index; typing narrows effects; clicking an item inserts and closes; F8 opens **Plugin Manager**, not the insert picker.
4. **Plugin Manager:** standalone floating card, not a bank dock; browsing/search, favorites, hidden entries, recent, metadata import, path list and Scan action. It does not steal channel width or change bank paging.
5. **PAN center:** physical PAN thumb/center-origin bar with ±8 click/drag center snap. Double-click PAN resets to center.

## Plugin Manager source-of-truth vs native integration
Official workflow benchmarks: [Image-Line FL Studio plugin scanning / Verify](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/basics_externalplugins.htm), [FL Studio F8 Plugin Picker](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/browser.htm), [Adobe Audition Audio Plug-in Manager](https://helpx.adobe.com/audition/desktop/applying-effects/adding-third-party-plug-ins.html).

**Default Windows VST3 discovery root:** `%COMMONPROGRAMFILES%\VST3` (commonly `C:\Program Files\Common Files\VST3`). Standard VST3 installation paths should always be scanned. An additional VST3 folder can be supported by ArSonMix's own native scanner if authorized by the user, but note that Adobe and FL Studio document *custom directory settings primarily for legacy VST/VST2*; do not assume clicking "Add folder" makes VST3 discoverable through the OS default path. Display a rescan/verify workflow for changed installations.

**Security & performance implementation contract for native ArSonMix host (NOT a GitHub Pages capability):**
- Expose a trusted native Plugin Management service independent from the realtime ASIO audio callback. The front-end adapter defined here is `window.ArSonMixNativePlugins.scan({paths: string[], verify: true})`; this must be provided by the actual native host or replaced by its documented typed bridge. The source demo has no host adapter and must report that explicitly.
- Enumerate `.vst3` bundles in approved locations outside the audio thread; verify each plugin as an **effect**, its stable VST3 class ID, vendor, layout/I/O, compatibility, architecture, and plugin load status. Categorize effects vs instruments, reject instruments from FX inserts, preserve favorites and a stable unique plugin identity across renames/restarts.
- Isolate or supervise scanner faults/hangs, record plugin failures as failed/unavailable without crashing the mixer; rescan updated plugins on explicit command; do not load an untrusted plugin merely by importing JSON metadata.
- Pass a validated catalog to `ArSonMixReference.setPluginCatalog([...])` in this reference prototype; production must use accepted `ConsoleSnapshot/Intent/Ack` and a real VST3 lease on native engine. A demo catalog entry must **never** be mistaken for a loaded VST3 effect.
- Manage file paths only with explicit user choice; do not silently crawl arbitrary folders. Physical plugin scanning/instantiation cannot occur on GitHub Pages.

## Verification and open gate
- 121/121 zero-dependency DOM/workflow tests, including actual pointer-drag simulation, FX picker open/search/insert, PAN center snap, shared Master width, and no bottom scrollbar: **PASS**.
- Embedded script parsed and CSS brace balance checked.
- Real Chromium screenshot/pixel QA of this V5 reference **NOT EXECUTED** here; full visual signoff across 1366×768, 1680×900 and 1920×1080 remains open. Native VST3 installation/scan and live audio are **NOT IMPLEMENTED** by this reference branch.
- The preview is a snapshot published through the existing Pages train only; `/preview/` legacy production GUI remains untouched.

**Next production integration gate:** native scanner/validation + live VST3 catalog and loading, native Console intent/ACK, and Windows hardware-first PCM qualification; this cannot be certified by static HTML.
