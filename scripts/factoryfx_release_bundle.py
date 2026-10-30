#!/usr/bin/env python3
"""Fail-closed ArSonMix Factory VST3 release staging. Python stdlib only.

A reviewed lock is the ONLY authority to ship a binary. Upstream latest-release
discovery cannot change this lock or bypass entitlement/host qualification.
The native JUCE registry independently rehashes every staged resource off RT.
"""
import argparse
import hashlib
import json
import re
import shutil
import stat
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path, PurePosixPath

OWNED = {
    "arson-eq": ("masarray/arson-eq", "ArSonEQ", "ArSonEQ.vst3", "EQ"),
    "arson-verb": ("masarray/arsonverb", "ArSonVerb", "ArSonVerb.vst3", "REVERB"),
    "arsonkupik-enhancer": ("masarray/vst-enhancer", "ArSonKuPik", "ArSonKuPik.vst3", "ENHANCER"),
    "arson-comp": ("masarray/arson-comp", "ArSonComp", "ArSonComp.vst3", "COMP"),
    "arson-harmony": ("masarray/arson-harmony", "ArSonHarmony", "ArSonHarmony.vst3", "HARMONY"),
    "arson-color": ("masarray/arson-color", "ArSonColor", "ArSonColor.vst3", "COLOR"),
}
HEX = lambda s, n: isinstance(s, str) and re.fullmatch("[0-9a-fA-F]{" + str(n) + "}", s) is not None
TOKEN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,119}$")
MAX_UNPACKED = 200 * 1024 * 1024
MAX_FILES = 256


def fail(message):
    raise ValueError(message)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def read_lock(path):
    obj = json.loads(Path(path).read_text(encoding="utf-8"))
    if not isinstance(obj, dict) or obj.get("schemaVersion") != 1 or set(obj) != {"schemaVersion", "plugins"}:
        fail("invalid FactoryFX reviewed lock schema")
    plugins = obj["plugins"]
    if not isinstance(plugins, list) or len(plugins) > 6:
        fail("FactoryFX lock must contain at most six reviewed effects")
    seen = set()
    for p in plugins:
        required = {"productId", "version", "releaseTag", "assetName", "artifactSha256",
                    "sourceRepo", "sourceCommit", "vst3ClassId", "hostUniqueId",
                    "licenseReview", "entitlementPolicy", "hostQualification"}
        if not isinstance(p, dict) or set(p) != required:
            fail("FactoryFX lock requires exactly the reviewed fields")
        pid = p["productId"]
        if pid not in OWNED or pid in seen:
            fail("unknown/duplicate factory product ID")
        seen.add(pid)
        if p["sourceRepo"] != OWNED[pid][0] and not (
            pid == "arsonkupik-enhancer" and p["sourceRepo"] == "masarray/askp-vst"
        ):
            fail(pid + ": unexpected DSP source repository")
        if not all(isinstance(p[k], str) and TOKEN.fullmatch(p[k])
                   for k in ("version", "releaseTag", "assetName")):
            fail(pid + ": unsafe release identity")
        if not p["assetName"].lower().endswith(".zip") or "windows" not in p["assetName"].lower() or "vst3" not in p["assetName"].lower():
            fail(pid + ": only Windows x64 VST3 ZIP distributions allowed")
        if not (HEX(p["artifactSha256"], 64) and HEX(p["sourceCommit"], 40)
                and HEX(p["vst3ClassId"], 32)):
            fail(pid + ": unpinned artifact/source/class identity")
        if type(p["hostUniqueId"]) is not int or not (-2147483648 <= p["hostUniqueId"] <= 2147483647):
            fail(pid + ": exact JUCE PluginDescription UID required")
        if p["licenseReview"] != "approved" or p["hostQualification"] != "passed":
            fail(pid + ": licence/host acceptance is not approved")
        if p["entitlementPolicy"] not in ("unrestricted", "separate-license", "factory-entitlement"):
            fail(pid + ": invalid entitlement policy")
    return plugins


