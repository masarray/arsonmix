#!/usr/bin/env python3
"""Git-connected Cloudflare Pages: assemble the approved ArSonMix static site.

Cloudflare configuration: framework None; build command
    python3 scripts/build_cloudflare_pages.py
build output directory dist-pages; repository root unchanged.

Uses the same single public /preview/ assembler as GitHub Pages. There is no
parallel GUI renderer, Windows build, VST3 action or paid service dependency.
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
import urllib.error
from pathlib import Path

from build_release_catalog import catalog, download_releases
from pages_public_origin import normalize_public_url

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "dist-pages"
DEFAULT_URL = "https://arsonmix.pages.dev/"


def public_url(environ):
    # CF_PAGES_URL is a preview deployment on branch builds; don't put that
    # unstable URL in canonical/OG tags. Custom production domains may override.
    return normalize_public_url(environ.get("ARSONMIX_PUBLIC_URL") or DEFAULT_URL)


def response_headers(branch):
    # Cloudflare _headers only: do not introduce a Worker, proxy or JS rewrite.
    # Preview deployments must not compete with the production search result.
    headers = (
        "/release-catalog.json\n"
        "  Cache-Control: no-store\n"
        "/preview/build.json\n"
        "  Cache-Control: no-store\n"
    )
    if branch != "main":
        headers += "/*\n  X-Robots-Tag: noindex\n"
    return headers


def write_catalog(path, fetch=download_releases):
    # Use GitHub Releases metadata exactly like the existing Pages builder.
    # Failure to fetch must never create counterfeit binaries or stale URLs.
    try:
        releases = fetch()
    except (OSError, urllib.error.URLError) as exc:
        print("ARSONMIX_RELEASE_API_UNAVAILABLE:", str(exc),
              "(empty verified catalog; browser has read-only live fallback)",
              file=sys.stderr)
        releases = []
    verified = catalog(releases)
    path.write_text(json.dumps(verified, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("ARSONMIX_VERIFIED_RELEASES",
          " ".join(k + "=" + (v["tag"] if v else "none")
                   for k, v in verified["platforms"].items()))
    return verified


def main():
    env = dict(os.environ)
    env["ARSONMIX_PUBLIC_URL"] = public_url(env)
    branch = env.get("CF_PAGES_BRANCH") or "main"
    sha = env.get("CF_PAGES_COMMIT_SHA") or "local"
    print("ARSONMIX_CLOUDFLARE_BUILD", branch, sha, env["ARSONMIX_PUBLIC_URL"],
          flush=True)

    def run(*args):
        subprocess.run([sys.executable, *args], cwd=ROOT, env=env, check=True)

    # One source, one owner-approved renderer, identical to GitHub Pages.
    run("scripts/build_pages.py", "--out", str(OUT),
        "--sha", sha, "--branch", branch)
    run("scripts/build_landing_assets.py", str(OUT))
    write_catalog(OUT / "release-catalog.json")
    (OUT / "_headers").write_text(response_headers(branch), encoding="utf-8")

    # Same critical contracts as GitHub Pages; no lengthy Windows/Chromium CI.
    run("tests/test_release_catalog.py")
    run("tests/single_preview_publication_contract_test.py", str(OUT))
    run("tests/landing_page_download_contract_test.py", str(OUT))
    run("tests/test_pages_public_origin.py", str(OUT))
    run("tests/test_cloudflare_pages_build.py")
    required = ("index.html", "preview/index.html", "preview/build.json",
                "release-catalog.json", "_headers", "sitemap.xml", "robots.txt",
                "landing.css", "downloads.js")
    for name in required:
        if not (OUT / name).is_file():
            raise RuntimeError("Cloudflare output missing: " + name)
    print("ARSONMIX_CLOUDFLARE_READY dist-pages", flush=True)


if __name__ == "__main__":
    main()
