# AN-PRO-FIELD-02 — operator UX and audio transition audit

Installed baseline da57dd742d8634e82c5f4655d653c7e1a5ba6764; #948 parent 02d38ac56d06e3f38ccfef32015bd39af4855cbe. Owner screenshot shows disabled Silence with visible native slider, profile ready/Accepted but Learn lamp appears active, manual dynamics + auto spectral; owner reports unnatural silence-to-voice transition. No exact PCM/settings dump, so cannot isolate audible symptom to one stage.

## External references (documented behavior, not reverse engineering or listening comparison)
- RX Spectral De-noise: https://downloads.izotope.com/docs/rx6/34-spectral-de-noise/index.html — fixed manually learned profile, separate adaptive mode, threshold, reduction, tonal/random noise and quality/latency tradeoffs.
- RX Voice De-noise: https://downloads.izotope.com/docs/rx6/36-voice-de-noise/index.html — manual noise reference, adaptive threshold, dialogue/music optimization, efficient zero-latency path.
- FabFilter Pro-G: https://prod.fabfilter.com/help/pro-g/using/timecontrols — hysteresis, program-dependent attack/release, hold, knee, optional lookahead/pre-open; zero latency with lookahead and oversampling disabled.
- Steinberg Gate: https://www.steinberg.help/r/cubase-pro/cubaseplugref/16.0/en/_shared/topics/plug_ref/gate_r.html — attack, hold, release, Live vs pre-open.
- Waves NS1: https://www.waves.com/plugins/ns1-noise-suppressor — continuous adaptive suppression distinct from one-shot capture.

## Fixed in this change
P0 knob overlay: generic .an-panel input:disabled opacity .45 overrides transparent hit target. Keep range invisible permanently and steel artwork opaque; mute indicator only. Chromium computed-style regression.
P0 capture UX: idle lamp lit whenever enabled; Accepted screenshot actually indicates capture finished then. White live input continues; orange anchor stays fixed, even if auto working PSD adapts. Idle lamp, Capturing label, aria-busy, fixed/adaptive spectral summary, clear live/reference labels; progress no longer jumps backward on extension. Capture remains 2–4 seconds of active processed audio, not wall time if host stops.
P0 gate discontinuity during Learn: beginLearn resets live gate to unity, gate disabled throughout capture, reference detector resets live detector at finish. Separate raw reference detector and keep manual gate active. Local exact 8/8 mono/stereo rate tests, spectral clean=0, settled gate mute: capture completes, PCM equals no-Learn control. Spectral transition not yet fixed. No extra PDC or state schema.
P1 Ratio and Knee disabled in hard gate; saved values preserved.

## Remaining audit findings
P0 natural onset: linear full-scale attack/release ramps, 8ms peak decay, fixed 3dB hysteresis, target sampled every 16 samples, hard gate step, reset-to-unity on mode/enable changes. Raw detector precedes delayed output 256/512 samples (~5.3ms at 48/96k); 20ms attack can attenuate first syllables; below-threshold consonants/breaths can be lost. Auto spectral/residual increases gain per WOLA frame; two stages may compound modulation. Need aligned dry A/B, weak consonants, sustained vocals, threshold hover, short phrases, near-zero pauses and real vocal listening. Measure onset energy, gain slew and modulation, re-open time, exact settled silence and headroom. No arbitrary threshold or attack changes; no extra latency.
P0 spectral Learn intentionally renders delayed unity even when a trusted profile already exists. Separate reference capture from active processing and crossfade only the qualified reference, preserving trusted profile on failure. Not fixed by gate isolation. Preserve legacy steady-state parity.
P1 callback stop leaves sample-count capture pending; explicit cancel with ACK, stale telemetry UX, no fake wall-clock acceptance. After Learn ACK stale snapshot may temporarily re-enable control; native duplicate guard exists, UI should fence fresh state.
P1 HPF/LPF edit during capture invalidates sidechain capture while spectral capture continues; define explicit policy.
P1 cold reload intentionally drops transient profile; clarify preset semantics. Profile persistence only with explicit source/rate/channel validity. Music bypass ignores saved processing knobs; expose reason. Compact Pro panel discoverability and fixed/adaptive mode clarity. P2 deepest bin cut is not broadband reduction; add real output meter and independent stage GR. P1 Noise Only replaces output including audience send; hard cancel can jump gain; safer monitor path/transition needed.
P0 previous exact candidate headroom job 139886612313 failed, functional GCC/Clang and GUI/corpus passed. Keep gate, compare on same runner, no merge/public release until qualified.

Local focused C++ manual gate regression 8/8 and Node contract PASS, exact candidate CI required. No hardware/installer claim. Remaining audio quality work open.