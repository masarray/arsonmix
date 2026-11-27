# ArSonMix Windows installer dependency recovery — 2026-11-23

## Symptom and root cause
Windows Sandbox may show **Checking VB-Audio VB-CABLE** permanently after copying ArSonMix. The bundled, official VB-Audio driver setup opens a visible **Install Driver** button. Inno previously ran a hidden PowerShell helper with `waituntilterminated`; the helper used `Start-Process -Verb RunAs -Wait`. Setup could not launch ArSonMix until the driver GUI (or its child processes) exited.

## Accepted behavior
- The verified ArSonMix application and FX files install independently of optional VB-CABLE. The installer no longer invokes the vendor GUI. Start Menu offers **Install optional VB-CABLE (Administrator approval required)** for users who want a streaming virtual device; the vendor's interactive installation, UAC, restart and license requirements remain respected. No undocumented silent switches, trust-store edits, driver uninstall or default-endpoint changes.
- Interactive WebView2 preflight still uses the Microsoft-signed Evergreen bootstrapper only when missing, but caps the child-process wait at **120 seconds** and records `NEEDS_ATTENTION` on timeout. It does **not kill** Microsoft setup and retains temporary files while it is running.
- The opt-in VB-Audio helper caps its own vendor-process wait at **180 seconds**, records `REPAIR_REQUIRED` on an indeterminate outcome and retains files used by a still-running installer. It refuses to re-run automatically after uncertainty. Already-present and pending-reboot PnP checks remain.
- The native Windows CMake build uses `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded` (static CRT); a separate VC++ Redistributable is not an installer prerequisite for this built executable. Reassess only if native linkage is changed.
- Silent installs/auto-updates remain free of driver elevation and first-run launch; Studio files, user routes, ASIO, vendor VST3 and sonic PCM are not changed.

## Testing and limitations
Source policy: `node --test tests/windows_installer_contract_test.mjs`. Windows exact-commit FAT/real Inno smoke is required before shipping a new installer. A Sandbox Windows driver installation cannot prove ASIO hardware. Signed driver UAC confirmation and the vendor Install Driver GUI cannot truthfully be called fully unattended.

## Previous stuck installer recovery
If the vendor driver dialog is still open, close it normally. If the main Setup remains stuck, end only the **Setup** process (not a currently running mixing session), then try a new validated installer or a portable archive; do not rerun VB-CABLE installer blindly. Inspect Windows Device Manager and reboot after a completed vendor installation.
