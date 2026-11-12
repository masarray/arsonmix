# Public preview routes — live and frozen comparison

There are **two intentional GitHub Pages routes**, with different roles:

| Public URL | Role | Source |
| --- | --- | --- |
| `/preview/` | Current, qualified mixer GUI. Keeps evolving with `main`. Also owns the matching native WebView2 composition. | Latest `main` |
| `/preview-prod/` | Frozen, browser-only snapshot of the previous approved GUI **before Harmonice v7 PNG buttons**. Includes MUTE/SOLO/PFL controls and old styling for visual comparison. | Commit `09022ef0f227ca2a40a0c2f62c619a31395c8dac` |

## Why an immutable source pin

The original previous GUI is **not** reconstructed with newer CSS, JS or
routing rules. The Pages publisher checks out the exact archived commit and
runs its own historical `scripts/build_pages.py`, validates that generated
`/preview/` with the historical single-preview contract, then copies only
its compiled preview directory into the new `/preview-prod/` path.

In `preview-prod/build.json`, `sha` and `archived_source_sha` show the
pinned commit; `kind` is `FROZEN_PRE_V7_GUI` and `frozen` is `true`.
All HTML, CSS, JavaScript, PNG, and other assets are copied byte-for-byte.
Only the archive manifest URL/type metadata differs.

- `/preview/` remains the single **live** GUI and single source for the
  native JUCE WebView2 resources.
- `/preview-prod/` is browser-only reference evidence, **not** an installer
  build, native PCM proof, another mutable GUI, or a replacement home page.
- Former legacy aliases `/preview-v2/`, `/preview-buttons/`,
  `/preview-v33/`, `/preview-v34/` still redirect to `/preview/`.
- The frozen page may remain interactive for comparison but never receives
  future feature, routing, styling or native updates.
- Avoid adding the archive to sitemap, marketing, or download links. Share
  the explicit URL when someone asks to inspect the former UI.

## Publisher and checks

`.github/workflows/pages-publish.yml` assembles both paths into one
`dist-pages` bundle in the same Pages run. A **second checkout and small
historical build happen only during Pages publishing**, not in regular
Chromium PR gates or costly Windows build/installer workflows.

`python -m unittest discover -s tests -p preview_prod_publication_contract_test.py -v`
provides a fast isolated archiver test. After both previews are assembled,
`python tests/preview_prod_publication_contract_test.py dist-pages`
validates separate old/new sprites, PFL/tap DOM and exact archive SHA.
If the pinned historical source cannot be built or validated, Pages fails
closed instead of silently redirecting `/preview-prod/` to the current GUI.

To intentionally update this frozen baseline to a different historical
release, change the pinned SHA in the installer, publisher workflow and
contract together, then re-qualify the entire historical build.
