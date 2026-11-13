# DLMS Professional Workflow & Safety Contract — M12 (2026-11-14)

**Product**: ArSonMix — Digital Loudspeaker Management System (DLMS) workspace.
**Unrelated**: Harmonice is the built-in vocal-harmony audio FX in the channel FX Rack. Its DSP test does not qualify DLMS.

## Verified competitive patterns and what is actually adopted

| Professional reference | Verified published workflow | ArSonMix DLMS decision |
| --- | --- | --- |
| d&b R1 | Offline design without attached amplifiers, separate device and group views, device system check | Keep an always-visible *DESIGN ONLY* identity and five purposeful pages; never imitate hardware telemetry |
| Xilica Designer | Project is authored offline, then explicitly mapped to physical device identity, reviewed and deployed separately | Require operator-selected exact ASIO port identity; device epoch and graph changes invalidate pending review |
| Lake Controller | Recall Compare detects differences between saved and active configuration; bulk recall has a distinct high-impact confirmation | Preserve the previous DSP design until explicit mapping is accepted; do not silently map ordinal 1 to a different amplifier |
| Powersoft ArmoníaPlus | Device-aware speaker-way routing, per-way crossover/EQ/delay/gain/polarity, RMS and peak protection | Separate logical crossover sections from output DSP and physical port inventory; prioritize measured electrical calibration |
| Q-SYS | Loudspeaker protection requires manufacturer-derived RMS/peak voltages and high-pass constraints | Digital dBFS ceiling and a 'limiter on' checkbox never constitute safe speaker hardware authorization |

Reference links:
- https://www.dbaudio.com/global/en/products/software/r1/
- https://support.xilica.com/en/articles/3955586
- https://mediadl.musictribe.com/media/PLM/data/docs/Lake-Controller/Lake%20Controller%20Operation%20Manual_1.7.0.pdf
- https://www.powersoft.com/en/technologies/dsp-technology
- https://help.qsys.com/Content/Schematic_Library/lua_speaker.htm

## Owner and signal path: one native authority

```
ArSonMix native accepted MAIN (post FX and post master processing)
  -> CONTROL prepared speaker DSP (HPF/LPF crossover, PEQ, delay, polarity, gain)
  -> isolated offline diagnostic PCM and atomic readback
  -> speaker output port intents (MUTED, not physical)
  -> NATIVE binder checks actual JUCE/ASIO inventory + same device epoch
  -> accepted speaker draft ACK + exact graph readback (hardwareApplied=false)
  -X-> physical loudspeaker output   [NOT AUTHORIZED]
```

No WebView JSON may declare trusted actual ASIO callback ordinals, DAC full-scale voltage, amplifier gain, or physical output authorization. The single existing native JUCE callback remains the future sole physical writer. The M10 diagnostic is bounded and isolated, not a parallel live sound path.

## Current M12 interaction rules (code implemented in branch)

1. **Design**: System → Crossover → Matrix → Processing → Alignment. Start offline, no auto-patched output.
2. **Inspect**: Obtain actual port identities from the native JUCE output inventory; MAIN/MONITOR/EXTERNAL occupied ports are not selectable as speaker destination.
3. **Option A — New hardware layout**: *Use verified ports* intentionally clears all prior routing/DSP and requires confirmation.
4. **Option B — Keep sound-system design**: *Reassign + keep DSP* opens a compact per-output review. Every connected/tuned/positioned old output requires an explicit unique new port. Unmapped untouched placeholders may be dropped. PEQ, delay, crossover, gain, polarity, limiter intent and geometry are preserved **only for selected ports**; every result is **MUTED**. No guessed ordinal, partial commit, or hidden auto-patch.
5. **Validate**: Reassignment invalidates old accepted design revision and needs a separate user-initiated native Validate; native ACK must be followed by a subsequent readback of every DSP field. Device epoch/graph or occupancy changes cancel pending mapping review.
6. **Save / Open**: The existing `.smix` codec stores a versioned, disconnected LMS draft with DSP values only. It strips port identity, graph epoch, hardware unmute, and claimed calibrated voltage values. Opening it cannot auto-arm a speaker. **Important limitation:** The GUI's direct restored-DLMS-editor presentation is not yet wired to the session import event; native session storage and offline model tests are separate completed contracts.

## Status taxonomy — never claim a capability that is not present

| Label | Allowed meaning |
| --- | --- |
| DESIGN ONLY | Editable offline data; no port authorization or acoustic measurement |
| INSPECTION REQUIRED | Port inventory absent/stale |
| REVIEW REQUIRED | Saved design needs explicit operator port mapping |
| ACK PENDING READBACK | CONTROL accepted a typed draft, but state not yet reconciled |
| VALIDATED DRAFT | Exact same epoch/graph/revision and DSP fields verified; hardwareApplied **false** |
| CALIBRATION UNVERIFIED | Actual DAC/amp/driver limit evidence missing; **no speaker LIVE** |
| LIVE | **Forbidden for this milestone**; requires independent FAT and typed native arm transaction |

### Tests and CI boundaries

- **DLMS focused** `.github/workflows/dlms-fast.yml`: seven portable C++20 tests for crossover/PCM, callback allocation, output reservation, post-MAIN witness, native ACK and stale revocation, real ASIO binder and disconnected session draft; two JS suites for the five pages, review transaction, stale inspection, and model math; job scope contract.
- **Shared integration**: native Windows MSVC/CTest tests JUCE session persistence, host/build ABI, and no regressions; full product Chromium ensures the rest of the mixer survives. A shared PAN test failure is **not** a failure of speaker DSP, but it must be resolved before full-product release.
- **Harmonice**: vocal harmony fast tests do not run for the DLMS feature branch. They remain in a distinct vocal-effect workflow and cannot be reported as DLMS PASS.

### Remaining gates for a physically usable professional DLMS

1. Read-only GUI session-recovery presentation with explicit `NEEDS_REMAP` and comparison to native device inventory; no automatic rematch by ordinal.
2. Separate **trusted measurement provenance**: DAC Vrms measured at a known gain setting, amplifier gain and voltage limits from a documented manufacturer profile, power handling/HPF, reviewable measurement date and instrument identity. Valid numeric fields alone are **not** proof.
3. Typed physical speaker admission lease, exclusive MAIN/MONITOR/EXTERNAL pin ownership, atomic device-change revocation, rollback, measured limiter readback and emergency mute through the **single** PCM callback. Avoid a second output engine.
4. Actual multichannel ASIO/amplifier/dummy-load FAT: calibration tolerance, silence on exception/stale epoch, crossover spectral analysis, latency, xrun/device restart, driver disconnect, channel polarity and physical pin labeling.
5. Only after these and full product qualification may production operator workflow expose LIVE. No estimate or simulated meter is a substitute for field evidence.

## Next implementation focus

Keep UI compact and operational, not a massive wizard: display `DESIGN`, `INSPECT`, `REVIEW`, `VALIDATED` as explicit states; one output map with selected source way, exact port label, mute and short status. A/B/compare changes before admission. No fake color-coded 'safe' LED, no optimism after stale ACK, and no redundant FX-specific CI gate.
