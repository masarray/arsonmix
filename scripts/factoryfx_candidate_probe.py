#!/usr/bin/env python3
"""Pinned sibling candidate intake; never writes an approved lock or dist.
Optional Windows probe uses an explicitly built JUCE test executable, isolated
one supplier per process with timeout. Metadata only on other platforms.
"""
import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
import tempfile
import zipfile
import urllib.request
from pathlib import Path
import factoryfx_release_bundle as bundle

ROOT = Path(__file__).resolve().parents[1]

def read_candidates(path):
    obj = json.loads(Path(path).read_text(encoding="utf-8"))
    rows = obj["candidates"]
    if obj["schemaVersion"] != 1 or len(rows) != 4 or {p["productId"] for p in rows} != bundle.FOUR_IDS:
        raise ValueError("candidate inventory must contain exactly the four sibling products")
    for p in rows:
        pid = p["productId"]
        expected = bundle.OWNED[pid][0]
        source = "masarray/askp-vst" if pid == "arsonkupik-enhancer" else expected
        if p["distributionRepo"] != expected or p["sourceRepo"] != source:
            raise ValueError("unexpected candidate supplier")
        if not bundle.HEX(p["artifactSha256"], 64) or not all(bundle.HEX(p[k], 40) for k in ("sourceCommit", "distributionCommit")):
            raise ValueError("candidate digest/commit not pinned")
        if not all(bundle.TOKEN.fullmatch(p[k]) for k in ("releaseTag", "assetName")):
            raise ValueError("unsafe candidate URL token")
        if not all(s in p["assetName"].lower() for s in ("windows", "x64", "vst3")) or not p["assetName"].endswith(".zip"):
            raise ValueError("candidate must be Windows x64 VST3 ZIP")
        if type(p["assetBytes"]) is not int or not 0 < p["assetBytes"] <= bundle.MAX_UNPACKED:
            raise ValueError("invalid candidate byte count")
        for sidecar in p.get("upstreamEvidence", []):
            if (set(sidecar) != {"name", "sha256", "bytes", "kind"} or
                    not bundle.TOKEN.fullmatch(sidecar["name"]) or not bundle.HEX(sidecar["sha256"], 64) or
                    type(sidecar["bytes"]) is not int or not 0 < sidecar["bytes"] <= 256 * 1024 or
                    sidecar["kind"] not in ("checksums", "buildinfo")):
                raise ValueError("invalid upstream sidecar pin")
    return rows

def check_pe(path):
    with path.open("rb") as f:
        head = f.read(64)
        if len(head) != 64 or head[:2] != b"MZ":
            raise ValueError("not a PE binary")
        offset = struct.unpack_from("<I", head, 60)[0]
        if offset < 64 or offset > path.stat().st_size - 26:
            raise ValueError("invalid PE header offset")
        f.seek(offset)
        pe = f.read(26)
        if pe[:4] != b"PE\0\0" or struct.unpack_from("<H", pe, 4)[0] != 0x8664 or struct.unpack_from("<H", pe, 24)[0] != 0x20b:
            raise ValueError("not x64 PE32+")

def read_module_info(path):
    # Steinberg/JUCE moduleinfo is JSON-like and some SDK writers emit
    # trailing commas. Remove only trailing commas outside string literals.
    # Preserve the original bytes for hashing and never rewrite the binary.
    if path.stat().st_size > 1024 * 1024:
        raise ValueError("moduleinfo exceeds evidence budget")
    raw = path.read_text(encoding="utf-8-sig")
    try:
        return json.loads(raw)
    except json.JSONDecodeError:
        normal = re.sub(r'("(?:\\.|[^"\\])*")|,(?=\s*[}\]])',
                        lambda m: m.group(1) or "", raw)
        return json.loads(normal)


def verify_sidecar(p, spec, raw):
    if len(raw) != spec["bytes"] or hashlib.sha256(raw).hexdigest() != spec["sha256"]:
        raise ValueError("upstream evidence digest/size mismatch")
    text = raw.decode("utf-8-sig")
    result = {"name": spec["name"], "sha256": spec["sha256"], "kind": spec["kind"]}
    if spec["kind"] == "checksums":
        matching = []
        for line in text.splitlines():
            m = re.fullmatch(r"([0-9a-fA-F]{64})\s+\*?(.+)", line.strip())
            if m and m.group(2).removeprefix("./") == p["assetName"]:
                matching.append(m.group(1).lower())
        if matching != [p["artifactSha256"].lower()]:
            raise ValueError("upstream checksum inventory does not uniquely attest exact VST3 ZIP")
        result["archiveChecksumMatched"] = True
    else:
        info = json.loads(text)
        if not isinstance(info, dict):
            raise ValueError("invalid upstream buildinfo")
        # Report the exact pinned source claim from structured published metadata.
        # Do not interpret a checksum sidecar or this claim as independent build attestation.
        mentions = set(re.findall(r"(?i)\b[0-9a-f]{40}\b", text))
        result["sourceCommitMentioned"] = p["sourceCommit"].lower() in {v.lower() for v in mentions}
        if not result["sourceCommitMentioned"]:
            raise ValueError("upstream buildinfo does not identify pinned source commit")
    return result


