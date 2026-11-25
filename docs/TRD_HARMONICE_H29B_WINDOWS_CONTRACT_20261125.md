# H29B — repair exact CI contracts and Windows FAT blocker
H29A 75a784f: scoped DSP qualification PASS (artifact 14977095162), but universal UI/native quick lanes failed the same stale literal Harmony paths policy at tests/ci_workflow_efficiency_test.mjs:330. Windows FAT H28D 45511636764 compiled; 112/113 CTests; test #50 failed at live_channel_fx_graph_test.cpp:1970. No package uploaded.

## Root cause and correction
The H25B zero-delay FxInstance fixture directly calls GainFx::prepareToPlay without JUCE rate metadata. FxInstance prepares the bypass envelope from getSampleRate(); absent metadata returns 0 and clamps to 8k, 64 samples, while reference uses 48k, 384 samples. ChannelHarmonyProcessor already sets rate/buffer metadata. Fix the mock processor preparation, not DSP, tolerance, fade semantics or PDC. Expand to 32/44.1/48/96/192k, variable blocks, interrupted fades, separate insert/rack request and OR, nonconstant source, dry/wet endpoints, quarantine and unchanged no-allocation/sentinel guarantees. Existing latency-bearing tests unchanged.
Update the exact Harmony-only path contract for the three dedicated H29A inputs; unrelated CI/player/mixer remains excluded. This is not a global workflow relaxation. No Reverb-Sis source changes.
JUCE reference: https://docs.juce.com/master/classjuce_1_1AudioProcessor.html#setRateAndBufferSizeDetails (see setRateAndBufferSizeDetails documentation and prepareToPlay).
No new runtime processing, state, audio buffers or latency. New Windows and exact-head quick checks required; no claim of human listening or ASIO proof.
