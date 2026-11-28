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
    # Prefer the verified installer ZIP, with Setup EXE for older releases.
    "windows": ("Windows-x64-Setup.zip", "Windows-x64-Setup.exe"),
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



def hydrate_landing(html_source, verified):
    """Render verified download cards into deployed HTML, not the editable seed.

    The browser may later choose newer validated assets. This generated
    snapshot keeps all three OS download links usable without JavaScript and
    prevents a published package from being described as "NOT PUBLISHED".
    """
    from html import escape

    if not isinstance(verified, dict) or verified.get("schemaVersion") != 1:
        raise ValueError("Invalid verified catalog for landing")
    platforms = verified.get("platforms")
    if not isinstance(platforms, dict) or set(platforms) != set(SUFFIXES):
        raise ValueError("Incomplete platform catalog")
    rendered = html_source
    available = []
    for platform, suffixes in SUFFIXES.items():
        item = platforms[platform]
        marker = f'<article class="os-card" data-platform="{platform}">'
        if rendered.count(marker) != 1:
            raise ValueError("Landing requires exactly one source card: " + platform)
        left = rendered.index(marker)
        right = rendered.find("</article>", left)
        if right < 0:
            raise ValueError("Unterminated landing card: " + platform)
        right += len("</article>")
        card = rendered[left:right]
        if item is None:
            continue  # Unknown remains CHECKING, never a fabricated download.
        if not isinstance(item, dict):
            raise ValueError("Malformed verified asset: " + platform)
        tag, name, url = item.get("tag"), item.get("name"), item.get("url")
        size, sha = item.get("size"), item.get("sha256")
        published = item.get("publishedAt")
        if (not isinstance(tag, str) or not TAG.fullmatch(tag) or
                not isinstance(name, str) or
                name not in (f"ArSonMix-{tag}-{suffix}" for suffix in suffixes) or
                url != f"https://github.com/{REPO}/releases/download/{tag}/{name}" or
                type(size) is not int or size < 2097152 or
                not isinstance(sha, str) or not re.fullmatch(r"[a-f0-9]{64}", sha) or
                type(item.get("prerelease")) is not bool or
                not isinstance(published, str) or
                not re.fullmatch(r"\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ", published)):
            raise ValueError("Unverified download cannot be embedded: " + platform)
        attrib = {
            "release-tag": tag, "release-name": name,
            "release-size": str(size), "release-sha256": sha,
            "release-prerelease": str(item["prerelease"]).lower(),
            "release-published-at": published,
        }
        additional = "".join(f' data-{key}="{escape(value, quote=True)}"'
                             for key, value in attrib.items())
        card = card.replace('class="os-card"', 'class="os-card is-available"', 1)
        card = card.replace(f'data-platform="{platform}"',
                            f'data-platform="{platform}"{additional}', 1)
        status = "EARLY ACCESS" if item["prerelease"] else "AVAILABLE"
        label = ("Download installer ZIP" if platform == "windows" and
                 name.endswith("-Setup.zip") else
                 "Download Windows installer" if platform == "windows" else
                 "Download for " + {"linux": "Linux", "macos": "macOS"}[platform])
        note = (tag + " · " + format(size / 1048576, ".1f") + " MB" +
                ((" · Public Beta" if "-beta." in tag else " · Alpha preview" if "-alpha." in tag else " · Prerelease") if item["prerelease"] else "") +
                (" · Extract ZIP, run Setup" if label == "Download installer ZIP" else ""))
        changes = [
            ('data-release-state>CHECKING</span>',
             'data-release-state>' + status + '</span>'),
            ('data-release-link href="#download" aria-disabled="true"',
             'data-release-link href="' + escape(url, quote=True) +
             '" aria-label="Download verified ' + escape(name, quote=True) + '"'),
            ('data-link-label>Checking package…</span>',
             'data-link-label>' + escape(label) + '</span>'),
            ('data-release-note>Checking the verified release catalog.</div>',
             'data-release-note>' + escape(note) + '</div>'),
        ]
        for old, new in changes:
            if card.count(old) != 1:
                raise ValueError("Landing template changed at " + platform + ": " + old)
            card = card.replace(old, new, 1)
        rendered = rendered[:left] + card + rendered[right:]
        available.append((platform, tag))
    if available:
        old = 'id="release-summary" class="release-summary" role="status" aria-live="polite">Loading verified releases…'
        names = {"windows": "Windows", "linux": "Linux", "macos": "macOS"}
        msg = "Ready to download · " + " · ".join(
            names[platform] + " " + tag for platform, tag in available)
        if rendered.count(old) != 1:
            raise ValueError("Landing release summary marker changed")
        rendered = rendered.replace(old, old.split(">")[0] + ">" + escape(msg), 1)
    return rendered


def hydrate_landing_file(path, verified):
    landing = Path(path)
    landing.write_text(hydrate_landing(landing.read_text(encoding="utf-8"), verified),
                       encoding="utf-8")



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
    parser.add_argument("--landing", help="Optional built landing index.html to hydrate using this same verified catalog")
    args = parser.parse_args()
    data = json.loads(Path(args.fixture).read_text("utf-8")) if args.fixture else download_releases()
    approved = catalog(data)
    if not any(approved["platforms"].values()):
        raise ValueError("No verified published native assets; preserve the last deployed Pages site")
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(approved, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    if args.landing:
        hydrate_landing_file(args.landing, approved)
    print("ARSONMIX_RELEASE_CATALOG_VERIFIED " + " ".join(
        key + "=" + (v["tag"] if v else "unavailable") for key, v in approved["platforms"].items()))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, urllib.error.URLError) as exc:
        sys.exit("Release catalog build failed; preserve last deployed landing: " + str(exc))
