#!/usr/bin/env python3
"""Verified optional ArSonMix Factory VST3 release staging. Python stdlib only.

A reviewed lock is the ONLY authority to ship a binary. Optional per-plugin
failure never blocks the mixer, but NEVER bypasses entitlement/host approval.
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
from pathlib import Path

OWNED = {
    "arson-eq": ("masarray/arson-eq", "ArSonEQ", "ArSonEQ.vst3", "EQ"),
    "arson-verb": ("masarray/arsonverb", "ArSonVerb", "ArSonVerb.vst3", "REVERB"),
    "arsonkupik-enhancer": ("masarray/vst-enhancer", "ArSonKuPik", "ArSonKuPik.vst3", "ENHANCER"),
    "arson-comp": ("masarray/arson-comp", "ArSonComp", "ArSonComp.vst3", "COMP"),
    "arson-harmony": ("masarray/arson-harmony", "ArSonHarmony", "ArSonHarmony.vst3", "HARMONY"),
    "arson-color": ("masarray/arson-color", "ArSonColor", "ArSonColor.vst3", "COLOR"),
}
FOUR_IDS = frozenset(("arson-eq", "arson-comp", "arson-verb", "arsonkupik-enhancer"))


def standard_ready(plugins):
    return FOUR_IDS.issubset({p["productId"] for p in plugins})


def distribution_for(plugins):
    ids = [p["productId"] for p in plugins]
    if len(ids) == 6 and set(ids) == set(OWNED):
        return "production-six"
    if len(ids) == 4 and set(ids) == FOUR_IDS:
        return "production-four"
    return "engineering-partial"


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
        expected_source = "masarray/askp-vst" if pid == "arsonkupik-enhancer" else OWNED[pid][0]
        if p["sourceRepo"] != expected_source:
            fail(pid + ": unexpected DSP source repository")
        if not all(isinstance(p[k], str) and TOKEN.fullmatch(p[k])
                   for k in ("version", "releaseTag", "assetName")):
            fail(pid + ": unsafe release identity")
        if not p["assetName"].lower().endswith(".zip") or "windows" not in p["assetName"].lower() or "x64" not in p["assetName"].lower() or "vst3" not in p["assetName"].lower():
            fail(pid + ": only Windows x64 VST3 ZIP distributions allowed")
        if not (HEX(p["artifactSha256"], 64) and HEX(p["sourceCommit"], 40)
                and HEX(p["vst3ClassId"], 32)):
            fail(pid + ": unpinned artifact/source/class identity")
        if type(p["hostUniqueId"]) is not int or p["hostUniqueId"] == 0 or not (-2147483648 <= p["hostUniqueId"] <= 2147483647):
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
    selected_root = None
    for info in z.infolist():
        # Ignore zip folder markers and unrelated top-level build metadata.
        if info.is_dir():
            continue
        # Older Windows ZIP producers use backslashes. Normalize ONLY
        # separators, never resolve path traversal or extract raw names.
        # Reject absolute, drive/ADS, UNC and dot/empty components before
        # using a relative resource path on any host.
        raw = info.filename.replace("\\", "/")
        parts = raw.split("/")
        if any(x in ("", ".", "..") or ":" in x or "\x00" in x for x in parts):
            fail("unsafe ZIP path: " + repr(info.filename[:160]))
        mode = info.external_attr >> 16
        if stat.S_ISLNK(mode):
            fail("symlink inside VST3 archive")
        positions = [i for i, x in enumerate(parts[:2]) if x == bundle]
        if len(positions) != 1:
            # Standalone EXEs and foreign plugins must never enter the factory.
            continue
        i = positions[0]
        # Supplier ZIPs may put the exact named bundle under one
        # versioned wrapper directory. The wrapper is never extracted.
        # Require a single root for ALL chosen resources; no merging two
        # different copies of the module, even with disjoint filenames.
        if i == 1 and (not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._ -]{0,119}", parts[0])
                       or parts[0].endswith((".", " ")) or parts[0].lower().endswith(".vst3")):
            fail("unexpected VST3 archive root: " + repr(parts[0][:120]))
        root = tuple(parts[:i])
        if selected_root is not None and root != selected_root:
            fail("multiple VST3 archive roots")
        selected_root = root
        sub = parts[i + 1:]
        if not sub:
            fail("Windows JUCE VST3 must be a bundle directory")
        rel = "/".join(sub)
        lower = rel.lower()
        if lower in seen:
            fail("duplicate / case-alias VST3 resource")
        seen.add(lower)
        if not all(TOKEN.fullmatch(v) and not v.endswith(".") and
                   v.split(".")[0].upper() not in {"CON", "PRN", "AUX", "NUL", *("COM"+str(n) for n in range(1,10)), *("LPT"+str(n) for n in range(1,10))}
                   for v in sub):
            fail("unsafe VST3 resource name")
        total += info.file_size
        if len(seen) > MAX_FILES or total > MAX_UNPACKED or info.file_size > MAX_UNPACKED:
            fail("VST3 unpacked resource budget exceeded")
        chosen.append((info, rel))
    if not chosen or not any(x[1].lower().endswith(".vst3") and "x86_64-win/" in x[1].lower() for x in chosen):
        fail("expected Windows x64 VST3 module is absent")
    return chosen


def stage(plugins, output, notices, best_effort=False):
    """Build the WHOLE factory off to the side; publish only after full verify.

    A partial download, invalid ZIP, bad notice or failed integrity check must
    leave the existing installed/package FactoryFX untouched. Temporary files
    are on the same filesystem so the final directory rename is atomic.
    """
    # Existing reviewed Windows workflow invokes --best-effort. Once the
    # four-product lock is complete, a supplier failure is a distribution
    # failure, not permission to silently downgrade a Standard package.
    standard = standard_ready(plugins)
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    destination = output / "FactoryFX"
    if destination.exists() or destination.is_symlink():
        fail("staging must start with clean FactoryFX destination")
    if not plugins and not best_effort:
        print("FACTORYFX: no approved plugins; app-only engineering package unchanged")
        return
    with tempfile.TemporaryDirectory(prefix=".factoryfx-incoming-", dir=output) as work:
        temp = Path(work)
        staging = temp / "staged"
        staging.mkdir()
        factory = staging / "FactoryFX"
        rows = []
        omitted = []
        for p in plugins:
            pid = p["productId"]
            try:
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
            except (ValueError, OSError, urllib.error.URLError, zipfile.BadZipFile) as error:
                if not best_effort or standard:
                    raise
                # A failed supplier cannot leave a broken module in the final bundle.
                shutil.rmtree(factory / OWNED[pid][1], ignore_errors=True)
                (factory / "licenses" / (pid + ".txt")).unlink(missing_ok=True)
                omitted.append({"productId": pid, "reason": type(error).__name__})
                print("FACTORYFX OPTIONAL SKIPPED: " + pid + " (" + type(error).__name__ + ")")
        accepted_ids = {row["productId"] for row in rows}
        accepted = [entry for entry in plugins if entry["productId"] in accepted_ids]
        if rows:
            manifest = {
                "schemaVersion": 1, "platform": "windows-x64", "hostFormat": "VST3",
                "distribution": distribution_for(accepted), "plugins": rows,
            }
            (factory / "factoryfx-manifest.json").write_text(
                json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
            # Verify exact optional subset BEFORE publishing the completed bundle.
            verify(accepted, staging, notices)
            if destination.exists() or destination.is_symlink():
                fail("concurrent FactoryFX staging detected")
            factory.rename(destination)
    if best_effort:
        report = {
            "schemaVersion": 1, "mode": "standard-four" if standard else "best-effort",
            "included": [{"productId": row["productId"],
                          "artifactSha256": row["artifactSha256"]} for row in rows],
            "omitted": omitted,
        }
        (output / "FACTORYFX_STAGE_REPORT.json").write_text(
            json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("FACTORYFX staged and verified: " +
          (", ".join(x["productId"] for x in rows) if rows else "no optional VST3; mixer unchanged"))


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
    if m.get("distribution") != distribution_for(plugins):
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
    # Never follow or tolerate symlinks, including directory junctions.
    for item in factory.rglob("*"):
        if item.is_symlink() or not (item.is_dir() or item.is_file()):
            fail("FactoryFX contains symlink or special filesystem entry")
    actual = {x.relative_to(factory).as_posix() for x in factory.rglob("*") if x.is_file()}
    if actual != allowed_files:
        fail("FactoryFX untracked/extra resource or user content found")


def read_best_effort_report(plugins, output):
    """Independently constrain a partial staged set to the immutable reviewed lock."""
    path = Path(output) / "FACTORYFX_STAGE_REPORT.json"
    report = json.loads(path.read_text(encoding="utf-8"))
    if (not isinstance(report, dict) or set(report) !=
            {"schemaVersion", "mode", "included", "omitted"} or
            report["schemaVersion"] != 1 or
            report["mode"] != ("standard-four" if standard_ready(plugins) else "best-effort") or
            not isinstance(report["included"], list) or
            not isinstance(report["omitted"], list)):
        fail("invalid optional FactoryFX stage report")
    locked = {p["productId"]: p for p in plugins}
    accepted_ids = []
    for row in report["included"]:
        if (not isinstance(row, dict) or set(row) !=
                {"productId", "artifactSha256"} or
                row["productId"] not in locked or
                row["artifactSha256"] != locked[row["productId"]]["artifactSha256"].lower()):
            fail("unreviewed FactoryFX stage report inclusion")
        accepted_ids.append(row["productId"])
    omitted_ids = []
    for row in report["omitted"]:
        if (not isinstance(row, dict) or set(row) != {"productId", "reason"} or
                row["productId"] not in locked or
                not isinstance(row["reason"], str) or
                not re.fullmatch(r"[A-Za-z]{3,40}", row["reason"])):
            fail("invalid optional FactoryFX omission")
        omitted_ids.append(row["productId"])
    expected_ids = [p["productId"] for p in plugins]
    if (accepted_ids != [pid for pid in expected_ids if pid in accepted_ids] or
            omitted_ids != [pid for pid in expected_ids if pid in omitted_ids] or
            len(accepted_ids) + len(omitted_ids) != len(expected_ids) or
            len(set(accepted_ids + omitted_ids)) != len(expected_ids)):
        fail("FactoryFX stage report does not account for every approved module")
    if standard_ready(plugins) and omitted_ids:
        fail("Standard FactoryFX cannot omit any locked module")
    return [locked[pid] for pid in accepted_ids]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=["stage", "verify"])
    parser.add_argument("--lock", default="config/factoryfx-approved-releases.json")
    parser.add_argument("--notices", default="config/factoryfx-notices")
    parser.add_argument("--output", default="dist")
    parser.add_argument("--require", choices=["none", "factory", "four", "six"], default="none")
    parser.add_argument("--best-effort", action="store_true",
                        help="Include each approved, retrievable plugin; never block core mixer")
    args = parser.parse_args()
    plugins = read_lock(args.lock)
    if args.best_effort and args.require != "none":
        fail("--best-effort cannot bypass an explicit --require gate")
    if args.require == "factory" and not plugins:
        fail("Factory FX required but no approved release")
    if args.require == "four" and distribution_for(plugins) != "production-four":
        fail("Four-FX release requires EQ, Comp, Verb and KuPik — all approved")
    if args.require == "six" and distribution_for(plugins) != "production-six":
        fail("Six-FX release requires all six approved effects")
    if args.action == "stage":
        stage(plugins, Path(args.output), Path(args.notices), args.best_effort)
        # The normal Windows distribution verifier consumes a report even
        # when a strict CLI gate was selected explicitly.
        if args.require == "four" or args.require == "six":
            report = {"schemaVersion": 1, "mode": "standard-four",
                      "included": [{"productId": p["productId"],
                                    "artifactSha256": p["artifactSha256"].lower()} for p in plugins],
                      "omitted": []}
            (Path(args.output) / "FACTORYFX_STAGE_REPORT.json").write_text(
                json.dumps(report, indent=2) + "\n", encoding="utf-8")
    else:
        accepted = (read_best_effort_report(plugins, Path(args.output))
                    if args.best_effort else plugins)
        verify(accepted, Path(args.output), Path(args.notices))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, urllib.error.URLError, zipfile.BadZipFile, KeyError) as exc:
        print("FACTORYFX REJECTED: " + str(exc), file=sys.stderr)
        sys.exit(1)
