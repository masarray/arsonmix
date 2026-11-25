# Global integration source ledger — 2026-11-25 (RC scope freeze)
Public baseline: v0.5.0-alpha.32 / main 2253001926e6ef318eb00c9673417dc1177df4d6. Target alpha.33 remains proposed, not published.
Rollback candidate 906b5e8aca4004d947743fdfee9ae8ed69372f38: exact Windows 123/123 CTest, installer 14982402159 (run 45521671701).
Harmonice 72f2f0f3799d8f92c449e7e48882e0b9b649b7b9: 9/9 CI, 72 renders, Windows 113/113 (run 45534987871; installer 14987959149). H30B candidate remains OFF. Exact four-path delta, common base 5ac7d5590e641554d252bb6869a0e00ed1cb82ca.
Anti Noiz dff734d71785666fbec008e27ebc13ef90397f1b: five new source/doc/test blobs, no live registration, CMake standalone test only. Source research retained, active speech/music quality, latency/CPU, MSVC and physical acceptance not claimed.
## All 24 open PRs at snapshot
| PR | Head | Disposition |
|---|---|---|
| #900 | dff734d71785666fbec008e27ebc13ef90397f1b | AN-01 exact source carried, test-only; no native registration, no product readiness |
| #899 | 906b5e8aca4004d947743fdfee9ae8ed69372f38 | previous qualified integration rollback |
| #898 | 68f80ef1ebcae004a503091de76d723d81b25929 | ancestor P12 |
| #897 | 2b4959485910e09351f03d8cc5a1d862f9700b3e | ancestor P10/P11 |
| #896 | 102082ab1ab0aacaa068e96d6fd2f614b8da87af | ancestor integrated I/O |
| #895 | bdb2e23c69b576f2b2db9df811ebab5dcce5500a | ancestor atomic output |
| #894 | 09bc37740b818a3e503612ae8a17919756082b06 | ancestor Reverb-Sis |
| #893 | 4c739bac7769659626fbd5cebb96696547d6015f | ancestor Audio I/O |
| #892 | bec0c0f8d4e2cde5eec66f4ab791bdd81db78047 | ancestor P0 consolidation |
| #891 | cbccdaa10407ed9c2e772f00b1ad832ff095b8fe | diverged final test commit; same ownership-transfer assertions present; remaining shared-file evolution retained |
| #890 | 7fe65abe28b8c874a8d31dfa4c5cde0e68e214e8 | ancestor ImmerSIB FIR |
| #889 | 72f2f0f3799d8f92c449e7e48882e0b9b649b7b9 | latest exact H30B delta carried; candidate OFF; FAT PASS |
| #888 | f55bc7580e534f9bcb6c4799c391711763f2a4ee | ancestor single-ASIO inventory |
| #887 | be4fe3a40dada73e0c92e8ab93c465ca563afe4e | ancestor Vocal Reverb |
| #886 | 72f51779081d5a01f76bd316b5eb56c58f874246 | ancestor INT-1 |
| #885 | 2fa0945b04ec81f59d8ecc1054285d8aa6368dbb | ancestor GEQ/F11 |
| #882 | 160806527b2f7256101efd36d9ef607833171e96 | diverged but exact current-lease locator and MOVE assertions present in integrated source; live field gate |
| #881 | 08787838fbdab61aee2f37b5f5b1202874426a24 | all five changed blobs identical |
| #861 | 840ea73fc4a3a51a84428f22c231610fb9db348c | diverged; direct Copy Audio Report present, actionable capture-pin recovery/banner/report-v3 parity not proven; P0 follow-up |
| #840 | 99909dba1691e761a4457b16e7418879145cb5fe | diverged; 1Hz AUDIO_HEALTH present, exact profiler semantics not fully verified; P0 follow-up |
| #830 | e9d6797e9371836a20b39caebf353c117643d4f4 | deferred typed secondary ASIO B intent; no GUI/persistence/physical qualification; preserve branch |
| #825 | e071e16e7136d58204acf8aaab6b1eb0d54d5d28 | deferred secondary ASIO ASRC/map/reader; no production promotion; preserve branch |
| #731 | 61d64cd76ca56d51a8dcd4d621eea49ac4ca7819 | diverged legacy editor host; successor #891/#892; full field equivalence not proven |
| #708 | a97462c2730f3e0feae1b9e8982c2784e81ea9a6 | diverged factory-reset/recovery; --reset-mixer/nativeMixerFactoryReset not confirmed in current Main.cpp; P0 follow-up, do not close |
## Admission rules
Do not use ancestry alone as functional proof; overlapping paths retain newer integrated behavior unless exact intent regression verifies equivalence. No old branch closure, force push, rebase, main merge, tag, public release, or implicit promotion of unfinished DSP/secondary ASIO. Preserve the three source branches and previous installer. H30B and AN-01 have no overlap with newer global changes (verified complete Git trees); CMake adds only standalone test and test build dependency. New exact-source CI and integrated installer must pass; old artifacts do not qualify new SHA.
P0 unresolved: #861 capture pin recovery/diagnostics, #840 1Hz matched evidence parity, #708 safe reset path, #731/#891/#882 physical editor ownership, #883 Music input routing and #884 wire gain field tests, #547 startup/WaveShell, #842 clean launch, #412 native audio, #837 delivery and #854 ImmerSIB. #825/#830/#812 dual-ASIO is separate unfinished feature, not public capability; keep no hidden route or second render owner. Remaining open research/backlog issues retain issue identity, including #867 #778 #752 #726 #686 #683 #680 #664 #636 #625 #608 #533 #527 #490 #473 #453 #450 #349 #347 #192 #185 #180 #135 #85 #56.
Before public alpha.33: exact integrated Windows compile/CTest/installer/upgrade/uninstall and rollback verification; owner physical ASIO, WIRE/FX, Music/stream, session/restart, singer listening; resolve actual P0 regressions; no unsupported claims about HIGH, denoise or dual ASIO. Release controller owner approval and exact main/tag/asset provenance; no public dispatch in this commit.
