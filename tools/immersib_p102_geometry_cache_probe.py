#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Standalone P102.3 six-wall geometry-cache and two-pass FIR A/B lab.

The owner-approved EARLY FIR header and all 30 PCM sources stay byte-for-byte
UNCHANGED. A generated candidate is confined to a temporary include directory.
Compile two independently instantiated REAL six-wall processors (approved
reference vs candidate), feed identical stereo PCM, compare float bits, and
report skipped geometry rebuilds. This is NOT a product CPU speedup claim.
"""
from __future__ import annotations

import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
EARLY = ROOT / "engine/builtin_fx/sonku/SonKuEarlyReflections.h"
TEST = ROOT / "tools/immersib_p102_geometry_cache_probe.cpp"
MACROS = (
    "SONKU_P7_MEASURED_ELEVATION_RESEARCH",
    "SONKU_P71_DIRECT_XYZ_RESEARCH",
    "SONKU_P72_NATIVE_XYZ_RT",
    "SONKU_P75_MUSIC_SAFE_RESEARCH",
    "SONKU_P80_TRANSPARENT_BASS_RESEARCH",
    "SONKU_P82_MULTIBAND_ROOM_RESEARCH",
    "SONKU_P83_PHYSICAL_SPEAKER_RESEARCH",
    "SONKU_P84_SINGLE_CORE_FUSION_RESEARCH",
    "SONKU_P85_WORLD_COHERENCE_RESEARCH",
    "SONKU_P86_HORIZONTAL_FOCUS_RESEARCH",
    "SONKU_P87_MUSIC_BODY_RESEARCH",
    "SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH",
    "SONKU_P89_DEPTH_COHERENCE_RESEARCH",
    "SONKU_P90_TRUE_EXTERNAL_RESEARCH",
    "SONKU_P94_FRONT_BACK_DEPTH_RESEARCH",
    "SONKU_P95_MUSICAL_PRESENCE_RESEARCH",
    "SONKU_P96_DISTANCE_REINFORCEMENT_RESEARCH",
    "SONKU_P97_STAGE_IMPACT_RESEARCH",
    "SONKU_P98_SCENE_VOICING_RESEARCH",
    "SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH",
    "SONKU_IMMERSIB_P99_VOCAL_ROOM_RESEARCH",
    "ARMIX_SONKU_EXPERIMENTAL_P98_SCENE_VOICING",
    "ARMIX_IMMERSIB_STAGE_MONITOR_RESEARCH",
    "ARMIX_IMMERSIB_MUSICAL_EQ_RESEARCH",
)


def replace_exact(text: str, old: str, new: str, count: int = 1) -> str:
    found = text.count(old)
    if found != count:
        raise ValueError(
            f"FIR_LAB_REJECTED_SOURCE_DRIFT expected={count} found={found} "
            f"needle={old[:80]!r}"
        )
    return text.replace(old, new)


def build_candidate(source: str, two_pass: bool = False,
                    stasis: bool = False) -> str:
    start = source.index("class SonKuEarlyReflections final {")
    end = source.rindex("} // namespace armix::sonku")
    clone = source[start:end]
    clone = replace_exact(
        clone, "class SonKuEarlyReflections final {",
        "class SonKuEarlyReflectionsGeometryCache final {",
    )
    clone = replace_exact(
        clone, "    static constexpr int geometryInterval=64;",
        "    static constexpr int geometryInterval=64;\n"
        "    std::uint64_t skippedGeometryUpdates() const noexcept {\n"
        "        return geometryCacheSkips_;\n"
        "    }",
    )
    clone = replace_exact(
        clone,
        "        if(untilGeometry_==0){\n"
        "            updateGeometry(azimuth,spread,headYawDegrees);\n"
        "            untilGeometry_=geometryInterval;\n"
        "        }",
        "        if(untilGeometry_==0){\n"
        "            // Proven-static geometry may reuse the SAME target taps.\n"
        "            // All per-sample HRIR kernel slew, FIR accumulation,\n"
        "            // delays and gain arithmetic still run unchanged.\n"
        "            if(!geometryCacheValid_ || azimuth!=cachedAzimuth_ ||\n"
        "               spread!=cachedSpread_ || headYawDegrees!=cachedYaw_){\n"
        "                updateGeometry(azimuth,spread,headYawDegrees);\n"
        "                cachedAzimuth_=azimuth;\n"
        "                cachedSpread_=spread;\n"
        "                cachedYaw_=headYawDegrees;\n"
        "                geometryCacheValid_=true;\n"
        "            }else ++geometryCacheSkips_;\n"
        "            untilGeometry_=geometryInterval;\n"
        "        }",
    )
    count = clone.count("untilGeometry_=0;")
    if count < 4:
        raise ValueError("FIR_LAB_REJECTED_INVALIDATION_SITES")
    clone = clone.replace(
        "untilGeometry_=0;", "untilGeometry_=0;geometryCacheValid_=false;"
    )
    clone = replace_exact(
        clone, "    int write_=0,untilGeometry_=0,validFrames_=0;",
        "    int write_=0,untilGeometry_=0,validFrames_=0;\n"
        "    bool geometryCacheValid_=false;\n"
        "    double cachedAzimuth_=0.;\n"
        "    float cachedSpread_=0.f,cachedYaw_=0.f;\n"
        "    std::uint64_t geometryCacheSkips_=0;",
    )
    if stasis:
        # Guarded candidate: skip an HRIR slew only if EVERY coefficient's
        # exact next scalar update is bitwise unchanged for two probes.
        # Invalidate at genuine geometry changes and fusion transitions.
        clone = replace_exact(
            clone, "    struct Tap {\n",
            "    struct Tap {\n"
            "        bool kernelStationary=false;\n"
            "        std::uint8_t stationaryChecks=0;\n",
        )
        clone = replace_exact(
            clone,
            "                updateGeometry(azimuth,spread,headYawDegrees);\n"
            "                cachedAzimuth_=azimuth;",
            "                updateGeometry(azimuth,spread,headYawDegrees);\n"
            "                for(auto& source:tap_)for(auto& p:source){\n"
            "                    p.kernelStationary=false;\n"
            "                    p.stationaryChecks=0;\n"
            "                }\n"
            "                cachedAzimuth_=azimuth;",
        )
        clone = replace_exact(
            clone,
            "        if(fastZeroSpread_ && spread!=0.f)fuseEligible_=false;",
            "        if(fastZeroSpread_ && spread!=0.f && fuseEligible_){\n"
            "            fuseEligible_=false;\n"
            "            for(auto& source:tap_)for(auto& p:source){\n"
            "                p.kernelStationary=false;\n"
            "                p.stationaryChecks=0;\n"
            "            }\n"
            "        }",
        )
        clone = replace_exact(
            clone, "        Ears output{};",
            "        if(directionalHrir_ && (write_ & 127)==0){\n"
            "            for(auto& source:tap_)for(auto& p:source){\n"
            "                if(p.kernelStationary)continue;\n"
            "                bool stable=true;\n"
            "                for(int ear=0;ear<2 && stable;++ear)\n"
            "                    for(int k=0;k<hrir::taps;++k){\n"
            "                        const float h=p.kernel[ear][k];\n"
            "                        const float next=h+kernelSlew_*\n"
            "                            (p.targetKernel[ear][k]-h);\n"
            "                        if(std::bit_cast<std::uint32_t>(next)!=\n"
            "                           std::bit_cast<std::uint32_t>(h)){\n"
            "                            stable=false;break;\n"
            "                        }\n"
            "                    }\n"
            "                if(stable){\n"
            "                    if(p.stationaryChecks<2)++p.stationaryChecks;\n"
            "                    if(p.stationaryChecks>=2)p.kernelStationary=true;\n"
            "                }else p.stationaryChecks=0;\n"
            "            }\n"
            "        }\n"
            "        Ears output{};",
        )
        clone = replace_exact(
            clone,
            "                        hL+=kernelSlew_*(p.targetKernel[0][k]-hL);\n"
            "                        hR+=kernelSlew_*(p.targetKernel[1][k]-hR);",
            "                        if(!p.kernelStationary){\n"
            "                            hL+=kernelSlew_*(p.targetKernel[0][k]-hL);\n"
            "                            hR+=kernelSlew_*(p.targetKernel[1][k]-hR);\n"
            "                        }",
        )
    if two_pass:
        # Preserve the exact per-tap coefficient update and sequential
        # l/r accumulation, but separate the kernel-slew pass from the
        # audio FIR pass. This allows the compiler to optimize independent
        # kernel updates without reordering the floating PCM sums.
        clone = replace_exact(
            clone,
            "                    const auto render=[&](auto&& sample) noexcept {\n"
            "                        for(int k=0;k<freshTaps;++k){\n"
            "                            const float v=sample(k);\n"
            "                            const auto h=advance(k);\n"
            "                            l+=v*h[0];r+=v*h[1];\n"
            "                        }\n"
            "                        for(int k=freshTaps;k<hrir::taps;++k)\n"
            "                            (void)advance(k);\n"
            "                    };",
            "                    const auto render=[&](auto&& sample) noexcept {\n"
            "                        // One independent kernel-slew pass. The\n"
            "                        // unchanged FIR accumulation still visits\n"
            "                        // k=0..freshTaps in its original order.\n"
            "                        for(int k=0;k<hrir::taps;++k)\n"
            "                            (void)advance(k);\n"
            "                        for(int k=0;k<freshTaps;++k){\n"
            "                            const float v=sample(k);\n"
            "                            l+=v*p.kernel[0][k];\n"
            "                            r+=v*p.kernel[1][k];\n"
            "                        }\n"
            "                    };",
        )
    return (
        "// GENERATED LAB-ONLY, never checked into product acoustic sources.\n"
        "#pragma once\n"
        "#include <bit>\n"
        '#include "engine/builtin_fx/sonku/SonKuEarlyReflections.h"\n'
        "namespace armix::sonku {\n" + clone
        + "\n} // namespace armix::sonku\n"
    )


def main() -> int:
    # Source lock must pass BEFORE generating any candidate.
    subprocess.run(
        [sys.executable, "scripts/verify_immersib_approved_pcm_sources.py"],
        cwd=ROOT, check=True, timeout=30
    )
    gxx = shutil.which("g++")
    if gxx is None:
        raise RuntimeError("FIR_LAB_GXX_NOT_FOUND")
    with tempfile.TemporaryDirectory(prefix="imersib-geometry-bitexact-") as d:
        root_tmp = pathlib.Path(d)
        approved_source = EARLY.read_text(encoding="utf-8")
        for variant, two_pass, stasis in (
            ("geometry_only", False, False),
            ("geometry_plus_two_pass_fir", True, False),
            ("geometry_plus_kernel_stasis", False, True),
        ):
            tmp = root_tmp / variant
            tmp.mkdir()
            (tmp / "SonKuEarlyReflectionsGeometryCache.h").write_text(
                build_candidate(approved_source, two_pass=two_pass, stasis=stasis),
                encoding="utf-8",
            )
            cmd = [
                gxx, "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
                "-pedantic", "-ffp-contract=off", "-I", str(ROOT),
                "-I", str(tmp),
                *["-D" + m + "=1" for m in MACROS],
                str(TEST), "-o", str(tmp / "geometry_bitexact"),
            ]
            print("IMMERSIB_LAB_VARIANT=" + variant, flush=True)
            subprocess.run(cmd, cwd=ROOT, check=True, timeout=210)
            subprocess.run(
                [str(tmp / "geometry_bitexact")],
                cwd=ROOT, check=True, timeout=210,
            )
    print("IMMERSIB_GEOMETRY_LAB_COMPLETED source_lock=UNCHANGED "
          "shipping_dsp=UNMODIFIED hardware=NOT_TESTED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
