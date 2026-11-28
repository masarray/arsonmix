# Systemic GUI stabilization — consolidated defect register

Owner-approved scope GUI-01–26, UX-01–12. Public alpha.33 **HOLD**. This file is a checkpoint, not a release certificate. Each row is OPEN until required evidence is collected on the same integrated candidate. Reproduction scenarios are owner reports / approved regression scenarios, not a claim each was reproduced locally. No physical hardware verification in this session.

Canonical PR [#899](https://github.com/masarray/arsonmix/pull/899), baseline `7e72085095e8dedc8008402389b4d04bd279d7b6`. Working PR [#1010](https://github.com/masarray/arsonmix/pull/1010), source commit `f985d3f70bc7de82503b656cf8552ba7ab11fc42` plus this checkpoint's selective retired-editor cleanup. No canonical integration yet. Existing FactoryFX EQ/Comp/Verb unchanged; KuPik deferred.

## Shared implementation / evidence keys

- T — native routing token/revision contract in `src/NativeSessionToken.h`, native regression tests. [1642d970](https://github.com/masarray/arsonmix/commit/1642d970421fd2ee04d51467cf406bfa25f96317): [Windows scoped routing](https://github.com/masarray/arsonmix/actions/runs/46093123141) PASS, 2/2 native routing tests; previous head, not final proof. [dafbc88](https://github.com/masarray/arsonmix/commit/dafbc88cd5ec033c3b98cc2087fca2f7f02be450) [scoped native](https://github.com/masarray/arsonmix/actions/runs/46094505652) PASS.
- P — catalog snapshot scanPaths, custom remove persistence/rollback, native command and truthful readback, compact folder controls, scan feedback; `src/PluginCatalog.h`, `src/Main.cpp`, `scripts/compose_operator_ux.py`, `web/approved-native-bridge.js`, `tests/v6_operator_io_plugin_sof_smoke.py`. [dafbc88](https://github.com/masarray/arsonmix/commit/dafbc88cd5ec033c3b98cc2087fca2f7f02be450), [UI smoke](https://github.com/masarray/arsonmix/actions/runs/46094505654) PASS; focused native compile on prior head; no real scan persistence proof yet.
- L — stable Audio I/O dialog dimensions, independent content scroll, header/footer retained; bridge and `tests/approved_audio_io_chromium_smoke.py`. Same dafbc88 UI evidence, browser native transport fixture only. Exact visual suite and Windows DPI pending.
- R — `src/Main.cpp`, `src/LiveAudioEngine.h`, `src/NativeDeviceApplyCoordinator.h`: reuse PCM-acknowledged BusPatchEnvelope, CONTROL lease, existing native topology continuity. Save captures under processor exclusion and releases fade before disk writes; no operator mute override. Restore keeps disarmed write guard and only recovers verified previous intent; cancels on superseding device/stop command or timeout. Source f985d3f; new contracts in `tests/approved_native_m2_contract.test.cjs`. Exact native integration pending, actual uninterrupted playback NOT established.
- E — existing instance-owner guards in Main.cpp; selective retired-editor cleanup replaces global editor close after rack file writes. Prior head [focused Windows HWND](https://github.com/masarray/arsonmix/actions/runs/46094505661) PASS: Win32 host ownership/hide/reopen, NOT real JUCE plugin runtime. Latest selective cleanup not yet verified.
- U — no confirmed root/fix on current evidence; open investigation. No guessed workaround.

Status key: P/L/R/T are IMPLEMENTED for their stated shared path, not blanket defect closure; E historical mitigation/partial implementation; U unresolved. All are working PR only; no integrated/physical pass. Each row's evidence/remaining column is the next required verification, in addition to the evidence keys above.

| ID | Stream | Reproduction | Expected | Root cause/evidence | Implementation / status | Required remaining evidence |
|---|---|---|---|---|---|---|
| GUI-01 | A | Save live FX rack | Save without permanent mute; fade only for processor snapshot | Shared save/write disarm guard + consent sets operator mute | R | PCM capture, cancel, archive failure; field audio gap unmeasured |
| GUI-02 | A | Open/restore FX rack with audio playing | Restore verified previous output state | Same shared consent; old path never resumed | R | Actual VST3 load, render and recovery required |
| GUI-03 | A | After failed restore inspect output and Resume | Actionable state, no hidden shutdown | Old consent set emergency mute even for capture | R | Verify timeout and original mute/secondary state |
| GUI-04 | B | Assign External Output from available device | Native ACK and destination PCM | Decimal revision/token mismatch confirmed for matrix path | T | ASIO physical hardware and all route variants |
| GUI-05 | B | Assign CH1/2/3 to available Maono VC | Valid assignment accepted, real output PCM | Native token parser rejected canonical uint64 string | T | Maono VC field retest |
| GUI-06 | B | Click available output cells; inspect toast | Real accepted state or useful rejection | Same token contract on output route; other failures not yet isolated | T | Repeated route changes on actual device |
| GUI-07 | C | Open Plugin Manager; add/remove custom folder | Actual default/custom scan folders persist | Composer omitted list; native snapshot omitted scan paths | P | Real restart and scanner filesystem test |
| GUI-08 | C | Run Scan/Rescan | Plain actionable progress and result | Native JUCE/catalog jargon in GUI | P | Actual vendor scan failure and cancellation |
| GUI-09 | C | Inspect Plugin Manager controls | Recognizable consistent buttons | Affordance complaint, not a confirmed native bug | P | Production visual review; further styling if needed |
| GUI-10 | C | Select plugin row | Thin selected border | Visual complaint; root unconfirmed | U | Inspect actual selection state and DPI |
| GUI-11 | E | Switch Audio I/O tabs | One fixed, compact dialog shell | Tab-specific width and scrolling shell | L | Production screenshots at DPI/window sizes |
| GUI-12 | E | Open Outputs matrix with many ports | Content-only scrolling; no shell overflow | Same shell layout | L | Long port names and actual device inventory |
| GUI-13 | E | Scroll Inputs/Outputs and close | Close stays visible and usable | Header scrolled with dialog | L | Busy native Apply retains existing safety block; field verification |
| GUI-14 | D | Open ImmerSIB after session restore | Editor opens or accurate failure | Historical; current root not confirmed | E | Actual native plugin/editor lifecycle, no generic success |
| GUI-15 | D | Open VST3 while ImmerSIB editor exists | Independent instance windows remain accessible | Historical; broad rack cleanup also confirmed separately | E | Actual ImmerSIB and third-party VST3 coexistence |
| GUI-16 | D | Close/reopen same editor repeatedly | Same retained instance, no orphaned HWND | Historical; existing ownership/relay guards | E | Real JUCE/WebView2, repeated open/close |
| GUI-17 | D | Move insert slot then reopen | Stable instance identity and updated binding | Historical; existing token-based ownership | E | Actual repeated move/reopen and bypass/remove |
| GUI-18 | C | Open Audio Setup then Plugin Manager | Catalog remains present | Unconfirmed; source now has authoritative scan paths | P | Cross-subsystem actual catalog regression |
| GUI-19 | C | Select available VST3 and Add | Accepted insert instance and audible effect | Historical; exact root unconfirmed | U | Real third-party VST3 insertion and PCM |
| GUI-20 | B | Record CH1 FX rack; open/apply Audio Setup | Same accepted rack unless explicit change | Historical; existing binding retains authored session | U | Actual device rebind and exact instance/rack state |
| GUI-21 | B | Patch WIRE and send signal | Destination PCM matches patch | Historical; not all WIRE paths explained by token bug | T | Native end-to-end matrix, field signal |
| GUI-22 | B | Add Track repeatedly | New track and graph readback consistent | Historical, root unconfirmed | U | Real create/render/delete cycles |
| GUI-23 | B | Add BUS then route/send | BUS native graph and PCM | Historical, root unconfirmed | U | Real create/render/delete cycles |
| GUI-24 | B | Save session and restart | No spurious NEEDS REMAP with same endpoint | Historical; genuine hardware change must still require remap | U | Same-endpoint restart + changed endpoint negative test |
| GUI-25 | B | Restore/Rebind after changed device | Valid rebind, verified graph, explicit failure otherwise | Historical, root unconfirmed | U | ASIO change/disconnect/reconnect |
| GUI-26 | B | Rapid native commands and late ACK/revision | Authoritative state, stale ACK rejected | Token encoding confirmed; systemic audit incomplete | T | Stress/revision/cancellation/endurance |
| UX-01 | E | Compare control dimensions | Compact consistent controls | Visual complaint | P | Full production component audit |
| UX-02 | E | Hover/press/disable/loading/select | Consistent semantic states | Visual complaint | P | Full state matrix and contrast audit |
| UX-03 | E | Open each modal and inspect Close | Same accessible Close shell | Audio I/O close scrolled; shared design elsewhere | L | All modal focus/keyboard/close flows |
| UX-04 | E | Scroll long table | Header stable; only rows scroll | Audio I/O shell overflow confirmed | L | All table/header/row scroll behavior |
| UX-05 | C | Read Plugin Manager at 100–150% DPI | Readable compact font | Visual complaint | P | Physical DPI screenshot review |
| UX-06 | E | Compare plugin manager fonts | Inter consistent where approved | Plugin manager primary controls used Segoe UI | P | Remaining global font audit |
| UX-07 | E | Inspect selected borders | Thin restrained borders | Visual complaint, exact selector unconfirmed | U | Production visual review |
| UX-08 | E | Resize/open dialogs | Compact non-bulky layout | Audio I/O size changed by tab | L | Other dialogs and supported display sizes |
| UX-09 | E | Compare BUS and input strips | Clear restrained distinction | Visual complaint | U | Preserve approved mixer design; actual review |
| UX-10 | E | Assign routing/sends | Intuitive mapping, actual state visible | Token contract fixed for output route; workflow audit incomplete | T | Real task completion across WIRE, SOF and outputs |
| UX-11 | E | Trigger plugin/audio operation failure | Plain actionable error | Scanner jargon and rack MUTE message confirmed | P/R | Remaining native errors and actual failures |
| UX-12 | F | Scan, remove folder, restore, timeout | Truthful pending/success/failure | Old scan text and rack status not operation-specific | P/R | Cross-subsystem native receipt + endurance |

## Reference decisions (not a redesign)

- [Ableton Live 12 — Using Plug-ins](https://www.ableton.com/en/live-manual/12/using-plug-ins/) and [Windows VST sources](https://help.ableton.com/hc/en-us/articles/209071729-Using-VST-plug-ins-on-Windows): expose real system/custom locations, explicit scan, independent plugin windows. Do not copy layout or assume their real-time plugin capture implementation.
- [Cubase Audio Connections Outputs](https://www.steinberg.help/r/cubase-pro/16.0/en/cubase_nuendo/topics/settings_dialog/settings_dialog_audio_connections_outputs_tab_r.html): explicit output bus and port mapping. ArSonMix retains its native graph and actual routing ACK.
- [WAI APG modal dialog](https://www.w3.org/WAI/ARIA/apg/patterns/dialog-modal/): accessible close and focus; limit scrolling to content. Existing approved compact mixer language takes precedence.
- Existing repository design contracts: docs/ARD_STUDIO_STYLE_AUDIO_IO_20261125.md, docs/workstreams/PRO_PLUGIN_MANAGER_WORKSPACE_20261128.md. No new renderer, DSP or state manager.

## Acceptance checkpoint

1. Save rack: implementation only; verified PCM capture/recovery needed.
2. Restore rack: implementation only; original mute, failure, cancellation, vendor preparation and timeout need real runtime test.
3. External output: scoped native prior-head pass; exact hardware retest.
4. Audio I/O output matrix: token fix; actual device PCM and readback pending.
5. Plugin folders: source + transport tests; actual persistence/scan/insert pending.
6. Editor lifecycle: scoped HWND harness pass prior head; actual editor integration and stress pending.
7. Layout: source + fixture; production GUI screenshots, DPI and keyboard focus pending.
8. Design consistency: partial implementation; global audit remains.
9. Regression: latest native/full UI/endurance and cross-subsystem workflow pending.
10. Integrated Windows FIELD portable with exact source/provenance/checksum: NOT AVAILABLE. Do not dispatch installer/release or mark physical FAT. No final candidate while unresolved P0/P1 remain.

Next: inspect latest exact-head CI and review, fix regressions (including real capture/restore and editor harness), complete outstanding rows, reconcile #1010 to #899 only with expected-HEAD protection and required gates, qualify one FIELD candidate. Do not repeat previously qualified FactoryFX work or bypass mandatory field validation.
