#!/usr/bin/env python3
"""Observe first-party Windows x64 VST3 releases without authorising redistribution.

Selection is by *actual VST3 asset*, not GitHub's releases/latest (which omits
prereleases and can point to a standalone-only stable release). The watcher is
metadata-only: an observed binary can NEVER modify the reviewed FactoryFX lock.
"""
import json
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path

PRODUCTS = [
    ("arson-eq", "masarray/arson-eq"),
    ("arson-comp", "masarray/arson-comp"),
    ("arson-verb", "masarray/arsonverb"),
    # The Chrome extension is NOT a VST3. Public binary distribution is here;
    # original proprietary DSP/source is in the separate askp-vst repository.
    ("arsonkupik-enhancer", "masarray/vst-enhancer"),
]

def windows_vst3_assets(release):
    return sorted(
        a["name"] for a in release.get("assets", [])
        if isinstance(a, dict) and isinstance(a.get("name"), str)
        and "windows" in a["name"].lower()
        and "x64" in a["name"].lower()
        and "vst3" in a["name"].lower()
        and a["name"].lower().endswith(".zip")
    )


def select_candidate(releases, repo):
    """Choose latest published VST3-bearing release, including test prereleases.

    A newer standalone-only release must not hide a compatible VST3 artifact.
    Ambiguous release archives are rejected rather than chosen arbitrarily.
    """
    if not isinstance(releases, list):
        raise ValueError(repo + ": malformed GitHub releases response")
    matches = []
    for release in releases:
        if not isinstance(release, dict) or release.get("draft"):
            continue
        names = windows_vst3_assets(release)
        if len(names) > 1:
            raise ValueError(repo + ": ambiguous Windows x64 VST3 assets in " +
                             str(release.get("tag_name")))
        if names:
            if not release.get("tag_name") or not release.get("published_at"):
                raise ValueError(repo + ": missing published release identity")
            matches.append(release)
    return max(matches, key=lambda x: (x["published_at"], x["tag_name"])) if matches else None


def latest(repo):
    # Bounded metadata request: does not download .vst3 or build plugin source.
    url = "https://api.github.com/repos/" + repo + "/releases?per_page=30"
    headers = {
        "Accept": "application/vnd.github+json",
        "User-Agent": "ArSonMix-FactoryFX-Candidate-Watch",
        "X-GitHub-Api-Version": "2022-11-28",
    }
    if os.environ.get("GITHUB_TOKEN"):
        headers["Authorization"] = "Bearer " + os.environ["GITHUB_TOKEN"]
    try:
        with urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=20) as response:
            return select_candidate(json.load(response), repo)
    except urllib.error.HTTPError as error:
        if error.code == 404:
            return None
        raise


def snapshot(fetch=latest):
    observed = []
    for product_id, repo in PRODUCTS:
        release = fetch(repo)
        if release is None:
            row = dict(productId=product_id, distributionRepo=repo, releaseTag=None,
                       publishedAt=None, windowsVst3Asset=None, prerelease=None)
        else:
            names = windows_vst3_assets(release)
            if len(names) != 1:
                raise ValueError(repo + ": selected release has no unique VST3 asset")
            row = dict(productId=product_id, distributionRepo=repo,
                       releaseTag=release["tag_name"], publishedAt=release["published_at"],
                       windowsVst3Asset=names[0],
                       prerelease=bool(release.get("prerelease", False)))
        observed.append(row)
    return {"schemaVersion": 1, "observed": observed}


if __name__ == "__main__":
    output = Path(sys.argv[1] if len(sys.argv) > 1 else "config/factoryfx-upstream-candidates.json")
    try:
        candidate = snapshot()
        rendered = json.dumps(candidate, indent=2, ensure_ascii=False) + "\n"
        output.parent.mkdir(parents=True, exist_ok=True)
        if not output.exists() or output.read_text(encoding="utf-8") != rendered:
            output.write_text(rendered, encoding="utf-8")
        for item in candidate["observed"]:
            print(item["productId"] + ": " + str(item["releaseTag"]) +
                  " / " + str(item["windowsVst3Asset"]) +
                  (" (TEST candidate)" if item["prerelease"] else ""))
        print("OBSERVATION ONLY — not approved, licensed, host-qualified or bundled")
    except (OSError, ValueError, KeyError, urllib.error.URLError) as error:
        print("RELEASE WATCH FAILED: " + str(error), file=sys.stderr)
        sys.exit(1)
