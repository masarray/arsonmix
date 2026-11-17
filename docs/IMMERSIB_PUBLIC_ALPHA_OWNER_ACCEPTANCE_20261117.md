# ImmerSIB — owner listening accepted; Windows public alpha shipped

Date: 17 November 2026. This is the **current status record**. Earlier
ImmerSIB P9.2/P9.8/P9.9 research gate documents and development PR descriptions
are historical records, not active requests for another listening approval.

## Decision already given

The product owner explicitly confirmed the actual ImmerSIB headphone listening
test was completed, the sound was **good**, and release to the public was
authorized. Do not re-label the accepted ImmerSIB sound as "awaiting owner
headphone approval". Do not request a repeat audition simply because
pre-publication research notes still say `pending`.

This acceptance is **subjective sound/voicing approval for the public Windows
alpha**, not a claim of measured ASIO latency, all-driver field qualification,
real-studio stability, or certified live-event performance. Reports of UI or
native-editor lifecycle bugs remain actionable independently of sound approval.

## Exact released state

- Main integration: [PR #734](https://github.com/masarray/arsonmix/pull/734).
- Native editor lifecycle regression repair: [PR #742](https://github.com/masarray/arsonmix/pull/742).
- First public ImmerSIB Windows alpha: [v0.5.0-alpha.24](https://github.com/masarray/arsonmix/releases/tag/v0.5.0-alpha.24).
- Native GUI/VST3 hotfix release: [v0.5.0-alpha.25](https://github.com/masarray/arsonmix/releases/tag/v0.5.0-alpha.25).
- Latest verified distribution at acceptance: [v0.5.0-alpha.26](https://github.com/masarray/arsonmix/releases/tag/v0.5.0-alpha.26), exact immutable release source `8ec5b8a3e07837c85fb6e06c952cff1621fecc51`. Alpha.26 only changed the license/distribution UX relative to alpha.25; ImmerSIB audio was not retuned.
- The official Windows alpha publisher explicitly compiles with
  `-DARMIX_IMMERSIB_RELEASE_CANDIDATE=ON` and
  `-DARMIX_SONKU_APPROVED_P99_MAIN=ON`. The generic CMake option stays OFF
  unless selected; that fallback is not proof the shipped Windows alpha lacks
  the native plugin.
- Preserve the native effect ID `armix.builtin.imersip.v1`, IMRS v6/v7,
  nested SONK v11/v12 and `.smix` compatibility.

## What is frozen versus independently testable

**Approved/frozen:** the currently released ImmerSIB acoustic tuning,
concert scenes (including Live Concert and Concert Front Row XL), source
range, room response, bass/vocal character, engine identity, and protected
SonKu P9.9/Venue references. Avoid arbitrary DSP retuning, bulk merges of
archived SonKu experiments, or regression-prone GUI rewrites.

**Independent stabilization:** automated native Show dual-editor regression
coverage (PR #744), physical editor-open/VST3-close/reopen verification for
the previously reported alpha.24 bug, recording legally adequate provenance
for venue imagery, and aligning older status documents with public releases.
These do **not** revoke the already given headphone listening acceptance.

This record intentionally does not invent LUFS measurements, benchmark
results, or details of the owner's listening setup that were not supplied.
