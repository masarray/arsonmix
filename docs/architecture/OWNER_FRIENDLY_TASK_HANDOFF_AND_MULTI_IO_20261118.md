# ArSonMix — Owner-Friendly Task Handoff & Multi-Device I/O Continuity

**Status:** CURRENT PROPOSED EXECUTION GUIDANCE (effective for agents only when merged into main). 2026-11-18.  
**Authority:** [AGENTS.md](../../AGENTS.md), [canonical modular MVVM ADR](ADR_MODULAR_MVVM_FEASIBILITY_20261118.md), [full multi-device PRD](../PRD_ARSONMIX_MULTI_DEVICE_IO_20261118.md), [multi-device ARD](../ARD_ARSONMIX_MULTI_DEVICE_IO_V2_20261118.md).  
**Parallel work:** [nonblocking guide](PARALLEL_THREAD_CONVERGENCE_20261118.md). **No file/domain locks, user permissions to start coding, or new CI coordination gates.**

## Why this exists

The operator/maintainer does not need to know CMake, Git internals, ASIO SDK or PCM graph details to decide whether a completed task is safe to use. Previous threads confused *written*, *tested*, *merged*, *installed* and *audible*. This has led to accidental testing of installers missing ImmerSIB and integration uncertainty.

**Rule: Every task-holding ChatGPT thread/agent MUST finish its current response with a clear Indonesian operator-facing closeout, including incomplete or failed tasks.** The same compact outcome should be in the relevant PR body or a PR Conversation comment after a substantial code milestone **when GitHub write access is available**. This is a **reporting duty, not an approval or admission gate**; an agent can code without opening a PR comment first. A ChatGPT thread cannot post into other independent chats or automatically push real-time notifications to the operator. The agent doing the task must report in its own reply; the PR is the durable cross-thread reference.

## Exact owner-facing completion report

Use normal Indonesian; no tables or long technical dumps required. **Start with an unambiguous product verdict**, not how many commits were pushed. Answer all six items:

1. **HASIL:** What changed for the operator, in plain terms? What did **not** change?
2. **STATUS:** Say precisely one most advanced state: `DESAIN SAJA`, `KODE DIBUAT (BELUM DIUJI)`, `KODE DIUJI`, `SUDAH MERGED KE MAIN`, `INSTALLER TERSEDIA`, `SUDAH DIUJI DI HARDWARE`, or `SUDAH DIRILIS`. A stage never implies the next. For multiple feature components, state their different stages.
3. **AMAN DIPAKAI?:** **YA / BELUM / TIDAK**, naming the exact build or feature and why. Never claim a feature exists in installed binary based on docs, green CTest, or PR title.
4. **TINDAKAN ANDA:** `TIDAK PERLU MELAKUKAN APA-APA`, or **one explicit and safe next action** with exact PR/build URL and brief steps. If a physical audio test is needed, state what interface/cables/outputs require operator approval; no surprise routing, audio output arm or driver installation. Do not ask the operator to operate Actions for routine CI when connected GitHub command dispatch is available.
5. **BUKTI & LOKASI:** GitHub PR/issue, exact HEAD / merged main SHA, tested run/artifact link, PASS/FAIL/PENDING/NOT RUN with meaningful scope. Distinguish UI, native PCM, Windows installer, real vendor dual-ASIO and acoustic listening tests.
6. **LANGKAH THREAD BERIKUTNYA:** The next useful milestone and what source/contract to reuse, with explicit risks/dependencies. This is **handoff information**, never a requirement to wait for another thread's approval.

**Readable default example (design-only multi-I/O):**

> **Hasil:** Rancangan dua audio interface sebagai input ke channel mixer sudah terdokumentasi; aplikasi yang terpasang **belum** memiliki kemampuan tersebut.  
> **Status:** DESAIN SAJA. **Aman dipakai?** Belum ada build untuk diuji.  
> **Tindakan Anda:** TIDAK PERLU MELAKUKAN APA-APA. Jangan mengunduh installer lama untuk mengetes dual-ASIO.  
> **Bukti:** PR #..., SHA ..., source reviewed; Windows/hardware FAT NOT RUN.  
> **Berikutnya:** Implementasi capture-only adapter dan uji ASIO A+B tanpa mengubah render primary.

**Readable example (code tested, not installed):**

> **Hasil:** Secondary capture berhasil diproses oleh PCM fixture; UI dan installer publik belum berubah.  
> **Status:** KODE DIUJI, BELUM MERGED. **Aman dipakai?** Belum untuk pengguna Windows.  
> **Tindakan Anda:** TIDAK PERLU MELAKUKAN APA-APA. Jangan mengganti soundcard utama.  
> **Bukti:** PR #..., SHA ..., native fixture PASS; dual-vendor physical FAT PENDING.  
> **Berikutnya:** Rekonsiliasi source lalu exact-head Windows qualification.

**Readable example (real field candidate):**

> **Hasil:** Installer kandidat sudah dibuat dan katalog built-in FX diperiksa; support dua perangkat belum dibuktikan.  
> **Status:** INSTALLER TERSEDIA, BELUM DIUJI DI HARDWARE. **Aman dipakai?** Hanya untuk uji terbatas, bukan pemakaian live.  
> **Tindakan Anda:** [If specifically authorized] unduh artifact SHA ..., simpan backup .smix, uji dengan output monitoring dimatikan, laporkan driver A/B + buffer + hasil.  
> **Bukti:** Exact-SHA FAT PASS; actual ASIO-vendor test PENDING.

