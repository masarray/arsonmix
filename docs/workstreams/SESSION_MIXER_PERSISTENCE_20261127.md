# Session Mixer: MON preference and FX rack file reliability

Base: canonical integration #899 at 32acb5ee47d09f01c53ceba4b25cb9b7f4eda87a; main at 379e867985bb6bab8e07297ba7a8c44925b22b2d. Scope is isolated, not a release lane.

## Findings
- Native secondary-output.json already persists verified endpoint ID when armed, but not on successful connect-only. MAIN MON selector ignored preferredOutput after restart and selected None until active. The separate settings dialog already consumed the preference.
- Native rack archive already implements whole rack save/open and single-effect insert/deactivate. Channel header menu omitted these operations; successful native notices were discarded by the approved bridge. Non-destructive single-effect export was missing. File-picker cancellation gave no feedback.
- Native .smix covers accepted graph, routing, FX binary, channel controls and hotkeys. Automatic capture is intentionally limited to disarmed idle/shutdown to protect vendor state and RT. No new auto-arm or fake browser persistence is introduced.

## Acceptance / test plan
- Exact saved endpoint displayed as saved/off after restart, explicitly enable with verified inventory; missing/replaced endpoint stays off. CONNECT and ENABLE persist native identity; failed writes visible.
- Header/rack menu offers save/open/insert; individual FX can export without removing it; deactivation remains an explicit separate operation after successful archive write. Native receipt only after operation returns.
- Preserve disarmed capture gate, registered VST3 identity, graph/roster/instance checks, atomic archive behavior, one approved GUI and one PCM graph.
- Regression contracts added to existing approved native M2 suite. Native compile, Chromium interactions, Windows restart and hardware confirmation remain required; no PASS claimed at source creation. No installer requested.

## Additional persistence audit
- The previous timer consumed the dirty audio-device flag before disk success. Keep it pending on failure, retry no faster than five seconds, and flush the verified machine profile before normal driver teardown.
- Join the final .smix worker and consume its completion before stopping the engine, so an actual failed shutdown commit is recorded, not silently discarded. Do not save over a retained/faulted session or force a pending driver to open.
- Request read-only MON inventory on startup even without an admitted audio route. Do not authorize an endpoint based on saved text. Normalized .arfx filenames cannot overwrite a path the user did not explicitly confirm.

## Smart Startup Recovery — prepare without auto-ARM
References: JUCE AudioDeviceManager https://docs.juce.com/master/classjuce_1_1AudioDeviceManager.html (explicit saved settings, default fallback switch); Microsoft https://learn.microsoft.com/en-us/windows/win32/coreaudio/endpoint-id-strings (opaque endpoint identity; OS/driver update may require explicit re-selection); Ableton https://help.ableton.com/hc/en-us/articles/115001878844-Recovering-a-Set-manually-after-a-crash (separate recovery and preferences).
- Native cold-start MON preparation waits for accepted session/graph, settled physical route, completed existing vendor-crash guard, and exclusive device transaction. Uses the same existing output-only sink and identity check. No startup ARM; ordinary previously verified MAIN resume policy unchanged.
- Existing v1 saved preference migrates with prepareOnStartup=true; successful explicit Disconnect writes false and cancels pending work, including missing endpoints. Shutdown disconnect is not operator intent. Same-session hotplug recovery remains separate.
- Missing endpoint retry uses existing 350ms / 1s / 3s / 8s / 30s capped scheduler; identity replacement terminates the automatic attempt. Explicit device/session actions cancel pending work; a nested stop or changed epoch/revision invalidates an in-flight open.
- GUI shows saved/off until actual native readiness, then ready/off; no claim of audible output. Existing UI inventory refresh when the endpoint returns. No new PCM callback, timer or graph.
- Scope: automatic cold MON preparation and intent persistence; primary ASIO cold-open failure retry, active-session crash loss window and hardware FAT remain separate qualification items. Native test/CI still required.

