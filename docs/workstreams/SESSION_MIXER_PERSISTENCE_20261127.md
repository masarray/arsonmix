# Session Mixer: MON preference and FX rack file reliability

Base: canonical integration #899 at 32acb5ee47d09f01c53ceba4b25cb9b7f4eda87a; main at 379e867985bb6bab8e07297ba7a8c44925b22b2d. Scope is isolated, not a release lane.

## Findings
- Native secondary-output.json already persists verified endpoint ID when armed, but not on successful connect-only. MAIN MON selector ignored preferredOutput after restart and selected None until active. The separate settings dialog already consumed the preference.
- Native rack archive already implements whole rack save/open and single-effect insert/deactivate. Channel header menu omitted these operations; successful native notices were discarded by the approved bridge. Non-destructive single-effect export was missing. File-picker cancellation gave no feedback.
- Native .smix covers accepted graph, routing, FX binary, channel controls and hotkeys. Automatic capture is intentionally limited to disarmed idle/shutdown to protect vendor state and RT. No new auto-arm or fake browser persistence is introduced.

## Acceptance / test plan
- Exact saved endpoint displayed as saved/off after restart, explicitly enable with verified inventory; missing/replaced endpoint stays off. CONNECT and ENABLE persist native identity; failed writes visible.
- Header/rack menu offers save/open/insert; individual FX can export without removing it; deactivation remains an explicit separate operation after successful archive write. Native receipt only after operation returns.
- Preserve disarmed capture gate, registered VST3 identity, graph/roster/instance checks, atomic archive behavior, one approved GUI and one PCM graph.
- Regression contracts added to existing approved native M2 suite. Native compile, Chromium interactions, Windows restart and hardware confirmation remain required; no PASS claimed at source creation. No installer requested.
