# Deploy ArSonMix through Cloudflare Pages (Git-connected)

This is for the **Workers & Pages → Create Pages project → Connect to GitHub**
screen, not for pointing Cloudflare DNS at GitHub Pages.

## Exact settings

| Cloudflare field | Set to |
| --- | --- |
| GitHub repository | `masarray/arsonmix` |
| Project name | `arsonmix` |
| Production branch | `main` |
| Framework preset | `None` |
| Build command | `python3 scripts/build_cloudflare_pages.py` |
| Build output directory | `dist-pages` |
| Root directory (advanced) | leave blank |
| Environment variables | none required |

Do **not** put `dist-pages` into **Build command**. The builder executes
`scripts/build_pages.py`, stages landing assets, constructs a strictly
validated download catalog, and checks the one approved `/preview/` before
Cloudflare publishes. No `npm install`, Wrangler configuration, secrets,
Windows MSVC, VST3 packaging or duplicate GUI.

The production URLs are `https://arsonmix.pages.dev/` and
`https://arsonmix.pages.dev/preview/`. Cloudflare rebuilds on changes to
`main`. Old preview route names redirect to the single actual GUI.
**SEO domain policy:** The GitHub Pages site
`https://masarray.github.io/arsonmix/` is the single preferred public search
URL because the README, FX guides and public download entrypoints already
reference it. Cloudflare remains a fully usable mirror at
`https://arsonmix.pages.dev/`; its generated canonical URLs, Open Graph
identity, image sitemap and robots.txt point at the primary GitHub Pages URL.
Navigation, screenshot URLs and release metadata are still resolved locally
from whichever host the visitor is using. A second searchable copy is not
promoted. The Cloudflare copy of `/preview/` remains usable but sends a
`noindex` header; non-main branch previews also remain `noindex`.

Submit **only the primary**
`https://masarray.github.io/arsonmix/sitemap.xml` to Google Search Console.
Indexing and search rankings still depend on Google's own crawling decisions.

## If you attach your own domain later

Cloudflare Pages → **arsonmix → Custom domains** → Set up a custom domain.
First decide whether the custom domain will become the **single primary**
search address. If it will, coordinate the GitHub Pages custom-domain/redirect
and its canonical metadata before changing the Cloudflare canonical. Then set
`ARSONMIX_PUBLIC_URL=https://your-hostname.example/` in the Cloudflare Pages
production environment variables and deploy. Do not include a project subpath.
Do **not** set the override on Cloudflare alone while GitHub Pages still claims
its own URL as primary; that would recreate competing canonicals. Until the
migration is coordinated, the default remains the existing GitHub Pages URL.

## Important publishing details

The Cloudflare Pages build is **static hosting** for landing, demo GUI,
release catalog and images. It is NOT the native audio engine. Download links
remain official GitHub Release assets, checked for SHA-256 digests and
provenance in the existing release-catalog generator. Cloudflare's unauthenticated GitHub Release API egress can be rate-limited
independently from GitHub Actions. The builder retries short-lived network
errors and, only if the API is unreachable, retrieves the **independently
verified, last published official GitHub Pages catalog** at
`https://masarray.github.io/arsonmix/release-catalog.json`. Every fallback
asset must satisfy exact release URL, platform filename, SHA-256, publication
timestamp, size and schema checks before use. If both sources are unreachable,
the fallback catalog is invalid, or the reachable API has no verified assets,
the builder **fails closed** and retains the last Cloudflare deployment.
No fabricated installer or unsigned third-party mirror is introduced. The exact verified same-origin
catalog also pre-renders the three OS download cards in published HTML, so
previously approved direct asset links work without JavaScript. Browser JS can
refresh to a newer validated public release and keeps the verified HTML links
when the live API is rate-limited. No fake, absent or unqualified native
installer is shown.

The existing GitHub Pages publisher continues independently. Both hosts use
the exact same single `scripts/build_pages.py` assembly, not two visual
branches. If abandoning GitHub Pages later, disable its deployment only
after verifying the Cloudflare production URL and release downloads.

## Automatic synchronization after every official Windows release

The official Windows publisher `.github/workflows/alpha-preview-publish.yml`
first uploads **verified, immutable** GitHub Release binaries, then explicitly
dispatches GitHub Pages to regenerate the signed static catalog. This path
already published Windows `v0.5.0-alpha.32` on 22 November 2026.

Cloudflare's Git connection only rebuilds on a `main` push; a GitHub
`release` event on a separate `releases/alpha/` branch does **not** itself
publish new static Cloudflare HTML. To keep server-rendered links fresh
**immediately after future releases**, configure **one production Deploy
Hook** (the hook URL is a secret):

1. Cloudflare dashboard → Workers & Pages → `arsonmix` → Settings →
   Builds & deployments → Deploy hooks → Create hook. Select production
   branch `main`; copy the generated URL.
2. GitHub repository → Settings → Secrets and variables → Actions →
   New repository secret. Name:
   `ARSONMIX_CLOUDFLARE_PAGES_DEPLOY_HOOK`, value: the full Deploy Hook URL.
3. On the **next approved Windows release**, the verified publisher requests
   Cloudflare rebuild after GitHub Pages refresh. Its request only accepts
   HTTPS Cloudflare Pages Deploy Hook hosts/paths, logs no secret, retries
   transient failures, and never publishes unchecked downloads.

The hook **requests** a rebuild; Cloudflare then runs the existing verified
`scripts/build_cloudflare_pages.py` against `main`. Rebuild publication
must be inspected separately in Cloudflare deployments. If a secret is not
configured or Cloudflare is down, the published site's existing
`site/downloads.js` still queries GitHub's signed official release assets
at page load, but visitors with JavaScript disabled may see the older static
catalog until the next `main` deployment. Do not claim Cloudflare static
version parity solely from a successful GitHub release workflow.

Verification URLs:
- GitHub Pages: https://masarray.github.io/arsonmix/release-catalog.json
- Cloudflare: https://arsonmix.pages.dev/release-catalog.json
Both `platforms.windows.tag` and the verified `sha256` should match
the newest officially published Windows release. Linux/macOS retain their
own last verified versions and are not falsely bumped to match Windows.

## Production parity after a main merge

After Cloudflare and GitHub Pages have deployed, inspect both `#fx-gallery`
and each image response on the production hosts, not just PR preview. All six
`/media/fx-editor-{8d-sonku,vocal-delay,harmonice,channel-eq,compressor,graphic-eq31}.jpg`
must be actual JPEG bytes (not the landing HTML fallback) on both sites. Both
`release-catalog.json` snapshots must agree on the official Windows tag,
SHA-256 and URL. A green GitHub Pages deployment does not automatically prove
Cloudflare production is current; the Cloudflare GitHub check is separate.
