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
Production uses `arsonmix.pages.dev` for canonical, OG, sitemap and robots,
rather than GitHub's `/arsonmix/` subpath. Cloudflare Pages serves `_headers`
to prevent stale release metadata and to keep non-main previews out of search.

## If you attach your own domain later

Cloudflare Pages → **arsonmix → Custom domains** → Set up a custom domain.
Set `ARSONMIX_PUBLIC_URL=https://your-hostname.example/` in the Pages
production environment variables, then redeploy production once to update
canonical/OG/sitemap. Otherwise the site still works but SEO metadata points
at the `pages.dev` address. Do not include a project subpath in the URL.

## Important publishing details

The Cloudflare Pages build is **static hosting** for landing, demo GUI,
release catalog and images. It is NOT the native audio engine. Download links
remain official GitHub Release assets, checked for SHA-256 digests and
provenance in the existing release-catalog generator. The JS client can query
public GitHub Releases read-only if the static catalog was unavailable during
build. No fake or missing native installers are shown.

The existing GitHub Pages publisher continues independently. Both hosts use
the exact same single `scripts/build_pages.py` assembly, not two visual
branches. If abandoning GitHub Pages later, disable its deployment only
after verifying the Cloudflare production URL and release downloads.
