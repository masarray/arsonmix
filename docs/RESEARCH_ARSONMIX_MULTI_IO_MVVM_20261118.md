# Research: Multi-Device Input/Output and Modular JUCE + WebView2

**Audited:** 2026-11-18, main SHA 85d84195d06c8798ce3a4309b362766c13b07e14.  
**Product terminology:** ArSonMix is the mixer. Harmonice is the separate built-in harmony plugin.  
**Status:** research and architecture evidence only, not a shipped feature or real hardware FAT.

Companions: [PRD](PRD_ARSONMIX_MULTI_DEVICE_IO_20261118.md), [ARD](ARD_ARSONMIX_MULTI_DEVICE_IO_V2_20261118.md), [MVVM decision](architecture/ADR_MODULAR_MVVM_FEASIBILITY_20261118.md), earlier input-only [draft PR #757](https://github.com/masarray/arsonmix/pull/757).

## Source audit findings

| Current file | Lines on main | Grounded role and concern |
| --- | ---: | --- |
| src/Main.cpp | 8,873 | Application/UI composition, multiple native command paths, device setup, FX/editor and status orchestration; extraction seams are high leverage |
| src/LiveAudioEngine.h | 3,637 | ASIO primary, independent WASAPI pair, OUTPUT1 consumer, callback renderer, FX handoff, recovery and output fences |
| src/LiveDryRoutingLab.h | 3,636 | Native accepted routing, physical patch, external output, FX leases, M4 compatibility |
| web/approved-native-bridge.js | 4,283 | Presentation, multiple state projections, native transport, route coordination |
| web/monitor.js | 2,118 | Hardware/route drafts and event handlers |
| web/mixer-io.js | 2,082 | Device selector, named sources, physical I/O UI |
| engine/core/DynamicNativeSessionPublisher.h | 426 | Existing single native graph authority, output assignments and render |
| engine/core/DynamicPreparedMixerBlock.h | 1,018 | Prepared ChannelId PCM; output fanout to primary-device physical pin pointers |

Line count alone is not a correctness defect. Evidence of complexity is mixed responsibilities and high coupling across source/UI/device/FX/session changes. The [maintainability issue #527](https://github.com/masarray/arsonmix/issues/527) confirms M1-M6 improvements were partially/fully merged: do not reimplement those milestones from scratch.

**Current implemented distinctions:**

- A single verified ASIO driver can expose multiple output pin pairs. DynamicPreparedMixerBlock::setExternalOutput, DynamicNativeSessionPublisher::setExternalOutput and LiveDryRoutingLab::setExternalOutput cover hardware output assignment on that **same device**, with no cross-clock ASRC required.
- LiveSecondaryOutputDevice is a second **WASAPI render-only device** fed by a copy of MAIN PCM via AdaptiveClockBridge. It is **not** generic per-BUS/device output routing.
- JuceIndependentClockDevice is an alternate **WASAPI capture + WASAPI render** pair with two AudioDeviceManager instances; its render callback forwards to the *one* existing mixer. It is not parallel multi-ASIO captures.
- AdaptiveClockBridge is bounded SPSC + FIR/polyphase ASRC, already reused for independent stream and additional output. It needs additional per-sink/source quality and scheduling validation before N-device use.
- LiveInputPortRegistry and JuceActivePortSnapshot represent per-device pin masks and packed callback pointers. A Input 1 and B Input 1 collide if represented only by one flat physical pin index.
- NativeDeviceApplyCoordinator, NativeLogicalMixerControl, .smix document, command ACK/readback, approved /preview/ GUI, and DynamicNativeSessionPublisher are canonical owners worth preserving.
- [Draft #757](https://github.com/masarray/arsonmix/pull/757) covers **additional input** and dual ASIO proof, not the broader independent output sink problem. The present research is additive, not proof that #757 is deployed.
- [Milestone D candidate #760](https://github.com/masarray/arsonmix/pull/760) is separate open field qualification work; no cross-merge or unrelated DSP edits here.

## Global engineering evidence

| Source | Adopt | Do not infer |
| --- | --- | --- |
| [Pinned JUCE 8.0.15 ASIO backend](https://github.com/juce-framework/JUCE/blob/8.0.15/modules/juce_audio_devices/native/juce_ASIO_windows.cpp) | 16 implementation slots and separate callback dispatch, suitable for exact-version dual-driver feasibility spike | Does not guarantee 16 vendor ASIO drivers can all start concurrently or accept input-only operation |
| [JUCE AudioDeviceManager tutorial](https://juce.com/tutorials/tutorial_audio_device_manager/) and [AudioIODeviceCallback docs](https://docs.juce.com/master/classjuce_1_1AudioIODeviceCallback.html) | One active device per manager; separate high-priority I/O callbacks and JUCE-hosted DSP | Built-in selector is not multi-device aggregation |
| [VB-Audio Matrix](https://vb-audio.com/Matrix/) | Vendor precedent for several ASIO and Windows streams, matrix, multiple sinks | Proprietary implementation not transferable; not every vendor driver works |
| [JACK independent soundcards FAQ](https://jackaudio.org/faq/multiple_devices) | Independent hardware clocks produce drift, need clock-aware resampling | Not a Windows library drop-in |
| [PipeWire combine-stream](https://docs.pipewire.org/devel/page_module_combine_stream.html) | Device-local output mapping and latency compensation | No guarantee different USB DACs are phase-locked |
| [Apple Aggregate Devices](https://support.apple.com/en-us/102171) | Reference clock plus drift correction on other devices | macOS implementation not Windows ASIO |
| [RME multiple cards](https://docs.rme-audio.com/aoxd/705-1c_multiple_cards_win/) | One vendor driver can aggregate several *hardware-synchronized* cards | Not evidence for unrelated USB driver clock synchronization |
| [Microsoft WASAPI](https://learn.microsoft.com/en-us/windows/win32/coreaudio/wasapi), [IAudioClient3](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nn-audioclient-iaudioclient3), [IAudioClock](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclock-getposition) | User-mode independent capture/render endpoints, negotiated period, clock evidence from actual stream | Neither ASIO multi-client promise nor zero-latency output aggregation |
| [JUCE WebView UI](https://juce.com/blog/juce-8-feature-overview-webview-uis/) | Native C++ / JS transport works with modular frontend state | Does not require moving DSP into JS or rewriting approved GUI |
| [Microsoft MVVM](https://learn.microsoft.com/en-us/windows/apps/develop/data-binding/data-binding-and-mvvm), [Fowler Presentation Model](https://martinfowler.com/eaaDev/PresentationModel.html) | ViewModel separates UI view/data, tests without DOM | MVVM alone cannot repair audio lifetime or domain ownership |
| [Redux unidirectional state](https://redux.js.org/understanding/thinking-in-redux/three-principles) | Model intents + immutable read projections + deterministic selectors | JS cannot become another source of accepted PCM truth |

## Options evaluated

1. **One multichannel ASIO device, same hardware clock:** best realtime latency and pin consistency; preserve current direct path and its existing external output mapping.
2. **Multiple independent JUCE-managed capture/render devices:** preferred future scope. Primary render-clock master; capture-only SPSC+ASRC per distinct secondary source and render-only SPSC+ASRC per independent output endpoint. One mixer/VST3 execution.
3. **WASAPI native narrow adapter:** fallback when JUCE ASIO refuses an input-only or safe output profile, subject to exact identity/readback and resource contention tests. Never silent backdoor device switching.
4. **ASIO4ALL/third-party aggregator:** optional documented workaround, never installation requirement.
5. **New kernel WDM/virtual ASIO driver:** rejected for this feature, adds signing/legal/driver maintenance and is unnecessary for an app-only multi-device mixer.
6. **Replace JUCE/audio engine or rewrite all GUI in React/MVVM:** rejected without a measured blocker; would amplify regression risk in existing VST3/session/GUI/PCM workflows.

## Key unknowns requiring falsifiable experiments

- Does pinned JUCE 8.0.15 open/start two *distinct vendor* ASIO instances in the same process with actual simultaneous PCM? In both open/stop orders, and with one secondary input-only? Failure must be reported as UNSUPPORTED, not disguised with repeated attempts.
- Can an independent output device B play a selected **BUS** signal distinct from MAIN using exact registered endpoint/pins, independently of the existing MAIN-copy OUTPUT1?
- Can A render MAIN without being stalled or muted when secondary B input/output fails, renegotiates, hotplugs or goes silent? No reference/memory reclamation before callback leases quiesce.
- Do output sinks maintain bounded fill and ppm under +/- drift and variable block sizes? Can independent DAC playback latencies be measured/compensated for a qualified use case?
- Does an operation preserve SOURCE ONLY metering, WIRE routing, FX/plugin leases, session persistence and output pin approval exactly? Does each VST instance process once per graph block irrespective of sink count?
- Does a new presentation ViewModel maintain one native accepted truth rather than shadowing a JS mixer session and misleading LIVE state?

**Non-negotiable qualification:** differentiate `MULTI_CAPTURE_FAT`, `MULTI_SINK_FAT`, and combined `MULTI_IO_FAT`. A passing synthetic clock simulation is not physical ASIO interoperability or audible output proof. Numeric CPU/latency/drift/THD acceptance budgets must be declared per qualified rig *before* physical testing, not invented after a pass.

**Distribution:** No custom kernel driver is required for JUCE/WASAPI/ASIO host use. Separately review AGPLv3 JUCE, bundled ASIO SDK GPLv3 terms and Windows executable signing/reputation; these are not kernel signing blockers.
