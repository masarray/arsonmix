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
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

from build_release_catalog import (
    REPO, SUFFIXES, TAG, catalog, download_releases, hydrate_landing_file,
)
from pages_public_origin import DEFAULT_PUBLIC_URL, normalize_public_url

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "dist-pages"
# GitHub Pages is the established primary search URL (README, verified Pages).
# Cloudflare is a mirror; preserve an explicit ARSONMIX_PUBLIC_URL override for
# a future coordinated primary-domain migration.
DEFAULT_URL = DEFAULT_PUBLIC_URL
GITHUB_PAGES_CATALOG = "https://masarray.github.io/arsonmix/release-catalog.json"


def public_url(environ):
    # CF_PAGES_URL is a preview deployment on branch builds; don't put that
    # unstable URL in canonical/OG tags. Custom production domains may override.
    configured = normalize_public_url(environ.get("ARSONMIX_PUBLIC_URL") or DEFAULT_URL)
    # Respect intentional custom-domain migrations, but never treat the known
    # duplicate pages.dev mirror as a competing primary—even if configured in
    # an older Cloudflare environment variable.
    if configured == "https://arsonmix.pages.dev/":
        return DEFAULT_URL
    return configured


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
    else:
        # The mirror hosts the same interactive mixer preview. Index only the
        # original approved preview on GitHub Pages; this remains navigable.
        headers += "/preview/*\n  X-Robots-Tag: noindex\n"
    return headers


def fetch_official_pages_catalog():
    """Read independently published verified release metadata from GitHub Pages."""
    req = urllib.request.Request(
        GITHUB_PAGES_CATALOG,
        headers={"Accept": "application/json",
                 "User-Agent": "ArSonMix-Cloudflare-VerifiedCatalog/1",
                 "Cache-Control": "no-cache"},
    )
    with urllib.request.urlopen(req, timeout=15) as response:
        return json.load(response)


def validate_official_pages_catalog(snapshot):
    """Never promote unverified or forged fallback links."""
    if (not isinstance(snapshot, dict) or
            set(snapshot) != {"schemaVersion", "platforms"} or
            type(snapshot["schemaVersion"]) is not int or
            snapshot["schemaVersion"] != 1 or
            not isinstance(snapshot["platforms"], dict) or
            set(snapshot["platforms"]) != set(SUFFIXES)):
        raise ValueError("Invalid official Pages catalog schema")

    valid = 0
    fields = {"tag", "name", "url", "size", "sha256",
              "prerelease", "publishedAt"}
    for platform, item in snapshot["platforms"].items():
        if item is None:
            continue
        if not isinstance(item, dict) or set(item) != fields:
            raise ValueError("Invalid Pages asset fields: " + platform)
        tag, name = item["tag"], item["name"]
        if (not isinstance(tag, str) or not TAG.fullmatch(tag) or
                not isinstance(name, str) or
                name not in (f"ArSonMix-{tag}-{suffix}"
                             for suffix in SUFFIXES[platform]) or
                item["url"] !=
                f"https://github.com/{REPO}/releases/download/{tag}/{name}" or
                type(item["size"]) is not int or item["size"] < 2_097_152 or
                not isinstance(item["sha256"], str) or
                not re.fullmatch(r"[a-f0-9]{64}", item["sha256"]) or
                type(item["prerelease"]) is not bool or
                not isinstance(item["publishedAt"], str) or
                not re.fullmatch(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z",
                                 item["publishedAt"])):
            raise ValueError("Unverified Pages asset: " + platform)
        valid += 1
    if not valid:
        raise ValueError("No verified assets in official Pages catalog")
    return snapshot


def write_catalog(path, fetch=download_releases,
                  snapshot_fetch=fetch_official_pages_catalog,
                  pause=time.sleep):
    """Prefer live signed releases; fall back only on API transport outages.

    An independently produced, strictly checked GitHub Pages catalog is the
    emergency read-only source for Cloudflare API throttling. Both unavailable:
    fail closed and preserve the previous published Cloudflare site.
    """
    verified = None
    network_error = None
    source = "GitHub Releases API"
    for attempt in range(3):
        try:
            verified = catalog(fetch())
            break
        except (OSError, urllib.error.URLError) as exc:
            network_error = exc
            if attempt < 2:
                pause((1, 2)[attempt])
    if verified is None:
        try:
            verified = validate_official_pages_catalog(snapshot_fetch())
        except (OSError, urllib.error.URLError, ValueError, TypeError) as exc:
            raise RuntimeError(
                "Both Release API and official verified Pages catalog unavailable; "
                "preserve existing Cloudflare deployment"
            ) from exc
        source = "verified GitHub Pages catalog (network fallback)"
        print("ARSONMIX_RELEASE_API_UNAVAILABLE",
              type(network_error).__name__, flush=True)
    if not any(verified["platforms"].values()):
        raise ValueError("No verified published assets; preserve existing deployment")
    path.write_text(json.dumps(verified, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8")
    print("ARSONMIX_VERIFIED_RELEASE_SOURCE", source, flush=True)
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
    verified = write_catalog(OUT / "release-catalog.json")
    hydrate_landing_file(OUT / "index.html", verified)
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
