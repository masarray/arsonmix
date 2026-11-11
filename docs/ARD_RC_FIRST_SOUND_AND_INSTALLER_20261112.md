# ARD — RC First-Run Audio, FX Interaction and VB-CABLE Installer

**Revision:** 2026-11-12 · **Status:** implementation architecture / incremental gate (not shipped RC).  
**Companion:** [PRD](PRD_RC_FIRST_SOUND_AND_INSTALLER_20261112.md). **Integration target:** [PR #579](https://github.com/masarray/arsonmix/pull/579).  
**Source provenance:** `src/LiveAudioEngine.h`, `src/Main.cpp`, `src/NativeDeviceApplyCoordinator.h`, `engine/device/WasapiEndpointIdentityGuard.h`, `engine/device/EndpointIdentityPolicy.h`, `web/approved-native-bridge.js`, `site/preview-v2/assets/mixer-builtin-fx-preset-bar.js`, `site/preview-v2/assets/mixer-contextual-buttons.js`, `installer/ArSonMix.iss`, `scripts/build_verify_windows_installer.ps1`.

## 1. Existing architecture and verified defects

- `LiveAudioEngine::start` suspends direct output fence, sets monitor false, then opens 2-in/2-out using default selection on unsaved first run. An automatically selected endpoint can be virtual/ambiguous or have no source; opening alone does NOT establish verified PCM + authorized MAIN audio.
- Saved explicit ASIO device selection intentionally uses nonfallback init and a registry preflight; preserve this. `knownVirtualCableFeedbackPair` prevents one obvious loop by name; this is a **secondary heuristic**, not a stable identity check.
- Windows identity resolver already exists (`WasapiEndpointIdentityGuard` + `EndpointIdentityPolicy`): leverage and extend; do not implement a second MMDevice name-to-id owner.
- `NativeDeviceApplyCoordinator` owns transactional manual device application; first-run automation must pass through equivalent serialized control transaction, guarded readback and rollback.
- PR #579 is the active GUI repair surface. Do not create a second preset store, FX adapter, picker owner, wheel scheduler or window drag manager.
- `installer/ArSonMix.iss` currently uses `PrivilegesRequired=lowest`, per-user install, controlled app close, no embedded VB-Audio driver. A driver install demands an independently consented elevated step, not arbitrary `runas` on every app update.

## 2. State machine and single ownership

```text
APP START (native control thread, never audio callback)
  |
  +-- saved explicit ASIO/WASAPI profile?
  |      -> resolve old endpoint ID + driver binding -> native open/readback
  |      -> no silent substitution; failure -> recovery panel; output remains safe
  |
  +-- pristine first run?
         -> DISCOVER (read-only MMDevice endpoints + JUCE supported backend)
         -> RANK (pure deterministic policy; unknown == not accepted)
         -> PROPOSE (CABLE Output eCapture + verified physical eRender only)
         -> RESERVE (snapshot old device/graph/arm and current generation)
         -> APPLY (one NativeDeviceApplyCoordinator / JUCE manager)
         -> VERIFY (endpoint IDs, stream open, two output pins, sample clock,
                    source input pins, same session generation, loop check)
         -> ROUTE (one native logical graph compiler/ACK; do not override explicit
                   SOURCE ONLY or existing user wires)
         -> AUTHORIZE (existing native direct fence, only proven MAIN destination)
         -> PERSIST (only after accepted native readback, not JS request)
         -> READY (actual hardware readback, signal if source contains PCM)
         -> on any failure: ROLLBACK / SAFE IDLE with one actionable status
```

This is an **opt-in default at first-run**, not a permanent background endpoint changer. UI still offers professional explicit Audio I/O, ASIO priority and manual named role choices. The observer may not silently arm after unrelated device reconfiguration or override user mute. `SOURCE ONLY` intentionally remains no MAIN output until user reconnects.

## 3. Data/identity contract (C++20 non-RT)

Separate deterministic `FirstRunDevicePolicy` (pure value selection) from native device discovery and JUCE side effects.

```cpp
struct Candidate {
  std::string endpointId;       // real MMDevice ID; never synthesized from name
  std::string friendlyName;
  Direction direction;          // capture / render
  Class kind;                   // standardVirtualCable / physical / otherVirtual / unknown
  bool active;                  // actual MMDevice active status
  bool supportsStereo;          // native capability verified; unknown -> false
  bool defaultMultimedia;       // actual Windows default eRender binding
};
struct Decision { Outcome outcome; std::string captureId, renderId; Failure reason; };
Decision selectFirstRun(std::span<const Candidate>, bool explicitSavedProfile);
```

Selection requirements:
- Reject missing/duplicate IDs and ambiguous names in the JUCE name-based backend mapping; never take `vector.front()` as a substitute.
- Only one uniquely classified standard VB-CABLE capture is permitted. Recognize actual vendor/device metadata, stable instance properties and direction; friendly-name heuristics alone must **not** authorize audio.
- Render must be verified *physical* and support stereo. Prefer uniquely matched pre-install physical default, otherwise unique current physical default, otherwise require one explicit selection; **do not just pick the first headset** when multiple choices exist.
- Preserve identity across preflight and post-open; abort on changed generation, stale token, different pins, ASIO or backend drift.
- Treat read-only probe `READY` separately from `AUDIBLE`; no-signal capture is not a device failure. Never automatically redirect OS-wide playback defaults.

## 4. GUI interaction implementation contract

One shared modeless utility manager in `mixer-contextual-buttons.js`: actual viewport center first open, bounded drag, authoritative outside **pointerdown** dismissal (excluding target child, active dropdown, native dialogs and pointer-captured drag); subsequent open within same session may restore position. Editor FX remains modeless, not a transient utility. Reuse one compact close-button CSS owner; no new shadow DOM renderer.

`mixer-builtin-fx-preset-bar.js`: offline `workspaceToken` accepted by same native `BuiltInFxUserPresetStore`, exact `instanceId`/`catalogId`/`graphRevision` match and disk ACK. Never tie Save As availability to `deviceEpoch>0`; distinguish logical preset state from audio Processor lease. Recall requires native ACK and accepted FX snapshot, no dry fake success.

Wheel controller: pointer-over knob or slider, delta normalized by `deltaMode`; wheel -> input event -> existing parameter dispatcher; coalesce updates, fine-step on Shift, prevent page scroll only when a control can change, and no full editor rerender. No rotary-trigger side effects. Vocal Throw's performance button does not disable/reset for unrelated knob edit.

FX Picker: after accepted Add FX and exact native graph readback, close picker and open editor for the inserted instance; never open the wrong track or assume positional slot. If insert fails or times out, keep operator feedback and no ghost FX. All playback, SAVE, and FX actions remain available in logical/offline mode where applicable.

## 5. VB-Audio packaging + privilege boundary

Official [VB-Audio license](https://vb-audio.com/Services/licensing.htm) permits the **standard** VB-CABLE package AS IS in another product, provided the app/installer/website clearly credits VB-Audio, links `www.vb-cable.com`, discloses donationware and professional usage requirements; A+B and C+D cannot be redistributed. [Driver manual](https://vb-audio.com/Cable/VBCABLE_ReferenceManual.pdf) warns the newly installed driver may become Windows default output, making speakers silent. [Official support](https://shop.vb-audio.com/en/contact-us?tspage=400) requires admin and reboot for driver setup.

**Never embed a third-party ZIP by unverified URL or fabricate its checksum.** Store a reviewed manifest with official download origin, exact driver package bytes SHA256, version, license notice, and staging hash. CI must reject missing/mismatched package. Installer's per-user app install remains nonadmin; an explicit `Install VB-CABLE` checkbox/page prompts for separate UAC, writes no credentials, does not force a default playback change. On UAC cancel/exit code error the user can still run the app with actual existing sources.

Installer lifecycle:
```text
Check driver identity/version/health
   installed valid -> skip
   absent -> user consent -> verify AS-IS signed official package + hash
          -> extract exactly reviewed payload -> elevate vendor setup
          -> verify exit code; if reboot required mark PENDING_REBOOT
          -> after reboot re-enumerate MMDevice and validate endpoint IDs
          -> only then offer first-run route configure
   partially installed/mismatch -> safely stop + offer manual repair; NO auto uninstall
```

Use process-safe install sentinel/manifest so repeated setup doesn't relaunch vendor setup indefinitely. Do not delete VB-CABLE during ArSonMix uninstall: other apps may depend on it. Test clean, existing, denied UAC, canceled, reboot pending, no driver, driver partial failure, stale output default, upgrade rollback, and restricted user. No silent administrator elevation. Restore remembered speakers only after user intent / verified identity; do not call undocumented OS policy methods to force defaults.

## 6. Test gates and minimal CI

**Fast portable**: pure policy C++ tests no JUCE/Windows, existing `rc_operator_workflow_contract.test.mjs`, normalized GUI wheel + native preset request tests; assert no duplicate IDs, virtual output selection, malformed source pair, explicit ASIO replacement or permission bypass.  
**UI**: existing 2-viewport/1024×576 Chromium smoke; pointer outside/drag, picker readback, wheel and dropdown contrast.  
**Windows**: exact source SHA full MSVC/CTest, JUCE open/close and verified MMDevice ID fixture, sample-rate mismatch, 1/2/4/8 hardware output capability fixtures, no crackle claim.  
**Installer**: independent vendor package checksum, per-user/no-elevation app install and explicit separate UAC process, driver absent/existing/reboot/rollback failure, in-place upgrade, private `.smix` preservation, manifest matching EXE + GUI.  
**RC**: compare `main`, all intended PRs, exact SHA, installer and direct landing-page ZIP. Hardware field-audio FAT separately PENDING with no blocking demand on users lacking ASIO hardware.

## 7. Ownership/reconciliation and state journal

- Keep #579 as the current UI/FX integration stream; include these docs in the same PR to avoid concurrent rewrite.
- Maintain native source-of-truth for accepted graph/readback and device authorisation; never add a JS authority or second independent renderer.
- Stop before publishing any RC if driver redistribution/provenance or installer/first-run native gates are incomplete.
- Every PR commit must state exact test evidence, which behaviors have a physical proof, and what remains unimplemented.

## 8. Implemented checkpoint (2026-11-12; PR #579)

| Component | Evidence | State |
| --- | --- | --- |
| PRD and this ARD | Reviewed product journeys, no hidden output-arm bypass, vendor licensing and error matrix | **Committed** |
| Portable policy | `engine/device/FirstRunEndpointPolicy.h`: pure first-run candidate ranking, preserved saved profile, unique active ID + name, trusted physical render, stereo output, safe fallback Player-only | **Implemented, not yet called from Windows MMDevice adapter** |
| Native C++ fixtures | `tests/first_run_endpoint_policy_test.cpp`: explicit ASIO, valid VB capture/physical speaker, virtual default, no driver, duplicate name/ID, ambiguous/current/historical defaults, unverified vendor, unplug, mono output | **Implemented** |
| CI gates | GNU C++20 `-Wall -Wextra -Werror` preflight before paid MSVC, plus new CMake CTest target | **Committed; [quick native green](https://github.com/masarray/arsonmix/actions/runs/43077083147)** |
| Existing FX repairs | [PR #579](https://github.com/masarray/arsonmix/pull/579) FX/preset/picker/window work | **Fast native, targeted preset and UI smoke green at source SHA `e6fa363`** |
| Windows device auto-select | Read actual MMDevice IDs, driver class, pre/post installer defaults, open JUCE, verify/rollback, route and authorize MAIN | **NOT IMPLEMENTED — no automatic first sound claim** |
| VB-Audio package | Verify official package bits/hash/signature, preserve AS-IS licence, consent/UAC/reboot/repair | **NOT IMPLEMENTED — no driver bundled in installer** |
| RC and landing page | Exact-SHA full Windows MSVC/CTest, hardware-free installer scenarios, ZIP hashes, release tag and landing direct download | **NOT QUALIFIED / NOT PUBLISHED** |

**Important source distinction:** An `Endpoint.kind=physical` candidate must come from a trusted Windows hardware-classification adapter. It MUST NOT be populated merely from `Speaker`, `Realtek`, a name prefix or first item. The current code intentionally has no adapter that could fabricate this classification. The policy is now testable but is not a functioning end-user first-run fix until that adapter, JUCE APPLY and output graph ACK are integrated.

**Last known quick CI** (exact PR head `e6fa363aa8eef8041ee3d6362fff008532f59e31`):
- [native quick workflow](https://github.com/masarray/arsonmix/actions/runs/43077083147) — success (full PR MSVC job skipped by intentional CI policy)
- [targeted native FX presets](https://github.com/masarray/arsonmix/actions/runs/43077080762) — success
- [UI smoke](https://github.com/masarray/arsonmix/actions/runs/43077080754) — success
- [Harmony DSP fast](https://github.com/masarray/arsonmix/actions/runs/43077080903) — success

This milestone must remain a **draft** until real sound path and third-party driver packaging criteria are implemented. Do not merge it as an RC or claim a verified audible default route.
