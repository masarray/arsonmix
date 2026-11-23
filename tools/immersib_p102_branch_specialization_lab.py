#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""One falsifiable MSVC DIR-branch hoisting experiment, never product code.

The branch specializes fused vs non-fused HRIR interpolation outside the
140-tap FIR loop. This might help MSVC avoid a per-tap branch. The existing
source's arithmetic is untouched. Compare the ENTIRE real P102.3 full-core PCM
byte stream against the already bit-qualified optimized implementation, then
measure matched callback CPU on same Windows runner. If slow, discard.
"""
from __future__ import annotations

import pathlib
import subprocess
import sys
import tempfile

from immersib_p102_fir_shipping_gate import PATH, CANDIDATE_BLOB, blob_hash
from immersib_p102_fullcore_gate import (
    ROOT, approved_header, build_and_measure
)

CONTIG_OLD = """                        render([&](int k) noexcept {
                            return fused
                                ?((1.f-frac)*(first[-k]+second[-k])+
                                    frac*(first[-k-1]+second[-k-1]))
                                :((1.f-frac)*first[-k]+frac*first[-k-1]);
                        });"""
CONTIG_NEW = """                        if(fused)
                            render([&](int k) noexcept {
                                return ((1.f-frac)*(first[-k]+second[-k])+
                                    frac*(first[-k-1]+second[-k-1]));
                            });
                        else
                            render([&](int k) noexcept {
                                return ((1.f-frac)*first[-k]+frac*first[-k-1]);
                            });"""

MASKED_OLD = """                        render([&](int k) noexcept {
                            const int at=(idx-k)&(ringFrames-1);
                            const int prev=(at-1)&(ringFrames-1);
                            return fused
                                ?((1.f-frac)*(signal[at]+history_[1][at])+
                                    frac*(signal[prev]+history_[1][prev]))
                                :((1.f-frac)*signal[at]+frac*signal[prev]);
                        });"""
MASKED_NEW = """                        if(fused)
                            render([&](int k) noexcept {
                                const int at=(idx-k)&(ringFrames-1);
                                const int prev=(at-1)&(ringFrames-1);
                                return ((1.f-frac)*(signal[at]+history_[1][at])+
                                    frac*(signal[prev]+history_[1][prev]));
                            });
                        else
                            render([&](int k) noexcept {
                                const int at=(idx-k)&(ringFrames-1);
                                const int prev=(at-1)&(ringFrames-1);
                                return ((1.f-frac)*signal[at]+frac*signal[prev]);
                            });"""


def specialize(source: bytes) -> bytes:
    if blob_hash(source) != CANDIDATE_BLOB:
        raise ValueError("IMMERSIB_BRANCH_SPECIALIZATION_SOURCE_NOT_PROVEN")
    text = source.decode("utf-8")
    for old, new in ((CONTIG_OLD,CONTIG_NEW),(MASKED_OLD,MASKED_NEW)):
        if text.count(old)!=1:
            raise ValueError("IMMERSIB_BRANCH_SPECIALIZATION_SOURCE_DRIFT")
        text=text.replace(old,new)
    return text.encode("utf-8")


def main() -> int:
    msvc=len(sys.argv)==2 and sys.argv[1]=="--msvc"
    if len(sys.argv)>1 and not msvc:
        raise ValueError("IMMERSIB_BRANCH_SPECIALIZATION_BAD_OPTION")
    subprocess.run(
        [sys.executable, "scripts/verify_immersib_approved_pcm_sources.py"],
        cwd=ROOT, check=True, timeout=30
    )
    old=approved_header()
    candidate=(ROOT / PATH).read_bytes().replace(b"\r\n",b"\n")
    variant=specialize(candidate)
    with tempfile.TemporaryDirectory(prefix="immersib-p102-branch-specialize-") as directory:
        root=pathlib.Path(directory)
        baseline_bytes,base=build_and_measure(root,"optimized",old,msvc)
        variant_bytes,optimized=build_and_measure(
            root,"specialized",old,msvc,variant
        )
        if baseline_bytes!=variant_bytes:
            first=next(i for i,(a,b) in enumerate(
                zip(baseline_bytes,variant_bytes)) if a!=b)
            raise ValueError(
                f"IMMERSIB_SPECIALIZATION_PCM_DRIFT offset={first}"
            )
        print("IMMERSIB_SPECIALIZATION_FULLCORE_PCM_BITEXACT_PASS "
              f"bytes={len(variant_bytes)} candidate=BRANCH_HOISTED "
              f"compiler={'MSVC' if msvc else 'GCC'}")
        for key in sorted(base):
            a=base[key];b=optimized[key]
            print(
                "IMMERSIB_SPECIALIZATION_FULLCORE_CPU_AB "
                f"rate={key[0]} scene={key[1]} "
                f"reference_p50_us={a[0]:.2f} variant_p50_us={b[0]:.2f} "
                f"p50_ratio={b[0]/a[0]:.4f} "
                f"compiler={'MSVC' if msvc else 'GCC'} "
                "policy=DO_NOT_SHIP_UNLESS_MEANINGFUL"
            )
    return 0


if __name__=="__main__":
    sys.exit(main())
