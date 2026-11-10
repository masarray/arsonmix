"""Publish a strict static catalog from verified GitHub Release assets.

The browser first loads this same-origin catalog and may recover via the public
GitHub Releases API when Pages is stale. Never point to Actions artifacts or
unverified / absent OS binaries.
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
    # A direct Setup EXE is the least confusing Windows download; verified
    # installer ZIP remains a fallback when the EXE asset is absent.
    "windows": ("Windows-x64-Setup.exe", "Windows-x64-Setup.zip"),
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


def release_sort_key(release):
    """Version order, not just upload timestamps (backfilled assets are common).

    Pre-releases are ordered alpha -> beta -> rc -> stable inside each version.
    Exact tag and GitHub asset digest guards still apply during selection.
    """
    if not isinstance(release, dict):
        return (-1, -1, -1, -1, -1, "")
    match = re.fullmatch(
        r"v(\d+)\.(\d+)\.(\d+)(?:-(alpha|beta|rc)\.(\d+))?",
        release.get("tag_name") if isinstance(release.get("tag_name"), str) else "",
    )
    if not match:
        return (-1, -1, -1, -1, -1, "")
    major, minor, patch, stage, sequence = match.groups()
    stage_number = {"alpha": 0, "beta": 1, "rc": 2, None: 3}[stage]
    return (int(major), int(minor), int(patch), stage_number,
            int(sequence) if sequence else 0, release.get("published_at") or "")


def catalog(releases):
    if not isinstance(releases, list):
        raise ValueError("Release API response must be an array")
    selected = {key: None for key in SUFFIXES}
    # Newest valid VERSION per OS. A backfilled older upload cannot displace
    # a more recent alpha; direct EXE takes priority over ZIP within one tag.
    ordered = sorted((r for r in releases if isinstance(r, dict)),
                     key=release_sort_key, reverse=True)
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
