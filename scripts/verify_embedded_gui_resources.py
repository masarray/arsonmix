#!/usr/bin/env python3
"""Fast, byte-exact attestation of the GUI resources INSIDE a Windows EXE.

The Pages assembler already creates one approved GUI and the native bundler
adds only an explicitly reversed JUCE event seam. This verifies the actual
distributed executable, not merely a manifest or CMake's input directory.

Use a read-only mmap and its native substring search rather than a PowerShell
byte-by-byte scan over a multi-megabyte EXE for every PNG. Fail closed for
missing, altered, duplicate or path-escaping staged resources.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import mmap
from pathlib import Path, PurePosixPath
import re


class EmbeddedGuiError(ValueError):
    """An approved resource cannot be attested to the executable."""


def verify(exe: Path, bundle: Path, manifest_path: Path) -> int:
    if not exe.is_file() or exe.stat().st_size == 0:
        raise EmbeddedGuiError(f"Missing/empty native executable: {exe}")
    if not manifest_path.is_file():
        raise EmbeddedGuiError(f"Missing native GUI manifest: {manifest_path}")
    doc = json.loads(manifest_path.read_text(encoding="utf-8"))
    if doc.get("schema") != "arsonmix.approved-native-pages-bundle.v2":
        raise EmbeddedGuiError("Unknown approved GUI manifest schema")
    entries = doc.get("files")
    if not isinstance(entries, list) or not entries:
        raise EmbeddedGuiError("Empty or invalid approved GUI resource list")

    root = bundle.resolve(strict=True)
    seen: set[str] = set()
    with exe.open("rb") as fh, mmap.mmap(fh.fileno(), 0, access=mmap.ACCESS_READ) as image:
        for entry in entries:
            if not isinstance(entry, dict):
                raise EmbeddedGuiError("Malformed approved GUI resource record")
            rel, digest = entry.get("path"), entry.get("sha256")
            if not isinstance(rel, str) or not isinstance(digest, str):
                raise EmbeddedGuiError("Missing approved GUI path/hash")
            posix = PurePosixPath(rel)
            if (
                rel in seen or posix.is_absolute() or "\\" in rel
                or any(part in (".", "..") for part in rel.split("/"))
                or not (rel == "index.html" or rel.startswith("assets/"))
                or not re.fullmatch(r"[0-9a-f]{64}", digest)
            ):
                raise EmbeddedGuiError(f"Unsafe/duplicate approved GUI resource: {rel}")
            seen.add(rel)
            path = (root / rel).resolve()
            if not path.is_relative_to(root) or not path.is_file():
                raise EmbeddedGuiError(f"Missing or escaped staged GUI resource: {rel}")
            raw = path.read_bytes()
            if not raw or hashlib.sha256(raw).hexdigest() != digest:
                raise EmbeddedGuiError(f"Staged GUI resource SHA mismatch: {rel}")
            if image.find(raw) < 0:
                raise EmbeddedGuiError(
                    f"APPROVED GUI BYTE RESOURCE NOT IN WINDOWS EXE: {rel}"
                )

    if "index.html" not in seen:
        raise EmbeddedGuiError("Approved GUI index.html not in manifest")
    return len(seen)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--exe", type=Path, required=True)
    ap.add_argument("--bundle", type=Path, required=True)
    ap.add_argument("--manifest", type=Path, required=True)
    args = ap.parse_args()
    count = verify(args.exe, args.bundle, args.manifest)
    print(f"APPROVED_GUI_EMBEDDED_BYTES_OK resources={count}")


if __name__ == "__main__":
    main()
