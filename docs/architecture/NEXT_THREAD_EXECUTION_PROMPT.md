# Ready-to-copy prompt — ArSonMix Maintainability Execution M1

**Use this as the first user message in a NEW ChatGPT thread with the connected GitHub app.** The body is written in Indonesian; repository blueprint and code remain in the project language.

---

[$github](app://connector_76869538009648d5b282a4bb21c3d157)

Lanjutkan langsung pekerjaan IMPLEMENTASI maintainability dan extensibility pada repo https://github.com/masarray/arsonmix. Saya meminta HASIL CODING NYATA dan PR yang bisa diverifikasi, bukan sekadar audit, strategi baru, atau diskusi konsep. Tujuan akhir: software mixer + VST3 host top global pro yang makin mudah dikembangkan, makin ringan di-maintain, reliabel, modular, dan tidak tambah ruwet ketika fitur baru bertambah.

WAJIB BACA terlebih dahulu dari main TERBARU:
1. AGENTS.md dan docs/SMART_CI_RUNNER_WORKFLOW.md (otoritas workflow/keamanan).
2. docs/architecture/README.md
3. docs/architecture/MAINTAINABILITY_EXECUTION_V1_20261111.md
4. docs/architecture/FEATURE_EXTENSION_PLAYBOOK.md
5. Issue #527 dan PR aktif yang menyentuh modul target; terutama draft #512 dan #521 jika masih aktif. Main/head SHA historis BUKAN otoritas, fetch ulang GitHub sekarang.

**MISINYA: EKSEKUSI M1 — satu native projection contract + logical editor/control boundary** sesuai blueprint, mulai coding langsung tanpa meminta approval rencana:
- Buktikan semua pemakai dua ApprovedNativeV8Projection.h yang kini ada di src/ dan integration/. Satukan implementasi validator/projection ke satu sumber kanonis dengan mode INSTALLED vs RETAINED yang eksplisit; semua API publik pengguna lama tetap kompatibel hanya lewat forwarder tipis jika dibutuhkan. Tolak snapshot stale/bus invalid tanpa mutasi; LOGICAL_ONLY/RETAINED TIDAK BOLEH dianggap memiliki callback PCM atau output live.
- Ekstrak unit kontrol logical mixer di src/Main.cpp (handleLogicalMixerIntent, emitLogicalMixerAck, saveLogicalWorkspace, fungsi terkait) ke service yang BENAR-BENAR memindahkan ownership validasi/transaction/ACK/save, menggunakan NativeLogicalMixerWorkspace, MixerSessionDocument dan SessionStore yang sudah ada. Main tinggal application composition / bridge forwarding. Jangan membuat second document, proxy class kosong, atau callback/publisher baru.
- Berikan jalur extension standar untuk logical FX supaya penambahan FX baru tidak perlu menyebar switch/if produk ke banyak bagian Main, engine, dan GUI. Namun jangan menulis ulang DSP Harmony/Compressor/Vocal Throw atau mengubah bunyi, latency, preset, VST lease dan editor. Jika ekstraksi FX khusus belum aman, buat kontrak seam minimal yang diuji, jangan berbohong sudah sepenuhnya selesai.
- Uji rangkaian end-to-end: no-device logical ADD/RENAME/REMOVE channel/BUS/WIRE/FX, .smix Save/Open, duplicate/stale/busy/write-failure rollback, FX state, ACK lalu readback, native installed/retained projection, dan compatibility terhadap current main. Tambah behavior tests, jangan sekadar string-search asserts. Perbarui tes lama yang mengunci lokasi source hanya jika invariant yang sama tetap teruji.
- Jangan memperbarui GUI owner-approved /preview/ secara visual. WIRE/FX/routing/ASIO/VST asli dan session tidak boleh regresi. Tetap SATU native JUCE realtime callback, SATU accepted ChannelId PCM graph, SATU source of truth. Tidak ada WebAudio, mock meter, second renderer, hidden monitor/ARM wizard, device auto-switch yang tidak sah, allocation/lock/disk/VST teardown pada audio callback.

**Metode kerja cepat dan aman:**
1. Fetch main saat ini, existing open PR dan HEAD, periksa diff yang relevan; jangan overwrite pekerjaan thread/PR #512 atau eksperimen #521. Buat branch M1 berdasarkan main terbaru bila belum ada branch in-scope.
2. Kerjakan satu milestone lengkap yang bermanfaat dengan implementasi + unit/PCM tests + native host seam + ACK/readback, dalam satu PR koheren. Jangan bikin PR untuk research atau satu test kecil; jangan hanya membuat dokumentasi lagi. Kalau ada konflik terhadap PR aktif, pilih perubahan yang aman berdasarkan actual diff, selesaikan milestone yang utuh dan jelaskan dependency spesifik.
3. Perubahan behavior harus dibuktikan, bukan diasumsikan. Tes cepat terkait setiap push; lanjut coding mandiri ketika CI quick sedang berjalan. Pada kandidat exact SHA, jalankan hanya UI dan/atau native/FAT sesuai docs/SMART_CI_RUNNER_WORKFLOW.md. FAT SUDAH mencakup native, jangan menjalankan native + FAT duplikat. Agent memakai komentar PR /arsonmix-ci yang sah, bukan meminta saya klik Actions. Hardware physical FAT tetap PENDING bila tidak ada bukti nyata.
4. Review diff kualitas: file responsibility berkurang, bukan sekadar LOC pindah; tidak muncul facade kosong, duplicate source of truth, broad catch swallow, callback branch atau state model baru; API dan CMake/test target tetap build.
5. Setelah verified dan green sesuai branch protection, rekonsiliasi dengan current main dan merge jika aman; bila blokir nyata, jelaskan tepat dan tinggalkan PR yang masih bisa langsung diteruskan. Jangan menunda dengan "akan dikerjakan nanti", jangan berhenti di rencana.

**OUTPUT WAJIB:**
- Link PR, branch, main SHA dan PR HEAD SHA terakhir, file yang benar-benar diubah, fungsi/ownership yang berhasil disederhanakan, duplikasi yang dihapus.
- Test yang benar-benar dijalankan dengan PASS/FAIL dan URL; detail area yang belum teruji. Jangan mengklaim hardware FAT.
- Bukti bahwa penambahan fitur FX/channel berikutnya menjadi lebih sederhana (seam dan test yang digunakan), berikut sisa pekerjaan M2 menurut blueprint; update issue #527 / docs/architecture jika source ownership berubah.
- Target akhir bukan berkurangnya baris semata, tetapi pengembangan fitur ke depan menjadi jauh lebih praktis, stabil, dan tidak perlu audit besar berulang.

Saya menginginkan hasil milestone M1 yang benar-benar selesai secara coding, bukan analisis tambahan. Mulai eksekusi langsung melalui GitHub connector.

---

For agent consumption: the prompt above is an implementation request, not evidence of code being shipped. Avoid treating older exact SHAs as current. The binding native GUI and release/physical FAT rules in AGENTS.md prevail.