## Primary startup and MAIN intent hardening
- A saved ASIO registration that passes initial preflight but fails JUCE open may retry at most three times, through the same existing engine and device transaction. The exact XML and initially verified driver class are retained; no default fallback and no second callback registration. A changed/invalid registration or post-open verification failure terminates retries. Explicit device/session operations revoke the pending retry. Startup session restore waits until the bounded attempt finishes and transient busy work settles.
- Restoring a saved explicit MAIN mute or previous verified generation latches the operator mute, so later routine input/FX edits cannot accidentally auto-arm. No automatic MAIN while the saved session is awaiting recovery.
- Limitation: an absent/unregistered ASIO driver, permanent open failure, or a driver that blocks inside its vendor API is not solved by retry; user may need Audio I/O. Physical hardware and full native restart qualification pending.

## Smart continuous checkpoint — safe idle, durable recovery
- Exact accepted control-change hint is independent of graph/ACK revisions; identical fader/mute/gain updates do not dirty it. Observe cheap scalar stamp on existing CONTROL timer, debounce 2s after changes or transition to disarmed outputs, 5s bounded retry on capture/worker failure, 60s fallback capture for opaque VST3 edits. No live-output capture or additional thread. Native capture and immutable IO worker stay authoritative.
- A recovered previous HEAD now has a generation-bound RECOVERED_MUTED pointer written atomically before promotion; a second crash before a fresh checkpoint cannot silently inherit the old MAIN auto-resume flag. New verified HEAD supersedes the marker. Existing corrupt generations and archives retained.
- New C++ tests cover scheduler timing, unchanged fader vs changed state revision and a real storage recovery/restart/commit round trip. Fast source gates cover wiring and ordering. No native hardware qualification implied. Active-output crash-loss window for arbitrary vendor state remains intentionally bounded by last verified checkpoint; no unsafe vendor capture added.

## Late device and preference I/O completion
- Saved WASAPI cold-start failures defer session/plugin restore until existing endpoint retry opens the exact saved pair and silently binds the native device. The ordinary startup crash guard then owns restoration once; no repeated vendor FX instantiation from the WASAPI retry loop. Explicit device/session commands supersede cold recovery.
- MON preference updates are accepted in memory immediately, independently of disk receipt. Failed writes keep only the latest intent pending, retry at 5s cadence, flush during normal exit, and refresh native inventory on failed save ACK. A hard disk failure plus crash can still lose an uncommitted preference; no false durable-success claim.

## Live control checkpoint (non-disruptive)
- Research: VST3 IComponent getState is UI-thread permitted while processing (https://steinbergmedia.github.io/vst3_dev_portal/pages/FAQ/Processing.html), but arbitrary vendor implementations may contend internally; JUCE documents callback-lock/suspend tradeoffs (https://docs.juce.com/master/classjuce_1_1AudioProcessor.html). Ardour snapshots separate durable session state (https://manual.ardour.org/working-with-sessions/snapshots/). No changes to accepted FX DSP.
- While outputs are armed, snapshot accepted native metadata only (roster, WIRE, routing, binding, rack/slot bypass, MAIN intent), with a separate 2s debounce/10s continuous-edit cap and 5s retry. No render permit, mute, vendor state call, DSP or new thread. Single existing IO worker validates immutable HEAD and exact FX instance/catalog/path/UID, writes one atomic checksummed LIVE_CONTROLS overlay bound to that HEAD. Same state dedupes; full checkpoint supersedes it. No blob copies to disk during live updates. New/uncaptured FX defer rather than guess; first FX-free live checkpoint can create HEAD. Corrupt overlay falls back to verified base muted. Previous-generation rollback ignores the overlay.
- Limits: arbitrary VST3 parameter/program state remains last verified binary, and newly added FX need an idle/exit full capture. Legacy FX without durable instance identity cannot reuse bytes safely. No guarantee of zero crash-loss in uncommitted time window, nor physical qualification without FIELD.
