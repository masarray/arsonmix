#!/usr/bin/env python3
"""Inventory only: never auto-approve, download or promote upstream binary releases."""
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
    ("arsonkupik-enhancer", "masarray/vst-enhancer"),
]


def latest(repo):
    url = "https://api.github.com/repos/" + repo + "/releases/latest"
    headers = {
        "Accept": "application/vnd.github+json",
        "User-Agent": "ArSonMix-FactoryFX-Candidate-Watch",
        "X-GitHub-Api-Version": "2022-11-28",
    }
    if os.environ.get("GITHUB_TOKEN"):
        headers["Authorization"] = "Bearer " + os.environ["GITHUB_TOKEN"]
    try:
        with urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=20) as response:
            release = json.load(response)
    except urllib.error.HTTPError as error:
        if error.code == 404:  # No public release, not a successful plugin qualification.
            return None
        raise
    if release.get("draft") or release.get("prerelease"):
        return None
    return release


def snapshot(fetch=latest):
    observed = []
    for product_id, repo in PRODUCTS:
        release = fetch(repo)
        if release is None:
            row = dict(productId=product_id, distributionRepo=repo, releaseTag=None,
                       publishedAt=None, windowsVst3Asset=None)
        else:
            assets = [
                a["name"] for a in release.get("assets", [])
                if isinstance(a.get("name"), str)
                and "windows" in a["name"].lower()
                and "x64" in a["name"].lower()
                and "vst3" in a["name"].lower()
                and a["name"].lower().endswith(".zip")
            ]
            if len(assets) > 1:
                raise ValueError(repo + ": ambiguous Windows x64 VST3 release assets")
            row = dict(productId=product_id, distributionRepo=repo,
                       releaseTag=release["tag_name"], publishedAt=release["published_at"],
                       windowsVst3Asset=assets[0] if assets else None)
        observed.append(row)
    return {"schemaVersion": 1, "observed": observed}


if __name__ == "__main__":
    output = Path(sys.argv[1] if len(sys.argv) > 1 else "config/factoryfx-upstream-candidates.json")
    try:
        candidate = snapshot()
        rendered = json.dumps(candidate, indent=2, ensure_ascii=False) + "\n"
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(rendered, encoding="utf-8")
        for item in candidate["observed"]:
            print(item["productId"] + ": " + str(item["releaseTag"]) + " / " + str(item["windowsVst3Asset"]))
        print("CANDIDATE ONLY — no redistribution, host certification or factory promotion")
    except (OSError, ValueError, KeyError, urllib.error.URLError) as error:
        print("RELEASE WATCH FAILED: " + str(error), file=sys.stderr)
        sys.exit(1)
