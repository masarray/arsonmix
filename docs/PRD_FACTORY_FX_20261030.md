# PRD — ArSonMix Factory FX Library
Date: 2026-10-30. Product requirement, NOT implementation claim.
Canonical technical rules: [ADR Factory FX](ADR_FACTORY_FX_BUNDLE_20261030.md) and [epic #185](https://github.com/masarray/arsonmix/issues/185). Retain L2 #176 and one installed/portable product feature set #85.

## A usable mixer on first launch
The official completed factory package carries **ArSonEQ**, **ArSonComp**, **ArSonVerb**, **ArSonHarmony**, **ArSonColor** and **ArSonKuPik Enhancer**, as REAL bundled first-party VST3 (not empty cards or fake FX). Open the mixer offline, choose FACTORY in the existing compact FX picker, add effects, hear DSP, tune native plugin/editor parameters, save Show and restore identical sound and slot order. All users may additionally install personal VST3. Factory files live in read-only app-controlled FactoryFX; private Studio/Plugins/VST3 is never part of the official package.

Product categories: EQ (transparent/dynamic equalization), COMP (vocal/music dynamics), REVERB (musical shared vocal space), HARMONY (pitch harmony with explicit latency), COLOR (musical saturation/tone), ENHANCER (ArSonKuPik smart enhancement). Each plugin may expose factory presets and vendor editor only after actual processing/host state acceptance. Use one native VST3 host for FACTORY and USER. No second audio engine, VST2 promise, internet requirement per launch, hidden license bypass or user-exclusive distribution.

## Actual sibling readiness at audit
ASKPVST has private source and a public v0.5.26 Windows VST3 release on vst-enhancer; separate product licence approval needed before factory distribution. ArSonEQ has real C++ DSP/VST3 on a feature branch, but default main is only LICENSE and no public release. ArSonVerb core/VST3 exists but outgoing licence remains undecided. COMP/HARMONY/COLOR have no verified sibling repo or release. None is currently attested as shipped inside ArSonMix. Sibling release work: askp-vst #60, arson-eq #6, arsonverb #4.

## Must-have UX and quality
FACTORY / MY PLUGINS tabs, search, compact category EQ/COMP/REVERB/HARMONY/COLOR/ENHANCER, actual installed/validated status and clear user-facing errors if not licensed, missing or wrong audio layout. No placeholder button that appears to process audio. Native scene/Show uses logical stable Factory ID and vendor binary state, not fragile absolute paths. No startup full system scan; registry loads only approved bundled modules and uses safe cached class identity. User-owned VST vault remains unrestricted. All plugins must be audible in real native CH/AUX/BUS/MAIN paths only where engine supports them, with mono/stereo layouts, bypass, plugin state, PDC/headroom and a safe retired-plugin lifecycle. Never resize the L2 eleven faders independently or add a new top toolbar.

## Delivery sequencing
FXF1 = qualified existing original EQ/Enhancer binaries, manifest/cache/picker, installed/portable release and live native roundtrip.
FXF2 = genuine Reverb/Comp/Harmony/Color VST3 packages and presets, full six-effect bundle.
FXF3 = atomic updates, Show migration, licensing robustness, measured low-spec GUI and long hardware soak.
The source code milestones MS1/G5A are merged but **not proof that Factory FX is packed**. Actual release proof requires exact source artifact SHA256, whole VST3 resources, host tests, legal approval, Windows ZIP and real hardware FAT. #15 crackle remains independent release blocker.