## Product progress and GitHub: no new shared mutex ledger

- **main**: source that has actually merged, not proof the customer-installed EXE has updated.
- **One feature PR**: current code delta, exact branch HEAD, concise test evidence, limited follow-up. A draft PR is simply unfinished work, **never a lock on a file**.
- **One issue/epic**: broader acceptance and links to related PRs; do not create a universal mutable progress file that every thread must edit. Use existing [#527](https://github.com/masarray/arsonmix/issues/527) for architecture, feature-specific PRD/ARD for multi-device, and original product PRs for implementation.
- **Any peer thread**: discovers recent source by reading live main, relevant open PRs, and the last significant completion comment. If a later PR merged, the new main always wins on the implementation details; preserve accepted operator baselines and forward-port only proven unique work.
- **Failure or GitHub outage**: clearly say what is not verified or not written. Never claim a completed PR, CI run, artifact or message that does not exist. Missing metadata does not prohibit developing independently.
- **Update means evidence, not permission**: the six-field report must not become a required GitHub status check, owner approval token, workstream reservation, CODEOWNERS gate, or a reason other agents cannot proceed.

## Multi-Device I/O implementation contracts for modular extension

**Single accepted render owner, many possible I/O-only callbacks.** Multiple capture-only producers and output-only sinks can have their own physically clocked callbacks, but **never a second mixer/DSP/VST3 processing graph**. Preserve primary ASIO direct path with no additional FIFO/ASRC. Keep WebView2 approved `/preview/` and the existing v8 typed requestId/deviceEpoch/graphRevision/ChannelId/EdgeId contract, no new JS mixer owner.

Existing capability is **one chosen primary interface** with same-device channels and the existing optional independent WASAPI MAIN-copy secondary output. The **new** multiple independently clocked input interfaces, mapped per channel, and selected independent output BUS taps remain **proposed**, not shipped or proven.

| Deliverable | Target capability, scope | Test / user report required |
| --- | --- | --- |
| IO-0: inventory & compatibility | Verify actual device identity, JUCE/ASIO simultaneous vendor use, supported input-only mode and resource conflicts; build a minimal feasibility fixture | Enumerate *actual* A+B device/driver/sample rate/buffer and whether hardware tested; cannot promise 2×ASIO from registry slots |
| IO-1: secondary **capture-only** | Independent ASIO/WASAPI capture callback -> preallocated per-device SPSC + measured drift correction -> **one primary** render graph | Native synthetic rate mismatch, underrun, hotplug, no primary regression; actual vendor FAT pending until observed |
| IO-2: native source binding | Stable DeviceKey + physical pin -> saved channel binding with epoch/revision ACK and rollback | Cold start, offline .smix, device loss/reconnect, no phantom pins, no accidental MAIN |
| IO-3: optional independent output sink | Fully processed selected BUS/MAIN tap -> queue/ASRC -> output-only callback; no new FX renderer | Different-rate sink loss doesn't block primary; no duplicate signal or silent speaker re-arm |
| IO-4: operator UI | Device Source/Output selection in existing approved mixer; pure Device Setup ViewModel, one native accepted readback | User can identify which interface/channel is selected; stale/rejected device request safe; approved visuals/gestures retained |
| IO-5: exact installer + field proof | Compiled feature profile and FX catalog/scene assets, plugin identity, real two-device Windows field trials | FAT Windows installer + real vendor A+B input routing + latency/xrun/drift + primary ASIO unchanged, per-device failure isolation |

These rows are **functional acceptance slices, not a global waterfall lock**. IO-1/IO-2/IO-4 can be researched independently where interfaces are stable. IO-3 hardware research can proceed alongside capture work; integration of shared device state must reconcile source and tests at merge. Distinguish primary multi-output same-device taps from novel cross-clock output hardware.

**Measurable safety targets:** primary ASIO audio monitoring stays independent of secondary queues; ASIO output remains explicitly authorized; callback no heap/mutex/driver-open/DOM/VST3 lifecycle; if secondary disappears, only bound channels/sink degrade while session remains; no unintended extra render-cycle buffer. Record `NOT MEASURED` for unobserved actual latency. No arbitrary number of milliseconds promised.

## Next implementation acceptance

The first code PR following this guidance should demonstrate **one complete native-to-UI vertical path**, not an architectural wrapper forest:
- reuse current `NativeDeviceApplyCoordinator`, `LiveAudioEngine`, `DynamicNativeSessionPublisher` and native session control;
- extract one bounded device presentation/ViewModel behavior, remove its old duplicate transitions, then add correlated ACK/rejected/snapshot tests; or independently prove a capture-only adapter against synthetic buffer/drift fixtures;
- include plain Indonesian task closeout in response and durable PR, naming exact tested behavior and what *still cannot be used*;
- no retuning of accepted ImmerSIB/SonKu audio, no rewrite of approved mixer GUI and no installer publication before explicit product parity.

**Definition of done for the user's goal:** a nontechnical operator can read each report, know whether it is ready to test, and know exactly the next action without searching other chats; other threads can continue independently without approval locks.
