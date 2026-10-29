# R0B B3 — native production normal-path startup and fail-silent routing

## Delivered control semantics
The normal DIRECT audio callback no longer selects the legacy CH1-only renderer simply because a 10CH patch has not been applied. It always enters the existing native 10CH+4AUX route owner, which clears all outputs if the device has no acknowledged prepared route. `start()` begins output-MUTED. A user must select real enabled ports, validate epoch/revision, apply while muted, and explicitly arm output. Clearing routing or a device epoch invalidation returns silence rather than CH1 fallback. The same CH1 vendor FX/Orbit processor lease and B2 prepared handoff remain intact; no duplicate graph or callback is created.

## Exceptions and limits
The established explicit independent-clock and diagnostic capture/output-tone paths are not silently removed; do not confuse them with production 10CH routing. The old native `routingDryStop` is retained as a compatibility command but now means clear patch + mute, not switch render engine. The button is hidden in the integrated workspace. No auto-map, auto-unmute, output patch, PDC, native full Show recall or physical ASIO/WASAPI FAT is claimed. Issue #15 remains an independent blocker.

## Verification
Source contracts and existing deterministic DSP tests; exact-head Windows MSVC/CTest and Chromium if triggered. Hardware FAT still needed for actual clean-audio qualification. Follow-on R0B must remove obsolete identifiers/command aliases only when compatibility tests and field acceptance support it.