def download_release(p, output):
    # Public binary distribution only. No private source checkout, secrets or
    # floating 'latest' URL. SHA lock prevents a mutable same-tag asset swap.
    repo = "masarray/vst-enhancer" if p["productId"] == "arsonkupik-enhancer" else p["sourceRepo"]
    url = "https://github.com/" + repo + "/releases/download/" + p["releaseTag"] + "/" + p["assetName"]
    request = urllib.request.Request(url, headers={"User-Agent": "ArSonMix-FactoryFx-Packager/1"})
    with urllib.request.urlopen(request, timeout=90) as source, open(output, "wb") as target:
        if source.geturl().split("/")[2] not in ("github.com", "release-assets.githubusercontent.com", "objects.githubusercontent.com"):
            fail("untrusted release redirect")
        while True:
            block = source.read(1024 * 1024)
            if not block:
                break
            target.write(block)
            if target.tell() > MAX_UNPACKED:
                fail("FactoryFX archive too large")
    if sha256(output) != p["artifactSha256"].lower():
        fail(p["productId"] + ": release ZIP digest mismatch")


def zip_members(z, bundle):
    chosen = []
    seen = set()
    total = 0
    for info in z.infolist():
        # Ignore zip folder markers and unrelated top-level build metadata.
        if info.is_dir():
            continue
        pp = PurePosixPath(info.filename)
        parts = pp.parts
        if not parts or any(x in ("", ".", "..") for x in parts) or info.filename.startswith("/") or "\\" in info.filename:
            fail("unsafe ZIP path")
        mode = info.external_attr >> 16
        if stat.S_ISLNK(mode):
            fail("symlink inside VST3 archive")
        positions = [i for i, x in enumerate(parts) if x == bundle]
        if len(positions) != 1:
            # Standalone EXEs and foreign plugins must never enter the factory.
            continue
        i = positions[0]
        if i > 1 or (i == 1 and parts[0] not in ("VST3", "vst3")):
            fail("unexpected VST3 archive root")
        sub = parts[i + 1:]
        if not sub:
            fail("Windows JUCE VST3 must be a bundle directory")
        rel = "/".join(sub)
        lower = rel.lower()
        if lower in seen:
            fail("duplicate / case-alias VST3 resource")
        seen.add(lower)
        if not all(TOKEN.fullmatch(v) for v in sub):
            fail("unsafe VST3 resource name")
        total += info.file_size
        if len(seen) > MAX_FILES or total > MAX_UNPACKED or info.file_size > MAX_UNPACKED:
            fail("VST3 unpacked resource budget exceeded")
        chosen.append((info, rel))
    if not chosen or not any(x[1].lower().endswith(".vst3") and "x86_64-win/" in x[1].lower() for x in chosen):
        fail("expected Windows x64 VST3 module is absent")
    return chosen


