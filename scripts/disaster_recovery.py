#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Offline Git recovery with an optional public GitHub release archive.
No remote writes, execution of recovered code, or upload of local data.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

SCHEMA = 1
REPO = "masarray/arsonmix"
SOURCE = "https://github.com/" + REPO + ".git"


def git(*args):
    # Do not inherit repository overrides, hooks, filters or alternate stores.
    env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
    env.update(GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull,
               GIT_TERMINAL_PROMPT="0")
    return subprocess.check_output(
        ["git", "-c", "core.hooksPath=" + os.devnull,
         "-c", "protocol.ext.allow=never", *map(str, args)],
        env=env, text=True, encoding="utf-8").strip()


def inventory(repo):
    result = git("-C", repo, "for-each-ref", "--format=%(refname) %(objectname)")
    if not result:
        raise ValueError("empty repository")
    refs = dict(line.split(" ", 1) for line in result.splitlines())
    return {"head": git("-C", repo, "symbolic-ref", "HEAD"), "refs": refs}


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def plain(path):
    # Windows junctions are reparse points even when not reported as symlinks.
    st = path.lstat()
    if path.is_symlink() or getattr(st, "st_file_attributes", 0) & 0x400:
        raise ValueError("linked path: " + str(path))
    return path.is_file()


def files(root):
    result = {}
    if plain(root) or not root.is_dir():
        raise ValueError("not a backup directory")
    for path in root.rglob("*"):
        if plain(path):
            name = path.relative_to(root).as_posix()
            if name != "manifest.json":
                result[name] = {"bytes": path.stat().st_size, "sha256": digest(path)}
        elif not path.is_dir():
            raise ValueError("special file: " + str(path))
    return result


def gh_json(*args):
    return json.loads(subprocess.check_output(
        ["gh", "api", *args], text=True, encoding="utf-8"))


def release_inventory():
    pages = gh_json("--paginate", "--slurp", "repos/" + REPO + "/releases?per_page=100")
    releases = [release for page in pages for release in page if not release["draft"]]
    seen = set()
    for release in releases:
        rid = release["id"]
        if type(rid) is not int or rid <= 0 or rid in seen:
            raise ValueError("invalid or duplicate release id")
        seen.add(rid)
        # Embedded release assets may not be a complete inventory; page explicitly.
        asset_pages = gh_json("--paginate", "--slurp", "repos/" + REPO +
                              "/releases/" + str(rid) + "/assets?per_page=100")
        release["assets"] = [asset for page in asset_pages for asset in page]
    return releases


def archive_releases(root):
    releases = release_inventory()
    target = root / "releases"
    target.mkdir()
    (target / "metadata.json").write_text(json.dumps(releases, indent=2), encoding="utf-8")
    seen = set()
    for release in releases:
        # Asset IDs are immutable numeric locators. Never use tag or asset name as path.
        for asset in release["assets"]:
            aid = asset["id"]
            if type(aid) is not int or aid <= 0 or aid in seen:
                raise ValueError("invalid or duplicate asset id")
            seen.add(aid)
            out = target / (str(aid) + ".asset")
            with out.open("xb") as stream:
                subprocess.run(["gh", "api", "-H", "Accept: application/octet-stream",
                                "repos/" + REPO + "/releases/assets/" + str(aid)],
                               stdout=stream, check=True)
            if out.stat().st_size != asset["size"]:
                raise ValueError("release asset size mismatch")
            expected = asset.get("digest")
            if expected and expected != "sha256:" + digest(out):
                raise ValueError("release asset published digest mismatch")
    # Reject a moving release inventory; download counts alone may change.
    def stable(items):
        return sorted((r["id"], r["tag_name"], r["updated_at"],
                       sorted((a["id"], a["name"], a["size"], a.get("digest"),
                               a["updated_at"]) for a in r["assets"])) for r in items)
    if stable(release_inventory()) != stable(releases):
        raise ValueError("release inventory changed during backup")


