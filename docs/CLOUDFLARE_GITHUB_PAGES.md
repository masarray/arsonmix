# ArSonMix GitHub Pages + Cloudflare — short setup

**Hosting remains GitHub Pages**, not Cloudflare Pages. Cloudflare handles DNS
(and optionally proxy/HTTPS); no Workers, API tokens, paid signing or second CI
pipeline are necessary. The existing guarded `pages-publish.yml` stays the
single publisher. Pages' `configure-pages@v5` selects the correct public URL
for canonical / Open Graph tags and sitemap on each real publication.

## Recommended: a subdomain (example `mix.example.com`)

1. In **GitHub** open [masarray/arsonmix → Settings → Pages](https://github.com/masarray/arsonmix/settings/pages).
   Source = **GitHub Actions**. Under **Custom domain** set `mix.example.com`
   (replace with the domain you actually own) and **Save**. Verify ownership
   in GitHub first if offered; this protects against domain takeover.
2. In **Cloudflare → DNS → Records**, add:
   
   | Type | Name | Target | Proxy status |
   | --- | --- | --- | --- |
   | CNAME | `mix` | `masarray.github.io` | **DNS only** (gray cloud initially) |

   **Never** include `/arsonmix` in the DNS target. Do not add wildcard CNAMEs.
3. Let GitHub report DNS/certificate ready, then select **Enforce HTTPS** in
   Settings → Pages. Visit `https://mix.example.com/` and
   `https://mix.example.com/preview/`. The old project URL was
   `https://masarray.github.io/arsonmix/`; custom-domain routes are rooted
   at `/` instead.
4. Optional: switch the Cloudflare record to **Proxied** (orange cloud) after
   HTTPS is working. Cloudflare **SSL/TLS → Overview: Full (strict)**; do not
   use **Flexible**. You may enable **Always Use HTTPS**.
5. Cloudflare **Caching → Cache Rules**: create **Bypass cache** for requests
   whose URI path ends in `/release-catalog.json` or `/preview/build.json`.
   Do **not** turn on blanket Cache Everything for HTML or JSON; don't use
   Rocket Loader or JS rewriting on the mixer preview. Normal static
   CSS/JS/images can use standard browser/edge caching.

For an apex domain (`example.com`) instead of a subdomain, configure the apex
as the GitHub Pages custom domain, then use four Cloudflare **A** records:
`185.199.108.153`, `185.199.109.153`, `185.199.110.153`,
`185.199.111.153`. Add `www` CNAME → `masarray.github.io` if desired.
Start DNS-only and validate HTTPS before orange-cloud proxying.

## Operating / validation

- No repository `CNAME` file is needed: GitHub **Actions-based** Pages ignores
  it; GitHub Settings → Pages owns the custom-domain mapping.
- A real Pages deployment reruns the existing lightweight, guarded builder,
  and generates matching `index.html`, `robots.txt`, `sitemap.xml`,
  `/preview/`, and `release-catalog.json`. No new Windows/Linux/macOS
  installer build is triggered by DNS changes.
- Check that page source contains `<link rel="canonical"` with your actual
  HTTPS host and that `/sitemap.xml` contains your public host.
  `/preview/build.json` must identify `APPROVED_SINGLE_GUI`; no second
  mixer UI or physical audio claim was added.
- Cloudflare cannot make GitHub Pages host native installer binaries.
  The verified download buttons still use official GitHub Release assets.
- If metadata still shows the old host immediately after changing the GitHub
  custom-domain setting, publish the already existing Pages workflow once
  through its authorized deployment path; setting DNS alone does not run a build.
  Avoid unnecessary native/Chromium CI.

**References:** [GitHub custom-domain docs](https://docs.github.com/en/pages/configuring-a-custom-domain-for-your-github-pages-site/managing-a-custom-domain-for-your-github-pages-site)
and [GitHub Actions Pages source](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages).
