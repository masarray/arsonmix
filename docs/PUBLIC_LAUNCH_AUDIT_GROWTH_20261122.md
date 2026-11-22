# ArSonMix public launch audit & growth plan — 22 November 2026

## Goal and audience

Turn an engineering-heavy repository into a convincing, honest first experience for singers, karaoke users, live streamers and audio-interface owners. The public path should answer, in order: **what is this → what does it look like → what can it do → how do I install it → how do I report a problem?**

This is a **public presentation and feedback** milestone, not a native mixer or DSP rewrite. Canonical GUI, controls, accepted sound, release contracts and device safety remain owned by their existing integration lanes.

## Evidence from current main and published releases

- Current public source already includes a branded `site/index.html`, dark responsive CSS, the verified live release resolver `site/downloads.js`, a single approved `/preview/`, and three genuine Chromium mixer gallery JPEGs in `site/media/`. Preserve and reuse these. Do not resurrect rejected secondary GUI variants.
- The 22 November public release `v0.5.0-alpha.32` exists as a Windows x64 prerelease with Setup ZIP/EXE, Portable ZIP, provenance and SHA256SUMS. Its release body explicitly does **not** certify dual-vendor ASIO, unmerged experiments or all hardware.
- Before this branch, README's first download paragraph still called the 17 November alpha.26 the latest despite alpha.32. Static pinned "latest" links age poorly.
- Three mixer screenshots existed; **no accepted native built-in FX editor screenshots** have yet been added to `site/media/`. Historical v0.3 editor screenshots must not be promoted as a current application.
- Engineering history, RT architecture and AGENTS.md are important for contributors but overwhelming for non-programmers. Show a consumer first, technical references later.
- Existing `.github/ISSUE_TEMPLATE` requests were engineering-oriented; a novice's first bug should be understandable without protocol details.

## Changes in this branch (public-facing scope)

1. Restructure README as a visual storefront: real approved screenshot, examples, plain-language effects, three-step quickstart, field-test disclosures, official downloads and feedback, with developer docs collapsed.
2. Remove README's time-sensitive "latest published alpha.26" release assertion. Let the official signed/verified landing resolver identify current binary; retain an evergreen releases/notes link.
3. Add a dedicated **FX overview** on the landing, using the *authentic mixer FX rack screenshot*, with accurate effect summaries and a disclaimer that it is not a native editor screenshot.
4. Add **first sound** onboarding and a direct **feedback** CTA. Improve the existing bug form for users who don't know audio driver terminology.
5. Keep the established Pages release catalog, same-origin download validation, AGPL notice, site brand, one compiled `/preview/` and native WebView2 rendering unchanged.
6. Adjust the site contract to permit explicit GitHub Issues feedback links while continuing to forbid pinned unverified binary URLs in the source landing.

## Next user-visible milestone: native FX media, not mockups

Capture verified native Windows images for **ImmerSIB, Harmonice, Vocal Delay, 8D-SonKu, Guitar FX, EQ/Compressor** as supported by the specific installed release. A screenshot set must include binary tag/exact SHA, capture date, real native editor window identity, readable controls, no personal data and no editing that falsifies feature state. If an editor is only in a pending PR, keep it out of the shipped gallery.

After capture: add a small 2–4 image authentic native FX gallery with meaningful alt text, compressed WebP/JPEG variants, explicit captions, responsive sizes and image load tests. Keep native screenshots distinct from browser-only `/preview/` evidence. This milestone **cannot be truthfully marked complete** until those actual Windows captures exist.

## Growth and quality loop

| Stage | User question | Evidence/KPI | Boundaries |
| --- | --- | --- | --- |
| Discovery | Do I understand ArSonMix in 30 seconds? | 5-person unprompted first-impression test; comprehension and next-click results | No fabricated testimonials/reviews |
| Consideration | Does the GUI/FX look genuine? | Actual screenshots, one canonical interactive preview and signed release provenance | No historical/mockup images presented as native |
| Download | Can I get the right build? | Latest verified release asset counts, Pages and Cloudflare link parity, binary checksum checks | GitHub download counts are **downloads**, not distinct installs/users |
| First sound | Can I safely hear my microphone/music? | Opt-in tester results: installation, initial output, chosen backend, dropout | CI/Chromium is not physical ASIO FAT |
| Retention | Can I save/reopen a usable session? | Voluntary `.smix` save/reopen and device restart reports | Preserve safe rollback and user data |
| Feedback | Do reports lead to fixes? | Number of actionable issue reports, reproducibility, time to close and release regression results | Never require private audio, keys or paths |

Do not install invasive analytics or unconsented telemetry for this marketing milestone. If later approved, use privacy-conscious aggregate, opt-in metrics and explicit disclosure. For now, GitHub release asset download counts and voluntarily submitted issues support trends but **cannot prove installation or active usage**.

## Suggested acceptance and next actions

- [x] README and landing clearly communicate consumer value with approved real GUI images.
- [x] Evergreen verified-download link; no stale pinned "latest" version in README.
- [x] Built-in FX overview + unambiguous GUI vs Windows-native screenshots.
- [x] Installation quickstart + approachable bug-report workflow.
- [ ] Run landing contract + static Pages build + desktop/mobile Chromium visual smoke on exact PR HEAD.
- [ ] Review actual GitHub Pages and any Cloudflare production URL after protected merge/deploy.
- [ ] Capture current native FX editor screenshots on a real Windows release.
- [ ] Recruit a small, voluntary novice-user test group and act on the first install/audio failure patterns.

Cross-thread coordination: [INT-1 #837](https://github.com/masarray/arsonmix/issues/837). Only a qualified exact-SHA installer is publicly described as released. Other lanes may change release readiness; refresh current main before updating public feature claims.
