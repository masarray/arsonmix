# ArSonMix GUI P0 — Proportion, Center Detent, and Console Legibility
2026-11-02 · PR #278 reference · **browser UX only; no native/ASIO certification**

## Reviewed operator patterns
- [Fender Studio Pro / Studio One Console](https://fenderstudiopromanual.fender.com/en/Content/Mixing_Topics/The_Console.htm): normal and narrow modes preserve mixer readability, with focused inserts and sends when expanded.
- [FL Studio Mixer](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer.htm): compact channel strips preserve long faders and direct routing without making insert slots full-height empty panels.
- [FL Studio effects slots](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/mixer_plugin.htm): ordered, individually bypassable slots, one compact row per effect. Do not copy branded UI art.

## P0 physical geometry contract
- At 1366×768 with ROUTE and FX expanded and SEND folded, the FX slot viewport fits at least four 22px effect rows plus its heading, while fader height is approximately 258px and physically dominates the lower strip.
- The fader scales with window height, not with whether ROUTE was collapsed. Collapsing ROUTE transfers its pixels to FX visibility, not to the fader; all strip rails and pinned MAIN remain aligned.
- Opening detailed SEND at constrained viewport height reduces fader travel deliberately to keep the four FX rows visible. Fold SEND again to prioritize a longer fader. Never silently crop the SEND fields.
- FX itself collapsed can transfer space to SEND; simultaneous FX and SEND collapse falls back to the fader.
- Native original fader bitmap, physical PAN thumbnail and button materials remain authoritative.

## P0 PAN detent
- During pointer drag, entering ±7 latches center; stays centered through ±12; beyond ±12 releases with signed pan.
- On pointer release inside ±12 snaps exactly to 0, with display `C`, accessible readback `Center`, and centered indicator.
- Keyboard arrows are fine adjustment and bypass the detent; Alt+pointer explicitly bypasses it. Double-click resets pan to zero.
- Only UI preview state is modified; no fabricated hardware route or native ACK.

## Visual readability and hardware feel
- Subtle machined/graphite panel edges, flat sufficient text contrast, compact FX rows and clipped names.
- Meter and gain readout acquire smoked protective glass and restrained reflections. Interactive layers are `pointer-events:none`; signal/meter semantics remain unchanged.
- Avoid heavy glow, massive typography, blurred paint, or new card layouts.
- Acceptance needs actual Chromium geometry + PAN interaction + screenshots at 1366/1666/1920, and a review of the user-facing `/preview/mixer.html` artifact.

## Verified physical layout nuance
At 1366×768, a 2px difference between the collapsed ROUTE row and growable FX area was due to forcing the FX minimum of 122px while reserving 260px for the fader. Shifting that reservation by 2px to **258px** allows the entire 53px ROUTE collapse to reallocate to the FX rack, with **zero fader movement**. Calibrated locally with Chromium at 1366×768, 1666×888 and 1920×1080 before CI.
