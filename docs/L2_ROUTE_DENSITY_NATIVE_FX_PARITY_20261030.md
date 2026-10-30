# ArSonMix L2 ROUTE bank + native FX rack parity (2026-10-30)

Baseline `ad6e4caf20ad21667df023cbaa944be3c9f2e350`. Real 1600×928 screenshots show ROUTE needs more vertical room than FX/SENDS, but its current upper grid is 163px (148px at laptop heights), compressing controls. Native bridge intentionally destroys the MASTER six-slot presentation and replaces it with one "UNAVAILABLE" span plus five empty spans. Routed CH2..10 use one monolithic `＋ ADD FX` (green text) and special green square `native-power` unlike CH1.

## Single integrated milestone
1. **Shared 11-channel fader grid**: ROUTE upper row 224px at normal height, 195px under 790px; 165px emergency under 690px for minimum fader travel. All eleven faders shrink by the same amount, not a per-channel CSS exception. ROUTE inputs/outputs/format/patch use fixed heights and intentional spacing; SENDS and FX mode budgets stay unchanged.
2. **One FX chrome for CH1–CH10**: CH2–10 native routed empty slots now emit the exact same structural plus icon + separate ADD FX span as CH1. Occupied names/empty rows/real power glyph share `insert` and `fx-power` standard classes. Native control targets and FX APPLY/ACK semantics remain the same. No extra DOM reconciliation and no new per-frame painting.
3. **MASTER visual rack**: six visible aligned rows remain mounted, with honest disabled `FX UNAVAILABLE` first slot and disabled `＋ ADD FX` subsequent slots. These are real disabled controls, not pretend audio effects. Native MASTER INSERT PROCESSING DOES NOT EXIST YET in the single JUCE graph, so it would be unsafe to enable a fake FX picker. Dedicated MASTER VST3 ownership/PDC/Show migration needs a subsequent engine milestone.
4. No RT DSP, device ACK, plugin ownership, Show, fader PNG, audio routing, CSS blur/filter/animation or mouse hover work changed.

## Acceptance / CI
- `tests/mixer_shell_geometry_smoke.py`: 1680×920, 1440×768, 1366×768, 1024×720 × FX/ROUTE/SENDS; all 11 fader segments top/bottom equal; ROUTE has less fader travel than FX/SENDS while every live route control fits entirely inside its own bank.
- `tests/ui_smoke.py`: six identical full-sized disabled native MASTER insert rows; CH2 loaded VST3 and five standard ADD FX slots; existing CH1 instance + native ACK preserved.
- `tests/mixer_io_smoke.py`: native routed power uses regular FX power chrome and existing zero-latency bypass ACK.
- `tests/mixer_shell_contract_test.mjs` source contracts; full Chromium gate first; Windows exact-head MSVC/CTest/ZIP after merge; real low-end PC/physical audio FAT #15 remains separate.

Keep DYN1B dynamic/live-channel architectural target; no arbitrary CH11/128 parity claim.
