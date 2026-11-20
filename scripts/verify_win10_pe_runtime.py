#!/usr/bin/env python3
"""Fail closed if a Windows x64 PE imports Visual C++ redistributable DLLs.

Runs on the exact EXE that the per-user Inno installer will ship. /MT is
configured for the WHOLE JUCE target graph; this is an independent binary
check, not evidence that WebView2 or hardware ASIO is installed.
Standard-library only, no downloads or machine-wide changes.
"""
from __future__ import annotations

import argparse
import os
import pathlib
import re
import struct
import sys


def imports(path: pathlib.Path) -> set[str]:
    data = path.read_bytes()

    def u16(at: int) -> int:
        if at < 0 or at + 2 > len(data):
            raise ValueError("Truncated PE word")
        return struct.unpack_from("<H", data, at)[0]

    def u32(at: int) -> int:
        if at < 0 or at + 4 > len(data):
            raise ValueError("Truncated PE dword")
        return struct.unpack_from("<I", data, at)[0]

    if len(data) < 256 or data[:2] != b"MZ":
        raise ValueError("Not an MZ executable")
    offset = u32(0x3C)
    if data[offset:offset + 4] != b"PE\x00\x00":
        raise ValueError("Not a PE executable")
    coff = offset + 4
    if u16(coff) != 0x8664:
        raise ValueError("Expected AMD64 PE machine")
    count = u16(coff + 2)
    optional = coff + 20
    opt_bytes = u16(coff + 16)
    magic = u16(optional)
    if magic != 0x20B:  # release Windows build is x64 PE32+
        raise ValueError("Expected 64-bit Windows executable")
    if optional + opt_bytes > len(data):
        raise ValueError("Truncated optional header")
    directory = optional + 112
    if directory + 16 * 8 > optional + opt_bytes or u32(optional + 108) < 16:
        raise ValueError("Missing import data-directory")
    sections = optional + opt_bytes
    section_rows: list[tuple[int, int, int]] = []
    for i in range(count):
        sec = sections + i * 40
        vaddr = u32(sec + 12)
        rsize = u32(sec + 16)
        raw = u32(sec + 20)
        if raw + rsize > len(data):
            raise ValueError("Section exceeds PE file")
        section_rows.append((vaddr, rsize, raw))

    def rva_to_offset(rva: int) -> int:
        for va, size, raw in section_rows:
            if va <= rva < va + size and raw + rva - va < len(data):
                return raw + rva - va
        raise ValueError(f"Unmapped import RVA {rva:x}")

    def cstring(rva: int) -> str:
        at = rva_to_offset(rva)
        end = data.find(b"\x00", at, min(len(data), at + 512))
        if end < 0:
            raise ValueError("Unterminated DLL import")
        return data[at:end].decode("ascii").lower()

    names: set[str] = set()
    # Standard and delay-import descriptor tables have distinct record sizes.
    for index, width, name_offset in ((1, 20, 12), (13, 32, 4)):
        table = u32(directory + index * 8)
        size = u32(directory + index * 8 + 4)
        if not table and not size:
            continue
        if not table or size < width:
            raise ValueError("Invalid import descriptor table")
        for position in range(0, min(size, 1024 * width) - width + 1, width):
            at = rva_to_offset(table + position)
            end = rva_to_offset(table + position + width - 1)
            if end != at + width - 1:
                raise ValueError("Import descriptor crosses file sections")
            if not any(data[at:end + 1]):
                break
            if index == 13 and not u32(at) & 1:
                raise ValueError("Unsupported absolute-VA delay import")
            names.add(cstring(u32(at + name_offset)))
        else:
            raise ValueError("Unterminated PE imports")
    return names


def check(path: pathlib.Path) -> None:
    names = imports(path)
    if not names:
        raise ValueError("No imported libraries: unexpected native GUI PE")
    forbidden = [n for n in sorted(names) if re.fullmatch(
        r"(?:(?:vcruntime|msvcp|msvcr|concrt|mfcm?|vcomp|vcamp|vccorlib)\d+[a-z0-9_-]*|"
        r"ucrtbased|webview2loader)\.dll", n)]
    if forbidden:
        raise ValueError("Unexpected runtime dependency: " + ", ".join(forbidden))
    if sys.platform == "win32":
        system32 = pathlib.Path(os.environ["SystemRoot"]) / "System32"
        missing = sorted(name for name in names if
                         not name.startswith(("api-ms-win-", "ext-ms-win-")) and
                         (pathlib.PureWindowsPath(name).name != name or
                          not (system32 / name).is_file()))
        if missing:
            raise ValueError("Non-OS or missing runtime dependencies: " + ", ".join(missing))
    print("WIN10_STATIC_CRT_PE_PASS: audited direct/delay DLLs: " + ", ".join(sorted(names)))
    print("WEBVIEW2_RUNTIME_REQUIRED_SEPARATELY_NOT_PROVEN")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=pathlib.Path, required=True)
    args = parser.parse_args()
    try:
        check(args.exe)
    except (OSError, ValueError, struct.error) as err:
        print("WIN10_STATIC_CRT_PE_FAIL: " + str(err), file=sys.stderr)
        sys.exit(1)