def verify(root):
    root = Path(root)
    if plain(root) or not root.is_dir():
        raise ValueError("invalid backup root")
    manifest = root / "manifest.json"
    if not plain(manifest) or manifest.stat().st_size > 32 * 1024 * 1024:
        raise ValueError("invalid manifest")
    def unique(pairs):
        obj = {}
        for key, value in pairs:
            if key in obj:
                raise ValueError("duplicate manifest key")
            obj[key] = value
        return obj
    data = json.loads(manifest.read_text(encoding="utf-8"), object_pairs_hook=unique)
    if (data["schema"] != SCHEMA or "source.bundle" not in data["files"] or
            data["files"] != files(root)):
        raise ValueError("backup checksum or inventory mismatch")
    return data


def restore_into(root, destination, data):
    # Caller supplies fresh private staging. Clone from bundle only; no GitHub.
    git("clone", "--mirror", "--", root / "source.bundle", destination)
    git("-C", destination, "symbolic-ref", "HEAD", data["git"]["head"])
    git("-C", destination, "config", "--remove-section", "remote.origin")
    git("-C", destination, "fsck", "--full", "--strict")
    if inventory(destination) != data["git"]:
        raise ValueError("restored ref inventory differs")
    git("-C", destination, "bundle", "verify", root / "source.bundle")


def publish(staged, target):
    if target.exists() or target.is_symlink():
        raise FileExistsError(target)
    staged.rename(target)


def backup(source, target, releases=False):
    target = Path(target).absolute()
    if target.exists() or target.is_symlink():
        raise FileExistsError(target)
    if releases and source != SOURCE:
        raise ValueError("release archive requires official repository")
    # Local paths are allowed for offline fixtures/owner mirrors; no arbitrary
    # URL or remote-helper command is accepted.
    if source != SOURCE:
        source = str(Path(source).resolve(strict=True))
    with tempfile.TemporaryDirectory(prefix=".arsonmix-recovery-", dir=target.parent) as temp:
        tmp = Path(temp)
        mirror, stage = tmp / "mirror.git", tmp / "backup"
        stage.mkdir()
        git("clone", "--mirror", "--no-local", "--", source, mirror)
        git("-C", mirror, "fsck", "--full", "--strict")
        snapshot = inventory(mirror)
        git("-C", mirror, "bundle", "create", stage / "source.bundle", "--all")
        if releases:
            archive_releases(stage)
        data = {"schema": SCHEMA, "git": snapshot, "releases": releases,
                "files": files(stage)}
        (stage / "manifest.json").write_text(json.dumps(data, indent=2), encoding="utf-8")
        restore_into(stage, tmp / "drill.git", verify(stage))
        verify(stage)
        publish(stage, target)


def restore(root, target):
    root, target = Path(root).absolute(), Path(target).absolute()
    if target.exists() or target.is_symlink():
        raise FileExistsError(target)
    if target == root or root in target.parents:
        raise ValueError("restore destination must be outside backup")
    data = verify(root)
    with tempfile.TemporaryDirectory(prefix=".arsonmix-restore-", dir=target.parent) as temp:
        stage = Path(temp) / "restored.git"
        restore_into(root, stage, data)
        if verify(root) != data:
            raise ValueError("backup changed during restore")
        publish(stage, target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    b = sub.add_parser("backup")
    b.add_argument("destination")
    b.add_argument("--source", default=SOURCE)
    b.add_argument("--releases", action="store_true")
    v = sub.add_parser("verify")
    v.add_argument("backup")
    r = sub.add_parser("restore")
    r.add_argument("backup")
    r.add_argument("destination")
    args = parser.parse_args()
    if args.command == "backup":
        backup(args.source, args.destination, args.releases)
    elif args.command == "verify":
        verify(args.backup)
    else:
        restore(args.backup, args.destination)
    print("PASS:", args.command)


if __name__ == "__main__":
    main()
