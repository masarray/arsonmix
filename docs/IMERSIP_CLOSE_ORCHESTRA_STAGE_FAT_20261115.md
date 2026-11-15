# ImerSip Close Orchestra Stage — FAT candidate, 15 Nov 2026

## Target from the owner's physical listening
Source instruments near, life-size orchestra directly in front, wide external separation, detailed voice, and living bass supported by the room rather than a thick late-reverb wash. SonKu P8.4 is the owner-preferred audible A/B reference; preserve its P8.2 transparent bass and bounded P8.4 HRIR fusion. No orbit.

## Native implementation
- All venues: **1.6m fixed source** in the genuine P8.2 six-wall acoustic geometry. Concert/Cinema use large venue dimensions; Live Room uses 14.4m. A 1.6m source distance does not imply a 1.6m room.
- Concert EARLY amount = 0.075 + 0.180×Venue Space; LATE amount = 0.025 + 0.100×Venue Space, zero wet at Immersion=0. Cinema/Live similarly bias EARLY above LATE. Larger early/direct cue can add external room impression while preserving articulate foreground instruments. Exact values are LISTENING CANDIDATES, not measured source-room impulse responses.
- Original LEFT/RIGHT source music remains direct, original MID vocal remains dry before optional air detail and coherent low-shelf; source SIDE preserved; the measured P8.4-12% HP750 fused direction contributes only a controlled SIDE return. Standard SonKu unaffected.
- BASS IMPACT adds a small positive *coherent* LP120 shelf with equal L/R coefficient and an extra (1+0.45×Space) multiplier. This mimics near-hall LF pressure while avoiding delayed bass recombination and low-frequency phase cancellations; it is NOT genuine measured room-mode modelling or a substitute for headphones-specific correction.
- Native JUCE editor displays SOURCE 1.6m CLOSE/DIRECT, same 6 controls and native ACK. No duplicate DSP engine or M4 routing; changes don't touch production main.

## Red Windows issue audited
Old run https://github.com/masarray/arsonmix/actions/runs/43659352042: GUI EXE, packaging, all seven JUCE/PCM tests actually PASS. It failed **after** packaging from duplicated `physicalHeadphoneQualified` in PowerShell hashtable, confirmed in the original job log. Commit `91fdc75` removed duplicate and added fast preflight gate. Do not call the CI green until full exact-SHA run including ZIP verification succeeds.

## Gates
1. 44.1/48k actual P8.2 native PCM test checks 1.6m invariant across 3 venues x spaces 0/.45/1, early > late, bass monotone & original bass preserved, original MID, 175/180 wide stereo, anti-phase content, P8.4 fusion/DC, native 64/128/8192 chunk parity.
2. Native Windows 7 CTests including real JUCE ImerSip editor, state v1/v2 restoration, SPSC accepted ACK; packaging manifest uniqueness and PE/ZIP hash checks. No public release unless green.
3. Physical A/B against owner-approved SonKu P8.4, same level with dearVR / SonKu inserts disabled when listening to ImerSip. Evaluate vocal 1–4k clarity, violin and percussion location, bass kick/body, frontal outside-head vs in-head, early vs late balance, ASIO buffer 64/128/256 and actual XRUN. Never assert better sound from source CI alone.

Sources: 2026 frontal externalization work https://pmc.ncbi.nlm.nih.gov/articles/PMC13451706/ ; auditory localization review https://doi.org/10.3389/fpsyg.2024.1408073 . The optimal ratio is listener/content-dependent.
