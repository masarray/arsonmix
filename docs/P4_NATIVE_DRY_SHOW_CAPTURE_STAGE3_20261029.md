# P4 stage 3 — Save an acknowledged native DRY Routing Show

**Scope:** A user-triggered, usable native SAVE path for an actual ACK-verified 10 INPUT + 4 AUX route with **zero** VST3; no simulated/localStorage state. Not native live recall.

## Runtime path
1. Enable a real verified DIRECT full-duplex device and APPLY Routing LAB. The audio engine publishes a prepared graph only after native device-mask, epoch and route revision checks.
2. In Routing LAB, adjust live CH faders/PAN/MUTE/SOLO as needed, then MUTE OUTPUT before selecting **SAVE DRY SHOW**.
3. `NativeLiveShowCapture::captureDry` runs on the JUCE message thread, reading `LiveAudioEngine::status()` and only its `dryRoutingLab.acceptedRoute` and `channelControls` native ACK. No JS draft, browser localStorage, remote path or guessed VST3 state is read.
4. Capture verifies `ready/active`, current device epoch, revision, actual physical enabled input mask, zero FX, exact 10CH controls and 4 AUX. The name and timestamp are generated natively. It preserves source NONE vs BUS distinction, sparse physical indices, exact linear faders (including 0 / -infinity), PRE/POST CH and AUX sends, AUX levels/switches, master level/mute/mono. On any error the previously accepted snapshot is unchanged.
5. `NativeShowSaveWorker` receives a **private copy** of the validated snapshot and commits one new Show generation under the portable `Studio/Shows` workspace. Staging + verification + atomic HEAD policy are reused from P4 stage 2; it reads back and compares the entire saved snapshot before showing success. IO/hashing never runs on callback or WebView paint thread. Saving and USB backup are mutually excluded.
6. The GUI only reports success from the native worker ACK. It does not arm audio, auto-apply physical I/O, auto-unmute or restore a Show. A stale/unsupported request is rejected.

## Non-negotiable guardrails
- **DRY-only**. Running VST3 state capture is explicitly refused. Muted output alone is not yet proof every vendor processor is quiescent: vendor-specific `getStateInformation` affinity, safe callback handoff, state blobs, Orbit state, hardware remap, and atomic live restore/undo remain future gates.
- This save is not full production scene recall and does not qualify #15 zero-FX hardware crackle. No arbitrary physical ports or VST3 binaries are activated from a loaded Show on a different PC.
- One existing JUCE device/engine and immutable published route. No filesystem scan, device call, plugin lifecycle operation, lock, heap allocation or JSON conversion on the audio callback.
- STOP LAB remains mute-first and restores the original CH1 path. All four AUX returns stay independent from the ten INPUT strips.
- Schema V3 carries an optional `busReturn` boolean per strip, allowing new recordings to distinguish a channel-to-channel BUS return from an unassigned NONE. Older V2/V3 snapshots without the field remain safely disconnected by default.

## Automated verification
- Native `P4NativeAcknowledgedDryShowCaptureAndStorage` CTest constructs a representative acknowledged routing session with sparse hardware mask, MONO/STEREO/BUS input types, 2 concurrent AUX, live gain 0, fader/PAN/MUTE/SOLO, PRE/POST, master and subsequent on-disk readback. Reject stale revision, output not muted, VST3, and disappeared physical input without changing last-good snapshot.
- Existing 47 native CTests + new test; Windows MSVC full app, native contract tests and packaged artifact; Chromium UI smoke (new Routing LAB button/event must not regress GUI).
- Hardware FAT still required at 48 kHz with physical 1/2/10 inputs, 128/256/480 frames, sparse ASIO/WASAPI masks, save/stop/restart/disconnect/USB backup, and #15 crackle isolation. PASS in synthetic CI is not a hardware claim.

## Next engineering milestone
True vendor VST3 state capture requires a deliberate callback quiescence/epoch handshake and correct message/vendor-thread affinity without destroying FX tails. Then integrate verified states into Show commit, preflight plugin availability, prepare off RT, and do an explicit muted atomic restore with rollback and native ACK. This stage deliberately does NOT enable old UI simulated scenes as native recalls.
