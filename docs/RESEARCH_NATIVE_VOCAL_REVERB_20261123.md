# ArSonMix Native Vocal Reverb — independent DSP and psychoacoustic research
Date: 2026-11-23 (Asia/Jakarta). Status: research/design with scoped native candidate, not physical listening approval.
Cross-thread coordination: [INT-1 #837](https://github.com/masarray/arsonmix/issues/837). Single approved /preview/, original PCM graph and primary ASIO render authority remain unchanged.

## User need and the actual meaning of "krenyess"
Singers want an uplifting, *crisp/sweet/airy* bright vocal tail that feels expensive, not a tinny hard high-shelf or more overall wet loudness. Dry intelligibility, mono-safe spatial width, attractive early bloom, density without metallic chirps, and a stable real-time monitor matter more than maximal reverb time. Claiming Alesis/FL/Valhalla sonic parity requires real equal-level blind listening; no static tests establish that.

## External authoritative references
* [Alesis MicroVerb 4 official product](https://www.alesis.com/products/view/microverb4.html): 200 programs including plate/hall/combined effects, unusually simple preset workflow. Its published program chart includes #23 Bright Dense Vocal Plate, #26 Long Smooth Vocal Plate, and #09 Bright Vocal Hall. These are descriptions to study, not licensed impulse responses or transferable proprietary algorithms.
* [FL Studio Fruity Reeverb 2 official manual](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/Fruity%20Reeverb%202.htm): predelay, diffusion, modulation, wet low/high cut, HF damping, early reflection level, stereo separation and send/insert wet/dry behavior; overuse muddies vocals.
* [FL Studio LuxeVerb official manual](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/LuxeVerb.htm): separate decay/size/diffusion, brightness, modulation, flexible stereo width and input-envelope-based wet ducking. Its feedback pitch-shift and freeze are optional creative effects, not required for a default *live vocal*.
* [FabFilter Pro-R 2 manual](https://www.fabfilter.com/help/pro-r): independently adjustable post/decay tone and ducking make density coexist with presence. We do not copy DSP, skin or protected preset data.
* [ArSonVerb v1.0.1 released DSP](https://github.com/masarray/arsonverb/releases/tag/v1.0.1) source 4c65f369348790bb6da7465deaffc2b3921fdeee: eight-delay FDN, orthogonal algorithm matrices, per-line HF damping, modulated delay taps, early reflection and diffuser bank, ducking, safe smooth equal-power mix, 13 stable parameters, four algorithms and 32 original presets. The P5/P7/P8 source-level audits document fixed old 50% dry loudness dip, wet-top damping and startup parameter-preparation spikes; this pinned current engine includes those repairs.

## Architecture decision
Make **one ArSonMix native FX insert** by compiling only the independent AGPL-3.0 ArSonVerb C++ core into existing JUCE 8 mixer. This is NOT another JUCE 9 VST plugin runtime, second callback, duplicated WebAudio graph or automatic FactoryFX VST3 injection. Keep previously shipped ArSonVerb VST3 as a separately named vendor item when present; never confuse their catalog identities. Original upstream core is kept byte-for-byte and pinned with attribution. Native wrapper owns its own settings, message-thread preset/editor, audio-thread core, SPSC edit ACK and robust .smix state. Preserve device/readback/graph identity, on-air bypass and Power behavior. Allocate DSP delay memory in prepareToPlay only, not in processBlock.

## Candidate vocal presets (original ArSonVerb values plus restricted original tuning)
- Krenyess Vocal: Plate, bright but controlled 10-14 kHz tail, predelay roughly 45-60 ms, RT60 around 2 s, softened early energy, presence-preserving duck
- Sweet Karaoke: Plate, more early cohesion, shorter tail, restrained HF
- Crisp Pop Plate: dense, low-cut, articulate, stereo-wide but correlation-tested
- Silky Ballad: long, less early attack, gentle modulation
- Tight Live Chamber: minimal smear for fast phrasing
- Air Vocal Hall: open side-field, rich/dense late bloom
- Warm Vintage Plate: lower high cut for sibilant material
- Large Stage Hall: deliberately longer tail with more ducking

No Alesis factory waveform/impulse/preset code or FL Studio plugin is copied. Values are starting points, not endorsement of any voice/PA microphone.

## Acceptance gates BEFORE sonic marketing or installer inclusion
1. C++/JUCE compile Windows MSVC and Linux; unified registered native effect ID, actual stereo PCM output, and accepted Show/.smix state with negative corrupted-data recovery. No VST3 dependency.
2. Deterministic impulse responses: late echo density, decay-to -60 dB by band and sensible damping/air trend. Silence input must not generate a drone; output finite under 30s at 44.1/48/96 kHz.
3. Buffer partition invariance (64/128/256), 8 insert worst-case p95/p99 real CPU and NO heap/locks in the audio callback; 32/64-frame ASIO start, stop and restart test with real hardware.
4. Compare insert Wet/Dry Mix=.0/.25/.5/1.0, no transient attenuation bug; equal-power dry at 50%; send use 100% wet ONLY when external dry signal is separately routed. Avoid internal + external duplicate dry summing.
5. At 80-200Hz: no low-frequency mush; 3-5k: consonants retain focus; 8-14k: audible 'air' without harsh sibilance. Compare matched LUFS and peak with user-supplied dry vocal against Alesis bright vocal plate, FL Reeverb2/LuxeVerb, and clean baseline; score preference blind. Not achieved by CI.
6. Stereo + mono fold-down (correlation, no cancellation), multi-instance bypass, FX rack MOVE/DELETE/reopen, .smix save/open, mute, channel routing, native GUI/ACK/readback and physical ASIO FAT. Keep INT-1 installer shipping independent if this feature needs more qualification.

## Scope limits
This native insert is a live vocal effect, not a clone of Alesis or FL Studio. Research does not license use of their DSP, presets, media, logos or plugin code. Freeze/shimmer/pitch-feedback and parametric decay-rate EQ are NOT in initial release scope. Analog-style characteristics are created by our own algorithmic parameters. Owner audition remains the ultimate 'krenyess' sonic gate.
