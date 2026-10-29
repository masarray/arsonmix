# R0B — Unified live CH1 FX structural graph handoff (major milestone, 29 Oct 2026)

## Delivered scope / architecture
- The old live ADD/REMOVE path detached JUCE's audio callback and mutated a plugin object in place. The 10CH+4AUX callback therefore rejected ADD/REMOVE; clicking FX DROP could produce a discontinuity even at zero latency. This milestone removes the structural callback detach and unifies ADD, REMOVE and existing preset recall under `LiveGraphHandoff`.
- A **control-thread** candidate graph copies only stable strong ownership handles of currently live vendor instances in logical slot order, preserving vendor states, active Orbit processor, plugin editor identity and tails. New VST3 bus negotiation, `prepareToPlay`, latency checks and all string/catalog work are off the audio callback.
- Both legacy CH1 and 10CH+4AUX routing consume the SAME handoff lifecycle. During routed mode the existing single JUCE callback de-clicks output 1→0, atomically publishes new graph pointer at buffer boundary, and ramps 0→1 on the next block. Existing graph remains safely owned until ACK, then control thread releases discarded instances. No plugin refcount modification or destructor on RT.
- Positive GUI insert ACK, chain update and removal telemetry are emitted **after** callback transition and message-thread finalization. Failed edits return actionable negative result; accepted routing epoch/revision remains unchanged. No separate mixer callbacks or CSS/UI redraws.
- Existing DIRECT physical-fence and independent-clock restriction remain. New nonzero-latency CH1 VST3 is rejected in native send mode until verified PDC. Reordered/bypassed VST3 retains its identity and filter envelope.
- No new worker threads, no periodic browser work, no new CI matrix; one existing Windows native suite runs deterministic structural stress + JS contract checks.

## Not yet accepted / test obligations
- A successful CI run does not prove zero physical crackle on ASIO/WASAPI: issue #15 stays open.
- R0B still needs default single 10CH+4AUX session startup and removal of user-facing LAB/STOP dependency, hardware reconnect handling and race tests on actual drivers. Ch2–10 multi-FX, independent clock live structural edits, PDC and plugin-equipped full cross-device Shows remain separate gates.
- Physical FAT: current DIRECT ASIO + WASAPI device, 0 VST baseline, CH1 ADD/REMOVE/MOVE/Orbit while AUX sends run, synthetic and real audio, recording continuous PCM and checking pops on each transition. No benchmark claims without source-labeled measurements.

## Review hardening (single PR, no duplicate engine)
- Last-owner `PluginRelease` releases third-party resources even for failed/stale vendor prepare, discarded recall and candidate rollback; no scattered `use_count()==1` decisions. Thread affinity remains the existing control-owner contract.
- Rate/buffer/generation/reconfiguring guards checked at publish time; a stale prepared graph is refused.
- An active retained Orbit stays paused through ADD/REMOVE until control-thread ACK, and is rebound to the new logical slot by pointer identity.
- Existing Windows workflow now explicitly builds the structural stress target before CTest; no extra CI workflow or matrix.
