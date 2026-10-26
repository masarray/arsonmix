# ARD — ArSonMix Pluggable Theme Engine v1.0

**Status:** PROPOSED / DESIGN ONLY · **Tanggal:** 2026-10-26  
**Source of truth:** `main` pada `7401573252f2e19f2a74ad59973f666a85fd6924` (ambil ulang `main` saat implementasi).  
**Pemilik keputusan:** GUI Core owns layout; Theme Engine owns visual skin; audio/native owns DSP.  
**Default wajib:** GUI yang saat ini di-approve tetap identik sampai user memilih theme alternatif. ARD ini **belum** mengaktifkan pilihan theme ataupun mengubah UI.

## 1. Keputusan arsitektur

**Theme ≠ Layout.** Theme mengubah tampilan material (brushed metal, dark crystal), surface, label/ink, state LED, shadows, bevel, gloss, warna, ikon yang dekoratif, serta *asset visual* fader/PAN yang memiliki **bounding box dan koordinat hotspot identik**. Theme **tidak** menentukan grid, flex, ukuran strip, urutan slot FX, spacing, alignment, panjang fader, DOM, handler, backend message, parameter audio, hitbox maupun interaksi drag.

Dua profil awal:
- `studio-default` — visual canonical GitHub dari checkpoint GUI terbaru, fallback deterministik.
- `brushed-metal` — stainless/brushed metal chassis, silver tactile buttons dengan state glow; **FX insert dan FX power tetap premium black-blue glossy glass** (bukan aluminium). Fader asli default sampai alternate skin PNG diuji parity; PAN pusat 0 dan center fill dipertahankan.

User memilih **Appearance** (bukan mengganti layout), switch live tanpa reload, render ulang mixer, audio restart, atau modifikasi state mute/pan/FX. Tema selalu dikembangkan *bersama* GUI utama, bukan alternatif file HTML standalone.

## 2. Kontrak kepemilikan (property ownership)

| Layer owner | Boleh mengatur | **DILARANG di Theme** |
| --- | --- | --- |
| GUI layout / `10-console-legacy.css` yang diekstrak bertahap | `display`, `grid`, `flex`, `gap`, `width/height/min/max`, `margin`, `padding`, `position/inset/top/left`, `overflow`, `z-index`, responsive breakpoints, box sizing, hitbox | Theme tidak menimpa |
| Component renderer / control mechanics | CSS rendering dari token, focus ring, active/disabled state, center snap, gesture, fader/PAN value-to-pixel mapping | Theme tidak mengubah behavior maupun positions |
| Theme semantic tokens | Warna, background/image *non-layout*, brushed texture, box/text shadows, borders **tanpa mengubah geometry**, foreground, icon color, state palette, asset URL yang sudah disetujui | Width/height/spacing, transform/motion, `content`, `display`, `pointer-events`, global `button` selector |
| Theme manager | `data-armix-theme` pada root, validasi ID, preferensi, fallback | Tidak menulis `innerHTML` pada strip, tidak mengirim command DSP/audio |
| Native WebView2 integration | Persist preferensi UI bila perlu, origin stabil, early theme bootstrap | Tidak menduplikasi nilai gain/channel/preset di storage tema |

**Layout adalah satu sumber kebenaran.** Jika di kemudian hari ingin pilihan compact/wide/mixing layout, itu menjadi sistem `LayoutProfile` terpisah dengan kontrak serta uji hitbox sendiri — **bukan** tambahan token theme.

**Atribut semantic role:** komponen GUI memiliki role yang stabil untuk pemetaan material (misalnya `data-ui-part="fx-insert"` hanya bila memang diperlukan). Theme pack sendiri tidak memakai selector `#masterLeft`, `.rack:nth-child(...)`, nomor channel, ataupun absolute positioning. Komponen boleh berganti struktur DOM selama ekspor role/token dipertahankan.

## 3. Arsitektur target

```text
                   GUI Core (layout + interaction; single canonical DOM)
                     |                  |                    |
                Control skin       FX material          Fader/PAN skin
                     |                  |                    |
               consumes semantic render tokens / approved image slots
                                      ^
                            Theme Token Registry
                         studio-default | brushed-metal
                                      ^
                          ThemeManager (validated ID)
                                      ^
            Appearance chooser —> storage adapter / native optional bridge

              audio state / native backend / DSP = separate, untouched
```

Theme manager hanya mengganti satu atribut pada root:
```js
document.documentElement.dataset.armixTheme = registry.resolve(preference).id;
```
Hal ini **bukan** jaminan layout/paint O(1): browser tetap melakukan style recalc dan repaint. Tetapi tidak ada rebuild DOM, tidak ada audio call, dan asset style dapat di-cache. Set theme awal **sebelum first paint** untuk mencegah flash. Saat tab Chrome biasa, bootstrap membaca preferensi valid sebelum UI tampil; pada Windows native tentukan storage dan bootstrap lewat jalur WebView2 resmi.

