#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Exact release asset manifest; streamed hashes, no extraction or execution.
File-level inventory is not a complete transitive binary dependency SBOM.
"""
import argparse
import hashlib
import json
import re
import stat
import subprocess
import zipfile
from pathlib import Path, PurePosixPath

SHA = re.compile(r"[0-9a-f]{40}")
TAG = re.compile(r"v[0-9]+\.[0-9]+\.[0-9]+-(?:alpha|beta)\.[0-9]+")
HEX = re.compile(r"[0-9a-f]{64}")
BLOCK = 1024 * 1024
MAX_METADATA = 4 * 1024 * 1024
MAX_FILES = 4096
MAX_EXPANDED = 8 * 1024**3

def digest(stream):
    h = hashlib.sha256()
    while chunk := stream.read(BLOCK):
        h.update(chunk)
    return h.hexdigest()

def file_hash(path):
    with path.open("rb") as stream:
        return digest(stream)

def json_file(path):
    if path.stat().st_size > MAX_METADATA:
        raise ValueError("Metadata too large: " + path.name)
    return json.loads(path.read_text(encoding="utf-8-sig"), object_pairs_hook=unique_object)

def layout(platform, tag):
    prefix = f"ArSonMix-{tag}-"
    if platform == "windows":
        binaries = [prefix + "Windows-x64-" + x for x in ("Setup.exe", "Setup.zip", "Portable.zip")]
        return binaries, ["BUILD_PROVENANCE.json", "INSTALLER_PROVENANCE.json",
                          "FACTORYFX_STAGE_REPORT.json"], "SHA256SUMS.txt"
    if platform == "linux":
        return [prefix + "Linux-x86_64.tar.gz"], ["LINUX_PROVENANCE.json"], "SHA256SUMS-Linux.txt"
    if platform == "macos":
        return [prefix + "macOS-universal.dmg"], ["MACOS_PROVENANCE.json"], "SHA256SUMS-macOS.txt"
    raise ValueError("Unsupported platform")

def unique_object(pairs):
    out = {}
    for key, value in pairs:
        if key in out:
            raise ValueError("Duplicate JSON metadata key")
        out[key] = value
    return out

def safe_member_part(part):
    # Windows aliases and reserved names are unsafe even when validation
    # runs on Ubuntu; reject before a user extracts a portable ZIP.
    if (not part or part in (".", "..") or part[-1] in " ." or
            any(ord(c) < 32 or c in '<>:"|?*' for c in part)):
        return False
    stem = part.split(".", 1)[0].rstrip(" ").upper()
    return not (stem in {"CON", "PRN", "AUX", "NUL"} or
                re.fullmatch(r"(?:COM|LPT)[1-9¹²³]", stem))

def zip_inventory(path):
    # Enumerate and hash actual portable ZIP members without extracting them.
    rows, seen, expanded = [], set(), 0
    with zipfile.ZipFile(path) as archive:
        infos = archive.infolist()
        if len(infos) > MAX_FILES:
            raise ValueError("Portable ZIP member limit exceeded")
        for info in infos:
            name = info.filename
            normalized = name.rstrip("/")
            parts = normalized.split("/")
            if (not normalized or name.startswith("/") or "\\" in name or
                    any(not safe_member_part(x) for x in parts) or
                    PurePosixPath(normalized).is_absolute()):
                raise ValueError("Unsafe portable ZIP member")
            key = normalized.casefold()
            if key in seen:
                raise ValueError("Duplicate portable ZIP member")
            seen.add(key)
            if stat.S_ISLNK(info.external_attr >> 16):
                raise ValueError("Portable ZIP symlink rejected")
            if info.is_dir():
                continue
            expanded += info.file_size
            if expanded > MAX_EXPANDED:
                raise ValueError("Portable ZIP expansion limit exceeded")
            with archive.open(info) as stream:
                sha256 = digest(stream)
            rows.append({"path": normalized, "bytes": info.file_size, "sha256": sha256})
    if not rows:
        raise ValueError("Empty portable ZIP")
    # Reject file-as-parent ambiguity, including case-folded aliases.
    files = {row["path"].casefold() for row in rows}
    for name in seen:
        parts = name.split("/")
        if any("/".join(parts[:i]) in files for i in range(1, len(parts))):
            raise ValueError("Portable ZIP file/directory collision")
    return sorted(rows, key=lambda row: row["path"])

def calculate(root, platform, tag, sha, existing=False):
    if not SHA.fullmatch(sha) or not TAG.fullmatch(tag):
        raise ValueError("Invalid release identity")
    root = Path(root)
    binary, metadata, sums = layout(platform, tag)
    manifest = f"RELEASE_TRUST-{platform}.json"
    expected = set(binary + metadata + [sums])
    if existing:
        expected.add(manifest)
    entries = list(root.iterdir())
    if {p.name for p in entries} != expected or any(
            not p.is_file() or p.is_symlink() or p.stat().st_size == 0 for p in entries):
        raise ValueError("Unexpected, missing, empty or non-regular release asset")
    rows = [{"name": name, "bytes": (root / name).stat().st_size,
             "sha256": file_hash(root / name)} for name in sorted(expected - {manifest})]
    hashes = {r["name"]: r["sha256"] for r in rows}
    declared = {}
    if (root / sums).stat().st_size > MAX_METADATA:
        raise ValueError("Checksum file too large")
    for line in (root / sums).read_text(encoding="utf-8-sig").splitlines():
        match = re.fullmatch(r"([0-9a-fA-F]{64})  ([A-Za-z0-9._-]+)", line)
        if not match or match[2] in declared:
            raise ValueError("Malformed or duplicate checksum entry")
        declared[match[2]] = match[1].lower()
    if declared != {name: hashes[name] for name in binary}:
        raise ValueError("Binary checksum mismatch")
    if platform == "windows":
        build, installer = [json_file(root / n) for n in metadata[:2]]
        if build.get("sourceCommit") != sha or installer.get("sourceCommit") != sha:
            raise ValueError("Windows source provenance mismatch")
        if "-beta." in tag and (build.get("releaseTag") != tag or installer.get("appVersion") != tag[1:]):
            raise ValueError("Beta release version provenance mismatch")
        if installer.get("installerSha256") != hashes[binary[0]]:
            raise ValueError("Installer provenance mismatch")
        with zipfile.ZipFile(root / binary[1]) as z:
            if z.namelist() != [binary[0]]:
                raise ValueError("Setup ZIP content mismatch")
            info = z.getinfo(binary[0])
            if (stat.S_ISLNK(info.external_attr >> 16) or
                    info.file_size != (root / binary[0]).stat().st_size):
                raise ValueError("Setup ZIP installer size/type mismatch")
            with z.open(info) as f:
                if digest(f) != hashes[binary[0]]:
                    raise ValueError("Setup ZIP installer mismatch")
        portable = zip_inventory(root / binary[2])
        exe = build.get("executable")
        if not isinstance(exe, str) or not any(
                r["path"] == exe and r["sha256"] == build.get("exeSha256") for r in portable):
            raise ValueError("Portable executable provenance mismatch")
        if installer.get("guiSha256") != build.get("exeSha256"):
            raise ValueError("Installer and portable GUI mismatch")
    else:
        provenance = json_file(root / metadata[0])
        if (provenance.get("sourceCommit") != sha or provenance.get("tag") != tag
                or provenance.get("platform") != platform or provenance.get("asset") != binary[0]
                or provenance.get("assetSha256") != hashes[binary[0]]):
            raise ValueError("OS package provenance mismatch")
        portable = []
    return {"schema": "arsonmix.release-trust.v1", "repository": "masarray/arsonmix",
            "sourceCommit": sha, "tag": tag, "platform": platform,
            "assets": rows, "portableFileInventory": portable,
            "inventoryScope": "actual Windows portable ZIP files only; not a transitive dependency SBOM"}

def verify_uploaded_draft(root, release, tag, sha):
    if (release.get("tag_name") != tag or release.get("target_commitish") != sha
            or release.get("draft") is not True):
        raise ValueError("Draft release identity mismatch")
    assets = release.get("assets")
    if not isinstance(assets, list) or len(assets) != len(list(root.iterdir())):
        raise ValueError("Draft asset count mismatch")
    expected = {p.name: p for p in root.iterdir()}
    for asset in assets:
        path = expected.pop(asset.get("name"), None)
        if (path is None or asset.get("state") != "uploaded"
                or asset.get("size") != path.stat().st_size
                or asset.get("digest") != "sha256:" + file_hash(path)):
            raise ValueError("Draft uploaded asset digest mismatch")
    if expected:
        raise ValueError("Draft missing assets")

def verify_github_draft(root, tag, sha):
    # Query the authenticated list, because the public tag endpoint need not
    # resolve unpublished drafts. No response or credential is logged.
    result = subprocess.run(["gh", "api", "--paginate", "--slurp",
                             "repos/masarray/arsonmix/releases?per_page=100"],
                            check=True, capture_output=True, text=True, timeout=120)
    matches = [r for page in json.loads(result.stdout) for r in page if r.get("tag_name") == tag]
    if len(matches) != 1:
        raise ValueError("Expected exactly one release draft")
    verify_uploaded_draft(root, matches[0], tag, sha)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=["create", "verify", "verify-upload"])
    parser.add_argument("--directory", required=True, type=Path)
    parser.add_argument("--platform", required=True, choices=["windows", "linux", "macos"])
    parser.add_argument("--tag", required=True)
    parser.add_argument("--sha", required=True)
    args = parser.parse_args()
    path = args.directory / f"RELEASE_TRUST-{args.platform}.json"
    data = calculate(args.directory, args.platform, args.tag, args.sha, args.mode != "create")
    if args.mode == "create":
        with path.open("x", encoding="utf-8", newline="\n") as f:
            f.write(json.dumps(data, indent=2, sort_keys=True) + "\n")
    elif json_file(path) != data:
        raise ValueError("Release trust manifest mismatch")
    if args.mode == "verify-upload":
        if args.platform != "windows":
            raise ValueError("Draft verification is Windows publication only")
        verify_github_draft(args.directory, args.tag, args.sha)
    print("RELEASE_TRUST_" + args.mode.upper() + "_PASS", args.platform, len(data["assets"]))

if __name__ == "__main__":
    main()
