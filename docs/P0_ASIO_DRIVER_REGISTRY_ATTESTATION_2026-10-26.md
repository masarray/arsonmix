# P0-10 — Control-thread ASIO registration attestation (2026-10-26)

**Base:** `main@8173119471326889b962bf7b410e107f71f61a5b` (P0-09 and concurrent accepted GUI meter branch).  
**Release state:** 0-FX physical audio FAT remains unqualified; [#15](https://github.com/masarray/arsonmix/issues/15) stays OPEN.

## Verified sources, not guesswork

JUCE 8's native Windows ASIO backend enumerates `HKEY_LOCAL_MACHINE\software\asio` subkeys, reads `description` (fallback to subkey name) and `clsid`, verifies `HKEY_CLASSES_ROOT\CLSID\{id}\InprocServer32`, then calls `CLSIDFromString` and constructs an ASIO instance using that driver class. Its GUI-facing device name is **not** a hardware identifier. The x64 application accesses the native-bitness Windows registry view. Microsoft documents registry redirection and `RegOpenKeyExW` as read-only with `KEY_READ`; driver CLSID is a registered software class, not a unique connected physical device.

Authoritative reading:
- JUCE Windows ASIO native source: https://github.com/juce-framework/JUCE/blob/8.0.15/modules/juce_audio_devices/native/juce_ASIO_windows.cpp
- Registry access rights: https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regopenkeyexw
- WOW64 registry views: https://learn.microsoft.com/en-us/windows/win32/sysinfo/32-bit-and-64-bit-application-data-in-the-registry
- CLSID string contract: https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-clsidfromstring

## Implementation

- `AsioRegistrationPolicy`: pure C++20 deterministic policy: exactly one registered entry matching the *chosen JUCE name*, valid canonical brace-format GUID (not a ProgID/all-zero value), nonempty COM inproc server registration, exact registry subkey and canonical CLSID equality for an approved driver generation. Duplicate **display names** are rejected, even if registry order changes.
- `AsioRegistryIdentityProbe`: bounded (128 entries maximum) native read-only Windows enumeration using `RegOpenKeyExW`/`RegEnumKeyExW`/`RegQueryValueExW`. RAII closes every HKEY. Fails closed on incomplete enumeration. Reads only the current process-native registry view to mirror JUCE. It never activates COM objects, loads DLLs, enumerates sound cards, or changes driver rates. Output copies no COM server/DLL path or raw CLSID into the diagnostic report.
- **Saved ASIO startup**: inspect registry before JUCE starts the saved route, then compare the complete registry key+CLSID to the actual name's registry attestation following open. Refuse startup if registry binding disappeared/changed during that transaction. New ASIO manual AUDIO SETUP may explicitly authorize a changed registered driver class; do not silently transfer the old approval.
- **DIRECT restore**: require matching full registry key+CLSID for an ASIO driver *as well as* the existing pinned backend, name, and exact channel maps. Failure stays muted through existing DIRECT recovery controls. ASIO/direct DSP callback checks remain just the existing atomic `DirectRouteFence`; **zero registry, COM, heap, string hashing, new DSP work or ASRC introduced to callback**.
- `status()` carries the approved driver's read-only correlation hash and `asioRegistryAttestation` scope; explicit `SIGNAL CHECK → COPY FULL REPORT` may rescan the current ASIO registry once and explain that CLSID is software-class evidence, not hardware identity. No registry polling in 12Hz status.
- If only driver names are known but no native registry can be verified, the ASIO route is **not** automatically asserted safe or silently mapped to a fallback. Manual WDM/WASAPI selection is still possible.

## Limits and architectural guardrail

This improves **registered driver class continuity**, not a permanent ASIO physical audio-interface ID. A class may operate different physical devices, ASIO4ALL can aggregate endpoints, and a registry class rebind may occur independent of the already-instantiated COM object. An out-of-band hotplug that restarts the same JUCE name but preserves its class is still guarded only by channel-map pinning (not hardware serial). Future physical endpoint correlation must use documented vendor/native APIs or clearly mark unsupported; never use class GUID as proof of shared clock, hardware certification, or permission to use a smaller audio buffer.

No automatic low-latency switching/ASIO device re-opening is introduced. Physical FAT is still essential.

## Validation gates

- New native `AsioDriverClassRegistryAttestation` CTest: valid GUID canonicalization, same registry key and CLSID, changed GUID or subkey, missing/ambiguous duplicate friendly names, missing COM server, malformed/empty/all-zero CLSIDs, uninstalled driver on headless CI.
- Node source contract asserts real startup pre/post and DIRECT restoration integration and on-demand report.
- Existing Windows MSVC Release, all CTest and Chromium smoke must pass on **exact PR-head SHA** before squash merge. Single native test target added to existing build; no additional CI runner/job, no GUI design edits. Record the resulting main commit and update #15.

## Review-driven hardening — startup is a silent transaction

Native review identified two edge cases and the implementation enforces their fixes:

1. **Preflight denial + manual recovery.** JUCE establishes default required capture/render channel counts inside `AudioDeviceManager::initialise(1,2,...)`; returning without that step leaves AUDIO SETUP unable to select a working full-duplex replacement. If a SAVED ASIO preflight fails, the already-registered host callback remains suspended, we initialize these requirements using a transient `DEVICESETUP` pinned specifically to `Windows Audio` (never the rejected ASIO registration), then close any temporary device. No saved device config is replaced or persisted; output remains muted pending explicit selection.
2. **Post-open attestation BEFORE sound authorization.** The comparison of preflight registry key/CLSID to the fresh post-open probe now lives *inside* `approveDirectRoute(expectedAsio)`, ahead of `DirectRouteFence::approve`. A changed registry class can no longer create a short window of authorized live microphone playback before being rejected. Only after all checks pass is the existing O(1) atomic realtime gate enabled.

The Node source contract enforces both invariants. These are fault-path protection changes, not any authorization to automatically switch an ASIO interface while performing.