## 4. Semantic token contract v1

Gunakan penamaan component-semantic; tokens harus stabil lintas DOM. Contoh kontrak (nilai di bawah ilustrasi, **bukan CSS final**):

```css
/* Token fallback pada component ownership; default harus pixel-parity */
:root {
  --am-surface-chassis: linear-gradient(#29343f,#121b24);
  --am-surface-strip: linear-gradient(90deg,#1b2933,#222d35,#111920);
  --am-surface-control: linear-gradient(#283641,#1e2a33);
  --am-surface-lcd: #071720;
  --am-surface-fx: linear-gradient(#172b38,#081319);
  --am-surface-fx-power: linear-gradient(#223b49,#09171f);
  --am-text-primary: #d6e6ee;
  --am-text-muted: #94b1c0;
  --am-meter-sim-text: #e6d68d;
  --am-button-face-off: #202832;
  --am-button-ink-off: #f2f8fb;
  --am-button-border-color: #070c13;
  --am-button-shadow-off: inset 0 1px #ffffff62, 0 2px 2px #0009;
  --am-button-shadow-on: inset 0 2px 3px #0008, 0 0 3px #18cafa66;
  --am-state-mute: #e76f83;
  --am-state-solo: #dbb75c;
  --am-state-hpf: #37c99a;
  --am-state-phase: #a68cdb;
  --am-state-selected: #22bbcc;
  --am-pan-track-fill: #4fd0e9;
  --am-focus-ring: #9ceaff;
}
/* Only token assignments in a theme pack — no raw button selectors. */
:root[data-armix-theme="brushed-metal"] {
  --am-surface-chassis: linear-gradient(180deg,#dde4e7,#bac4ca);
  --am-surface-strip: linear-gradient(90deg,#b7c2c9,#e2e6e9,#b3bdc4);
  --am-surface-control: linear-gradient(180deg,#c8d2d7,#a5b2b9);
  --am-button-face-off: linear-gradient(180deg,#cfd4d6,#f0f1f2 47%,#b8c0c4 55%,#c9d0d3);
  --am-button-ink-off: #34424a;
  --am-surface-fx: linear-gradient(180deg,#192b37,#081319);
}
/* Existing control owns its geometry, hover/click behavior and state precedence. */
.btn-180 { background: var(--am-button-face-off); }
```

### Token taxonomy
1. **Foundation/reference:** underlying raw palette/texture values (not directly consumed by layout).
2. **Semantic surface:** `chassis`, `strip`, `control-bay`, `lcd`, `fx-glass`, `meter`, `modal`, `left-rail`.
3. **Control rendering:** `button-face`, `button-depth`, `button-text`, `pan-groove`, `pan-fill`, `fader-cap-visual`, `selection-halo`.
4. **Semantic state:** `mute` red, `solo` amber, `hpf` green, `phase` distinct, `selected` blue, `disabled`, `focus`, `clip`; labels and accessibility state never encoded by color alone.
5. **Asset slots:** `fader-cap`, `master-cap`, `pan-thumb` refer to approved dimension-matched artwork; fallback to canonical PNG if not packaged.

Design token source may use DTCG-style typed JSON later, but the runtime should consume **precompiled, static CSS** to avoid new JSON interpreter on each switch. Manifest fields `id`, `displayName`, `version`, `tokenSchemaVersion`, `cssModule`, `assets`, `capabilities`, `checksum` are validated at build time. **Tidak** mengizinkan CSS/JS URL dari internet ataupun user-imported executable themes pada v1.

## 5. Pemilik kode di repository: fakta dan target

**Current main**:
- `web/styles-src/00-font-and-assets.css` menyimpan font + encoded existing hardware assets (besar);
- `10-console-legacy.css` berisi geometry dan banyak style historis bercampur;
- `20-button-180.css` pemilik tombol yang saat ini raised-crystal;
- `30-selection-effects.css` pemilik selection effects;
- `40-rail-pan.css` pemilik spesifik rail/PAN;
- `scripts/build_ui_css.py` merakit `web/styles.css` dan memverifikasi parity standalone `AR-MIX-Live-FX-Host-Standalone-v0.3.html`;
- `web/app.js` membuat strip/Master, menyimpan `armix-v03-*` untuk snapshot/audio UI, dan `web/bridge.js` menjaga hubungan native. Native audio *bukan* theme owner.