def stage(plugins, output, notices):
    factory = output / "FactoryFX"
    if factory.exists():
        fail("staging must start with clean FactoryFX destination")
    if not plugins:
        print("FACTORYFX: no approved plugins; app-only engineering package unchanged")
        return
    with tempfile.TemporaryDirectory(prefix="arsonmix-factoryfx-") as work:
        temp = Path(work)
        rows = []
        for p in plugins:
            pid = p["productId"]
            _, display, bundle, category = OWNED[pid]
            notice = notices / (pid + ".txt")
            if not notice.is_file() or not notice.read_text(encoding="utf-8").strip():
                fail(pid + ": reviewed redistribution/EULA notice missing")
            archive = temp / (pid + ".zip")
            download_release(p, archive)
            target = factory / display / bundle
            target.mkdir(parents=True, exist_ok=False)
            with zipfile.ZipFile(archive) as z:
                for info, relative in zip_members(z, bundle):
                    path = target.joinpath(*relative.split("/"))
                    path.parent.mkdir(parents=True, exist_ok=True)
                    with z.open(info) as src, open(path, "wb") as dest:
                        shutil.copyfileobj(src, dest, 1024 * 1024)
            files = [{"path": f.relative_to(target).as_posix(), "sha256": sha256(f)}
                     for f in sorted(target.rglob("*")) if f.is_file()]
            notice_to = factory / "licenses" / (pid + ".txt")
            notice_to.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(notice, notice_to)
            rows.append({
                "productId": pid, "displayName": display, "category": category,
                "pluginVersion": p["version"], "sourceRepo": p["sourceRepo"],
                "sourceCommit": p["sourceCommit"], "artifactSha256": p["artifactSha256"].lower(),
                "bundleRelativePath": "FactoryFX/" + display + "/" + bundle,
                "vst3ClassId": p["vst3ClassId"].upper(), "hostUniqueId": p["hostUniqueId"],
                "licenseReview": "approved", "entitlementPolicy": p["entitlementPolicy"],
                "notices": ["FactoryFX/licenses/" + pid + ".txt"], "files": files,
            })
        manifest = {
            "schemaVersion": 1, "platform": "windows-x64", "hostFormat": "VST3",
            "distribution": "production-six" if len(rows) == 6 else "engineering-partial",
            "plugins": rows,
        }
        (factory / "factoryfx-manifest.json").write_text(
            json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        verify(plugins, output, notices)
        print("FACTORYFX staged and verified: " + ", ".join(x["productId"] for x in rows))


def verify(plugins, output, notices):
    factory = output / "FactoryFX"
    if not plugins:
        if factory.exists():
            fail("unapproved FactoryFX found in app-only distribution")
        return
    manifest_path = factory / "factoryfx-manifest.json"
    if not manifest_path.is_file():
        fail("missing FactoryFX manifest")
    m = json.loads(manifest_path.read_text(encoding="utf-8"))
    if m.get("distribution") != ("production-six" if len(plugins) == 6 else "engineering-partial"):
        fail("FactoryFX distribution level mismatch")
    rows = m.get("plugins")
    if not isinstance(rows, list) or len(rows) != len(plugins):
        fail("FactoryFX entries mismatch reviewed lock")
    allowed_files = {"factoryfx-manifest.json"}
    for p, row in zip(plugins, rows):
        pid = p["productId"]
        _, display, bundle, category = OWNED[pid]
        if (row.get("productId"), row.get("pluginVersion"), row.get("category"),
            row.get("sourceRepo"), row.get("sourceCommit"), row.get("vst3ClassId"),
            row.get("hostUniqueId"), row.get("artifactSha256"),
            row.get("licenseReview"), row.get("entitlementPolicy")) != (
            pid, p["version"], category, p["sourceRepo"], p["sourceCommit"],
            p["vst3ClassId"].upper(), p["hostUniqueId"], p["artifactSha256"].lower(),
            "approved", p["entitlementPolicy"]):
            fail(pid + ": manifest lock divergence")
        path = factory / display / bundle
        if row.get("bundleRelativePath") != "FactoryFX/" + display + "/" + bundle or not path.is_dir():
            fail(pid + ": bundle path missing")
        files = row.get("files")
        if not isinstance(files, list) or not files or len(files) > MAX_FILES:
            fail(pid + ": resource inventory invalid")
        if row.get("notices") != ["FactoryFX/licenses/" + pid + ".txt"]:
            fail(pid + ": licence notice not attested")
        notice = factory / "licenses" / (pid + ".txt")
        if not notice.is_file() or notice.read_bytes() != (notices / (pid + ".txt")).read_bytes():
            fail(pid + ": licence notice modified")
        allowed_files.add(notice.relative_to(factory).as_posix())
        for entry in files:
            relative = entry["path"]
            comps = relative.split("/")
            if not all(TOKEN.fullmatch(c) for c in comps):
                fail(pid + ": unsafe file inventory")
            file = path.joinpath(*comps)
            if file.is_symlink() or not file.is_file() or sha256(file) != entry["sha256"]:
                fail(pid + ": missing or modified resource " + relative)
            allowed_files.add(file.relative_to(factory).as_posix())
    actual = {x.relative_to(factory).as_posix() for x in factory.rglob("*") if x.is_file()}
    if actual != allowed_files:
        fail("FactoryFX untracked/extra resource or user content found")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=["stage", "verify"])
    parser.add_argument("--lock", default="config/factoryfx-approved-releases.json")
    parser.add_argument("--notices", default="config/factoryfx-notices")
    parser.add_argument("--output", default="dist")
    parser.add_argument("--require", choices=["none", "factory", "six"], default="none")
    args = parser.parse_args()
    plugins = read_lock(args.lock)
    if args.require == "factory" and not plugins:
        fail("Factory FX required but no approved release")
    if args.require == "six" and len(plugins) != 6:
        fail("Production bundle requires all six approved effects")
    if args.action == "stage":
        stage(plugins, Path(args.output), Path(args.notices))
    else:
        verify(plugins, Path(args.output), Path(args.notices))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, urllib.error.URLError, zipfile.BadZipFile, KeyError) as exc:
        print("FACTORYFX REJECTED: " + str(exc), file=sys.stderr)
        sys.exit(1)
