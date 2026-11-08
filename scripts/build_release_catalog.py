"""Publish a strict, static download catalog built from actual GitHub Release assets.

Browser uses only same-origin release-catalog.json. No GitHub API, Actions or
release-page navigation. Invalid/absent OS binaries are never offered.
"""
import argparse
import json
import os
import re
import sys
import urllib.error
import urllib.request
from pathlib import Path

REPO = "masarray/arsonmix"
API = f"https://api.github.com/repos/{REPO}/releases?per_page=100"
TAG = re.compile(r"^v\d+\.\d+\.\d+(?:-(?:alpha|beta|rc)\.\d+)?$")
DIGEST = re.compile(r"^sha256:([a-f0-9]{64})$")
SHA = re.compile(r"^[a-f0-9]{40}$")
SUFFIXES = {
    "windows": ("Windows-x64-Setup.exe",),
    "linux": ("Linux-x86_64.AppImage", "Linux-x86_64.tar.gz"),
    "macos": ("macOS-universal.dmg", "macOS-arm64.dmg", "macOS-x64.dmg"),
}
PROVENANCE = {
    "windows": "INSTALLER_PROVENANCE.json",
    "linux": "LINUX_PROVENANCE.json",
    "macos": "MACOS_PROVENANCE.json",
}


def signed_asset(asset, name, tag, binary=False):
    if not isinstance(asset, dict) or asset.get("name") != name:
        return False
    if asset.get("state") != "uploaded" or type(asset.get("size")) is not int:
        return False
    if asset["size"] < (2_097_152 if binary else 1):
        return False
    if not isinstance(asset.get("digest"), str) or not DIGEST.fullmatch(asset["digest"]):
        return False
    url = f"https://github.com/{REPO}/releases/download/{tag}/{name}"
    return asset.get("browser_download_url") == url


def catalog(releases):
    if not isinstance(releases, list):
        raise ValueError("Release API response must be an array")
    selected = {key: None for key in SUFFIXES}
    # Newest publication per OS, never infer assets of an unqualified platform.
    ordered = sorted((r for r in releases if isinstance(r, dict)),
                     key=lambda r: r.get("published_at") or "", reverse=True)
    for release in ordered:
        if release.get("draft"):
            continue
        tag, assets = release.get("tag_name"), release.get("assets")
        if (not isinstance(tag, str) or not TAG.fullmatch(tag) or
                not isinstance(assets, list) or
                not isinstance(release.get("published_at"), str) or
                not isinstance(release.get("target_commitish"), str) or
                not SHA.fullmatch(release["target_commitish"])):
            continue
        if sum(signed_asset(a, "SHA256SUMS.txt", tag) for a in assets) != 1:
            continue
        for platform, suffixes in SUFFIXES.items():
            if selected[platform] is not None:
                continue
            if sum(signed_asset(a, PROVENANCE[platform], tag) for a in assets) != 1:
                continue
            for suffix in suffixes:
                name = f"ArSonMix-{tag}-{suffix}"
                matched = [a for a in assets if signed_asset(a, name, tag, binary=True)]
                if len(matched) != 1:
                    continue
                a = matched[0]
                selected[platform] = {
                    "tag": tag, "name": name, "url": a["browser_download_url"],
                    "size": a["size"], "sha256": DIGEST.fullmatch(a["digest"]).group(1),
                    "prerelease": bool(release.get("prerelease")),
                    "publishedAt": release["published_at"],
                }
                break
    return {"schemaVersion": 1, "platforms": selected}


def download_releases():
    headers = {"Accept": "application/vnd.github+json",
               "User-Agent": "ArSonMix-Pages-Release-Catalog/1"}
    if os.environ.get("GITHUB_TOKEN"):
        headers["Authorization"] = "Bearer " + os.environ["GITHUB_TOKEN"]
    with urllib.request.urlopen(urllib.request.Request(API, headers=headers), timeout=20) as response:
        return json.load(response)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--fixture", help="Offline fixture for deterministic tests")
    args = parser.parse_args()
    data = json.loads(Path(args.fixture).read_text("utf-8")) if args.fixture else download_releases()
    approved = catalog(data)
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(approved, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    print("ARSONMIX_RELEASE_CATALOG_VERIFIED " + " ".join(
        key + "=" + (v["tag"] if v else "unavailable") for key, v in approved["platforms"].items()))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, urllib.error.URLError) as exc:
        sys.exit("Release catalog build failed; preserve last deployed landing: " + str(exc))
