#!/usr/bin/env python3
"""Attest actual Windows PE RT_GROUP_ICON pixels against the approved source.

Stdlib only: parse x64 PE resources and compare 256px icon RGBA to the
committed brand. JUCE/Windows premultiplied RGB conversions may round a
visible component by 1-2; alpha must match exactly, RGB <=3 composite units.
"""
import argparse
from pathlib import Path
import struct
import sys
from generate_brand_icons import read_png


def check(data: bytes, expected: Path):
    def bounds(offset, size):
        if offset < 0 or size < 0 or offset + size > len(data):
            raise ValueError("Corrupt PE resource bounds")
    def u16(offset):
        bounds(offset, 2)
        return struct.unpack_from("<H", data, offset)[0]
    def u32(offset):
        bounds(offset, 4)
        return struct.unpack_from("<I", data, offset)[0]
    if data[:2] != b"MZ":
        raise ValueError("Not Windows MZ executable")
    pe = u32(0x3c)
    bounds(pe, 24)
    if data[pe:pe+4] != b"PE\0\0" or u16(pe+4) != 0x8664:
        raise ValueError("Expected AMD64 Windows PE")
    section_count = u16(pe+6)
    if not 1 <= section_count <= 48:
        raise ValueError("Invalid PE section count")
    opt = pe+24
    opt_size = u16(pe+20)
    bounds(opt, opt_size)
    if u16(opt) != 0x20b:
        raise ValueError("Expected Windows PE32+ optional header")
    resource_rva, resource_size = struct.unpack_from("<II", data, opt+112+16)
    if not resource_rva or resource_size < 32:
        raise ValueError("No PE resource section")
    sections = []
    for i in range(section_count):
        pos = opt+opt_size+40*i
        bounds(pos, 40)
        virtual_size, rva, raw_size, raw_pos = struct.unpack_from("<IIII", data, pos+8)
        bounds(raw_pos, raw_size)
        sections.append((rva, max(virtual_size, raw_size), raw_pos, raw_size))
    def file_offset(rva, length):
        for base, _, raw, size in sections:
            delta = rva-base
            if 0 <= delta and delta+length <= size:
                return raw+delta
        raise ValueError("PE RVA outside raw image")
    root = file_offset(resource_rva, 16)
    def entries(node):
        pos = root+node
        bounds(pos, 16)
        named, numeric = struct.unpack_from("<HH", data, pos+12)
        count = named+numeric
        if count > 1024:
            raise ValueError("Excessive PE resource entries")
        bounds(pos+16, count*8)
        result=[]
        for i in range(count):
            ident, offset = struct.unpack_from("<II", data, pos+16+i*8)
            if ident & 0x80000000:
                name_offset = root+(ident & 0x7fffffff)
                nchars = u16(name_offset)
                if nchars > 255:
                    raise ValueError("Invalid resource group name")
                bounds(name_offset+2, nchars*2)
                ident = data[name_offset+2:name_offset+2+nchars*2].decode("utf-16le")
            else:
                ident &= 0x7fffffff
            result.append((ident, offset))
        return result
    resources={}
    for resource_type, type_node in entries(0):
        if resource_type not in (3, 14):
            continue
        if not type_node & 0x80000000:
            raise ValueError("Malformed icon resource type")
        parts={}
        for name, lang_node in entries(type_node & 0x7fffffff):
            if not lang_node & 0x80000000:
                raise ValueError("Missing icon language tree")
            for _, leaf in entries(lang_node & 0x7fffffff):
                if leaf & 0x80000000:
                    raise ValueError("Nested icon data")
                pos = root+(leaf & 0x7fffffff)
                bounds(pos, 16)
                rva, size, _, _ = struct.unpack_from("<IIII", data, pos)
                if size > 1024*1024:
                    raise ValueError("Oversized icon resource")
                off=file_offset(rva, size)
                parts.setdefault(name, data[off:off+size])
        resources[resource_type]=parts
    images=resources.get(3,{})
    groups=resources.get(14,{})
    if not groups or not images:
        raise ValueError("Windows EXE is missing RT_GROUP_ICON/RT_ICON")
    size, expected_rows = read_png(expected)
    if size != 256:
        raise ValueError("Committed approved brand icon is not 256px")
    checked=0
    required={16,32,48,256}
    for name, group in groups.items():
        if len(group)<6:
            raise ValueError("Truncated icon group")
        reserved,kind,count=struct.unpack_from("<HHH", group)
        if (reserved,kind)!=(0,1) or count<4 or len(group)!=6+14*count:
            raise ValueError("Bad Windows icon group header")
        found=set()
        for i in range(count):
            w,h,_,_,planes,depth,length,icon_id=struct.unpack_from("<BBBBHHIH", group, 6+14*i)
            dimension=w or 256
            if dimension!=(h or 256) or dimension not in required or planes!=1 or depth!=32:
                continue
            raw=images.get(icon_id)
            if not raw or length!=len(raw):
                raise ValueError("Icon group references missing/invalid RT_ICON")
            found.add(dimension)
            if dimension!=256:
                continue
            class RawPNG:
                def read_bytes(self):
                    return raw
            actual_size, actual_rows = read_png(RawPNG())
            if actual_size!=256 or len(actual_rows)!=len(expected_rows):
                raise ValueError("Bad 256px Windows icon PNG")
            for y,(old,actual) in enumerate(zip(expected_rows,actual_rows)):
                for pos in range(0,len(old),4):
                    if old[pos+3]!=actual[pos+3]:
                        raise ValueError(f"Icon alpha mismatch x={pos//4} y={y}")
                    alpha=old[pos+3]
                    for c in range(3):
                        if abs(old[pos+c]*alpha-actual[pos+c]*alpha)>3*255:
                            raise ValueError(f"Icon differs from approved brand x={pos//4} y={y}")
            checked+=1
        if required-found:
            raise ValueError(f"Windows icon {name} missing sizes {sorted(required-found)}")
    if checked!=len(groups):
        raise ValueError("Every PE icon group must match approved ArSonMix pixels")
    return checked


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--exe",required=True,type=Path)
    parser.add_argument("--approved",required=True,type=Path)
    args=parser.parse_args()
    try:
        n=check(args.exe.read_bytes(),args.approved)
    except (ValueError,OSError,struct.error,IndexError) as exc:
        print("PE_BRAND_ICON_REJECTED: "+str(exc),file=sys.stderr)
        return 1
    print(f"PE_BRAND_ICON_VERIFIED: {n} Windows icon groups, 16/32/48/256px, approved alpha and visible RGB")
    return 0


if __name__=="__main__":
    sys.exit(main())