**Target modular tanpa mengganti framework:**
```text
web/
  styles-src/
    00-font-and-assets.css              existing, canonical assets
    10-console-legacy.css               migrate property-by-property
    20-button-180.css                   refactor to consume appearance tokens
    30-selection-effects.css            consume state tokens
    40-rail-pan.css                     mechanical pan ownership preserved
    50-component-materials.css          sole component renderer for surfaces/fx/meters
    60-theme-tokens.css                 generated from the two approved manifests
  themes/
    manifest.json                        registry allowlist
    studio-default.tokens.json
    brushed-metal.tokens.json
    theme-contract.schema.json           schema + allowed property list
  theme-manager.js                       tiny, independently testable; no DSP imports
tests/theme_*.py                         static validation/parity
tests/theme_switch.spec.js               browser geometry/behavior
docs/ARD_MULTITHEME_ENGINE_V1_2026-10-26.md
docs/THEME_ENGINE_IMPLEMENTATION_PLAN_2026-10-26.md
```

`scripts/build_ui_css.py` memakai daftar module **deterministik** dan menghasilkan CSS tunggal offline. `web/index.html` meload theme manager dan static CSS; standalone memasukkan inline JS/CSS **byte-parity** untuk browser offline. Native hanya load file yang telah dibundel.

### Cascade: risiko terpenting

`10-console-legacy.css` mengandung banyak aturan *unlayered* dan `!important`. Jangan menerapkan `@layer` secara membabi buta: unlayered normal rules mengalahkan normal rules di layer, sedangkan `!important` membalik urutan prioritas layer. Pertama pindahkan deklarasi material tertentu dari legacy ke pemilik component secara **mekanis dengan screenshot/geometry diff**; perbaiki specificity dan `!important` hanya pada property terkait. Setelah seluruh consumer token siap, barulah konsolidasi ke `@layer reset, base, layout, component, state, accessibility`, bila benar-benar memberi benefit. **Tidak boleh** membuat `99-final-override.css` baru atau `button{...!important}` universal.

Komponen dynamic dari JS/bridge wajib ikut melalui semantic role atau inherited tokens; bukan daftar selector halaman yang diduplikasi di setiap theme.

## 6. Preference, startup, native compatibility

- Key tersendiri: `armix.ui.theme.v1`; jangan disatukan dengan preset/snapshot `armix-v03-current`, audio devices, atau DSP scene.
- Nilai yang disimpan hanya **ID allowlisted** dan schema version; invalid/removed/corrupt -> `studio-default`, log satu warning non-blocking.
- Storage browser: `localStorage` bila origin stabil. `file://` atau `NavigateToString` **tidak boleh diasumsikan** memiliki persistence sama dengan HTTPS; verifikasi loading method native. Microsoft WebView2 mendukung localStorage dengan virtual-host-name mapping (origin HTTPS) bila host setup kompatibel; native user settings JSON boleh menjadi authoritative saat persisten per-profile dibutuhkan.
- Theme manager tidak mengubah state di bridge; native startup harus memberikan initial theme sebelum paint jika native settings authoritative. Skenario mismatch browser/native resolve sekali, bukan loop dua sumber yang saling menimpa.
- Switch state `<html data-armix-theme="...">` tanpa `renderMixer`, `renderMaster`, `send()`, audio restart atau reattachment events. Debounce write preference saat dibutuhkan, tapi visual update segera.
- Scope pilihan theme bersifat **application preference**, tidak diekspor bersama audio session, kecuali nanti ada field UI preference eksplisit dengan versi sendiri.
- UX chooser: menu **Appearance** yang kecil dan accessible di aplikasi; jangan menyamar sebagai SETUP ASIO/WASAPI atau menambah tombol yang membuat toolbar tabrakan. Letak final harus diaudit pada 1280+ sebelum merge. Tampilkan preview Default vs Brushed + nama + indikator selected. Accessible keyboard/focus.

## 7. Asset strategy dan fader engineering

Existing metal hardware assets dan hotspot fader asli **dipertahankan**. Asset tema baru boleh aktif setelah:
1. Width/height bitmap, `background-size`, clickable hitbox, detent position, `--pos` mapping, arah fader dan thumb artwork **identik**.
2. Pan thumb canonical 18×40 dan `::before top=13px`, `fill top=15px`, `::after top=12px`, z-index thumb di depan; theme boleh recolor texture **tanpa mengubah lokasi**.
3. Validasi semua image path/data URI telah dibundel, tidak ada external network; fallback ke canonical asset bila missing.
4. Uji 125%/150% Windows DPI serta Chromium zoom 100/125/150%; screenshot dan pointer drag.
5. Artwork fader master tetap distinct red agar tidak salah channel.

Gunakan hanya *visual CSS variables* dan asset switch predeclared pada host; jangan menaikkan `width` atau `height` lalu “memperbaikinya” melalui `transform:scale()`.

## 8. Safety, accessibility, performance, reliability