def fetch_sidecars(p):
    results = []
    for spec in p.get("upstreamEvidence", []):
        url = "https://github.com/" + p["distributionRepo"] + "/releases/download/" + p["releaseTag"] + "/" + spec["name"]
        request = urllib.request.Request(url, headers={"User-Agent": "ArSonMix-FactoryFx-Evidence/1"})
        with urllib.request.urlopen(request, timeout=30) as response:
            if response.geturl().split("/")[2] not in ("github.com", "release-assets.githubusercontent.com", "objects.githubusercontent.com"):
                raise ValueError("untrusted evidence redirect")
            raw = response.read(spec["bytes"] + 1)
        results.append(verify_sidecar(p, spec, raw))
    return results


def inspect_candidate(p, work, host=None):
    pid = p["productId"]
    name = bundle.OWNED[pid][2]
    archive = work / (pid + ".zip")
    bundle.download_release(p, archive)
    if archive.stat().st_size != p["assetBytes"]:
        raise ValueError("release asset byte count mismatch")
    target = work / name
    target.mkdir()
    with zipfile.ZipFile(archive) as z:
        members = bundle.zip_members(z, name)
        legacy_flat = any(info.filename.replace("\\", "/").split("/")[-1] == name
                          and len(info.filename.replace("\\", "/").split("/")) <= 2
                          for info, _ in members)
        for info, relative in members:
            f = target.joinpath(*relative.split("/"))
            f.parent.mkdir(parents=True, exist_ok=True)
            with z.open(info) as src, f.open("wb") as dst:
                shutil.copyfileobj(src, dst, 1024 * 1024)
        # Root build/licence metadata is evidence only, not copied to dist.
        evidence = []
        commit_mentions = set()
        for info in z.infolist():
            if not info.is_dir() and info.file_size <= 256 * 1024 and any(
                    x in info.filename.lower() for x in ("build", "license", "licence", "eula", "notice", "provenance")):
                if len(evidence) >= 64:
                    raise ValueError("too many candidate provenance/notice files")
                raw = z.read(info)
                evidence.append({"path": info.filename, "sha256": hashlib.sha256(raw).hexdigest()})
                if any(x in info.filename.lower() for x in ("build", "provenance")):
                    commit_mentions.update(re.findall(r"(?i)\b[0-9a-f]{40}\b", raw.decode("utf-8", errors="replace")))
    binary = target / "Contents" / "x86_64-win" / name
    check_pe(binary)
    classes = []
    metadata = target / "Contents" / "Resources" / "moduleinfo.json"
    if metadata.is_file():
        obj = read_module_info(metadata)
        classes = [{"cid": c.get("CID"), "name": c.get("Name")} for c in obj.get("Classes", [])
                   if c.get("Category") == "Audio Module Class" and bundle.HEX(c.get("CID"), 32)]
    result = {"productId": pid, "artifactSha256": bundle.sha256(archive), "archiveStatus": "verified-x64",
              "sourceCommitClaim": p["sourceCommit"], "moduleSha256": bundle.sha256(binary),
              "sourceLayout": "legacy-flat-normalized" if legacy_flat else "bundle",
              "moduleInfoAudioClasses": classes, "archiveEvidence": evidence,
              "archiveBuildCommitMentions": sorted(commit_mentions),
              "sourceCommitMentioned": p["sourceCommit"].lower() in {v.lower() for v in commit_mentions},
              "hostStatus": "not-run",
              "approval": "not-approved"}
    result["upstreamEvidence"] = fetch_sidecars(p)
    if host:
        receipt = work / (pid + "-host.json")
        subprocess.run([str(host), str(target), str(receipt)], check=True, timeout=90)
        result["hostReceipt"] = json.loads(receipt.read_text(encoding="utf-8"))
        actual_cid = result["hostReceipt"].get("runtimeClassId")
        if not bundle.HEX(actual_cid, 32):
            raise ValueError("actual runtime VST3 class ID missing")
        if classes and not any(c["cid"].upper() == actual_cid.upper() for c in classes):
            raise ValueError("runtime VST3 class differs from moduleinfo")
        result["hostStatus"] = "native-probe-passed-not-physical-qualification"
    return result

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pins", default=ROOT / "config/factoryfx-qualification-candidates.json")
    ap.add_argument("--output", required=True, type=Path)
    ap.add_argument("--host", type=Path, help="Windows ARMixFactoryFxHostProbe executable")
    args = ap.parse_args()
    rows = read_candidates(args.pins)
    host = args.host.resolve(strict=True) if args.host else None
    results = []
    with tempfile.TemporaryDirectory(prefix="armix-factory-candidates-") as root:
        for p in rows:
            work = Path(root) / p["productId"]
            work.mkdir()
            try:
                results.append(inspect_candidate(p, work, host))
            except (ValueError, OSError, subprocess.SubprocessError, zipfile.BadZipFile, KeyError) as e:
                results.append({"productId": p["productId"], "artifactSha256": p["artifactSha256"],
                                "archiveStatus": "failed", "approval": "not-approved",
                                "error": str(e)})
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({"schemaVersion": 1, "candidates": results}, indent=2) + "\n", encoding="utf-8")
    for row in results:
        # Visible bounded evidence, no credentials or arbitrary remote text.
        print(json.dumps({k: row[k] for k in ("productId", "archiveStatus", "error", "moduleInfoAudioClasses", "sourceCommitMentioned", "archiveBuildCommitMentions", "sourceLayout", "upstreamEvidence") if k in row}, ensure_ascii=True))
    return 1 if any(r["archiveStatus"] == "failed" for r in results) else 0

if __name__ == "__main__":
    raise SystemExit(main())
