#!/usr/bin/env python3
"""Fail closed if a Windows x64 PE imports Visual C++ redistributable DLLs.

Runs on the exact EXE that the per-user Inno installer will ship. /MT is
configured for the WHOLE JUCE target graph; this is an independent binary
check, not evidence that WebView2 or hardware ASIO is installed.
Standard-library only, no downloads or machine-wide changes.
"""
from __future__ import annotations

import argparse
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
    count = u16(coff + 2)
    optional = coff + 20
    opt_bytes = u16(coff + 16)
    magic = u16(optional)
    if magic != 0x20B:  # release Windows build is x64 PE32+
        raise ValueError("Expected 64-bit Windows executable")
    if optional + opt_bytes > len(data):
        raise ValueError("Truncated optional header")
    directory = optional + 112
    if directory + 16 * 8 > optional + opt_bytes:
        raise ValueError("Missing import data-directory")
    sections = optional + opt_bytes
    section_rows: list[tuple[int, int, int]] = []
    for i in range(count):
        sec = sections + i * 40
        vsize = u32(sec + 8)
        vaddr = u32(sec + 12)
        rsize = u32(sec + 16)
        raw = u32(sec + 20)
        section_rows.append((vaddr, max(vsize, rsize), raw))

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
    # Standard import descriptors (20-byte structs; Name at +12).
    base = u32(directory + 8)
    if base:
        at = rva_to_offset(base)
        for _ in range(1024):
            if at + 20 > len(data):
                raise ValueError("Truncated import descriptors")
            if data[at:at + 20] == b"\x00" * 20:
                break
            names.add(cstring(u32(at + 12)))
            at += 20
        else:
            raise ValueError("Unterminated PE imports")
    # Delay-load descriptors (32-byte structs; DLL Name at +4).
    delay_rva = u32(directory + 13 * 8)
    if delay_rva:
        at = rva_to_offset(delay_rva)
        for _ in range(1024):
            if at + 32 > len(data):
                raise ValueError("Truncated delay-import descriptors")
            if data[at:at + 32] == b"\x00" * 32:
                break
            attributes = u32(at)
            if attributes & 1:  # name is RVA, not virtual address
                names.add(cstring(u32(at + 4)))
            else:
                raise ValueError("Unsupported absolute-VA delay import")
            at += 32
        else:
            raise ValueError("Unterminated delay imports")
    return names


def check(path: pathlib.Path) -> None:
    names = imports(path)
    if not names:
        raise ValueError("No imported libraries: unexpected native GUI PE")
    forbidden = [n for n in sorted(names) if
                 re.match(r"^(vcruntime\d*(?:_\d+)?|msvcp\d+(?:_\d+)?|"
                          r"concrt\d+|mfc\d+|ucrtbased)\.dll$", n)]
    if forbidden:
        raise ValueError("Windows redistributable dependency: " + ", ".join(forbidden))
    print("WIN10_STATIC_CRT_PE_PASS: verified executable imports no VC++ runtime "
          f"DLLs; {len(names)} direct/delay DLL imports inspected")
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
