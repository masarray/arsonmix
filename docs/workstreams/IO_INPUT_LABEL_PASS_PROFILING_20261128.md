# I/O presentation-pass input-label reuse — 28 Nov 2026

Stacked on #1006 exact head e66ca97ecc72c1895213d80ce151dda05658da9d, canonical a61749fbcfa3f11a3268528af4c7cf9fd780e740, main 379e867985bb6bab8e07297ba7a8c44925b22b2d. Do not merge independently of #1006 or overwrite peer changes.

The actual native bridge projection and refreshRealM4Inputs called inputLabel independently for every row. Even after #1006, that regenerated all choices each time. One lazy resolver per synchronous pass reuses the exact same choices. No persistent cache, stale device epoch, format change, new source authority, native callback/DSP changes or GUI changes. None/BUS/unpatched-only passes remain scan-free.

Actual production selector/resolver A/B using isolated V8, 24 synthetic stereo rows, five alternating 200-call batches after 50 warmups. Date.now batch median microseconds per 24-row pass; getter-count fixture used in both arms. Timing advisory only, not a native Windows or DOM benchmark.

| Pins | Before us | After us | Active reads/pass before -> after |
|---:|---:|---:|---:|
| 2 | 380 | 25 | 48 -> 2 |
| 8 | 845 | 55 | 192 -> 8 |
| 32 | 2310 | 150 | 768 -> 32 |
| 64 | 4035 | 255 | 1536 -> 64 |

A/B parity in all four measured fixtures. Reproduce with node scripts/profile_input_label_pass.mjs (baseline commit must be present). Additional regression covers reverse/custom/M4/retained/remapped sources, invalid types, no-scan fast paths and exact bridge wiring. Deterministic read counts and parity, not time thresholds, are the tests. Existing canonical cold-start restore and Plugin Manager Chromium failures remain independent. No Windows/FAT/installer dispatch. Physical ASIO and end-to-end GUI timing NOT RUN.