- WCAG 2.2 AA sebagai target: text contrast normal ≥4.5:1, meaningful controls/state indicators ≥3:1; focus visible keyboard tidak hilang karena metal glare; label `MUTE` tetap tertulis.
- `prefers-reduced-motion` mengurangi transisi decorative; `forced-colors` dan high-contrast memakai fallback yang jelas tanpa glossy masking.
- Bypass FX adalah state nyata, bukan animasi/button color; theme switch tidak boleh mengubah `aria-pressed`, meter values, PAN, fader dB, channel names, FX chain, atau status native.
- Theme tidak boleh membawa script/event listener, font URL eksternal, style injection dari input mentah, dynamic stylesheet fetch, atau import CSS non-allowlisted.
- Performance budget sebagai **acceptance target** (perlu diukur, bukan sudah terbukti): theme apply P95 <100 ms pada mesin Windows target untuk 13 strips; 50 perubahan theme tidak meningkatkan jumlah DOM node, handler, timer, audio bridge call; tidak menimbulkan long jank pada drag audio controls. Capture DevTools timings untuk bukti; tidak mengklaim hard realtime.
- Modul preferensi teruji saat `localStorage` disabled/quota exception, restored profile, offline HTML, native WebView2, dan manifest mismatch.
- Theme baru wajib lolos existing `tests/ui_smoke.py`, `tests/gui_baseline_lock_test.py`, PNG canonical checks, JS syntax, standalone parity, plus theme contract tests. Native Windows CI juga wajib sebelum merge.

## 9. Definition of Done v1

- Default tanpa preference: **pixel-equivalent** terhadap `main` pada commit integrasi, atau perubahan diff memiliki explicit approval.
- Brushed Metal preview cocok dengan silver target yang baru disetujui; FX glasses tetap blue-black.
- Kedua theme menghasilkan geometry sama (bounded ≤0.5 CSS px untuk critical x/y/size); dB readouts channel ↔ MASTER align; 12 channel + MASTER, 6 FX slots, fader/PAN dB tracks intact.
- Change theme di MIX, FX RACK, SIGNAL FLOW, modal plugin serta native dialogs; tidak ada area yang tertinggal atau berubah menjadi metal pada FX.
- MUTE, SOLO, HPF, phase, fader drag, PAN center, FX bypass, native bridge berjalan identik; audio device tidak restart; session unchanged.
- Switch preference bertahan restart browser/native jika storage kompatibel; safe fallback bila gagal.
- Golden screenshots 1680×920, 1440×820, 1280×768, 1100×740, 125%/150% DPI; optional quick visual review oleh product owner.
- Release hanya setelah regression suite green. Tidak menyatakan FAT audio tercapai karena tema tidak berkaitan dengan kualifikasi DSP.

## 10. ADR (keputusan versus alternatif)

| Opsi | Keputusan | Alasan |
| --- | --- | --- |
| CSS duplicate per full layout | **REJECT** | Setiap perubahan spacing/DOM harus diulang dua theme |
| Material tokens + centralized component renderer | **ADOPT** | Layout core tunggal; theme pack hanya data |
| Runtime React/Shadow DOM migration | **REJECT v1** | Rewrite besar, mengancam bridge native, tidak diperlukan untuk 2 theme |
| Runtime arbitrary remote theme CSS | **REJECT v1** | Risiko injection, offline breakage, uncontrolled geometry |
| Static precompiled assets/CSS | **ADOPT** | Deterministik, offline, parity dengan standalone/WebView2 |
| `@layer` langsung di atas legacy `!important` | **REJECT** | Inversi precedence dan unlayered issue; migrasi dulu |
| Theme preference di audio snapshots | **REJECT** | UI appearance tidak boleh memicu audio/session recall |
| Native settings optional adapter | **ADOPT after verification** | Persisten lintas restart bila origin WebView berbeda |

### Referensi teknik (research, bukan klaim pengujian lokal)
- Microsoft Fluent 2: global/alias tokens untuk theme lintas platform — https://fluent2.microsoft.design/design-tokens/
- W3C Design Tokens Community Group Format Module 2025.10 (Community Group Report, **bukan** W3C Recommendation) — https://www.w3.org/community/reports/design-tokens/CG-FINAL-format-20251028/
- MDN CSS cascade layers/unlayered precedence — https://developer.mozilla.org/en-US/docs/Learn_web_development/Core/Styling_basics/Cascade_layers
- MDN CSS custom properties/inheritance — https://developer.mozilla.org/en-US/docs/Web/CSS/Guides/Cascading_variables/Using_custom_properties
- Microsoft WebView2 virtual-host mapping & storage origin — https://learn.microsoft.com/microsoft-edge/webview2/concepts/working-with-local-content
- W3C WCAG 2.2, 1.4.11 Non-text contrast — https://www.w3.org/WAI/WCAG22/Understanding/non-text-contrast.html

**Execution backlog dan test ladder:** [THEME_ENGINE_IMPLEMENTATION_PLAN_2026-10-26.md](THEME_ENGINE_IMPLEMENTATION_PLAN_2026-10-26.md).
