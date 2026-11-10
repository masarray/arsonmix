#!/usr/bin/env python3
"""Harmonice offline quality-study contract. Synthetic output != singer qualification.

This verifier tests the real C++ WAV renderer, not just isolated fake metrics:
four synthetic sources, 32/24/16ms source alignment, float32 RIFF integrity,
candidate causal bass limitation, dry-lead sample identity and honest metadata.
"""
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

CASES = {
    "SYNTHETIC_low_C2_65Hz": False,
    "SYNTHETIC_mid_A3_220Hz": False,
    "SYNTHETIC_high_A4_440Hz": False,
    "SYNTHETIC_antiphase_A3_220Hz": True,
}


def float32_stereo(path: Path):
    data = path.read_bytes()
    assert len(data) >= 44 and data[:4] == b"RIFF" and data[8:12] == b"WAVE", path
    assert int.from_bytes(data[4:8], "little") + 8 == len(data), path
    cursor, audio, fmt = 12, None, None
    while cursor + 8 <= len(data):
        tag = data[cursor:cursor + 4]
        n = int.from_bytes(data[cursor + 4:cursor + 8], "little")
        begin = cursor + 8
        assert begin + n <= len(data), path
        if tag == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", data, begin)
        if tag == b"data":
            audio = memoryview(data)[begin:begin + n]
        cursor = begin + n + (n & 1)
    assert fmt is not None and fmt[0] == 3 and fmt[1] == 2
    assert fmt[2] == 48000 and fmt[4] == 8 and fmt[5] == 32
    assert audio is not None and len(audio) % 8 == 0
    assert len(audio) >= 48000 * 8
    return audio


def check_file_pair(base: Path, aligned: Path, reference_offset: int, tolerance=0.0):
    a, b = float32_stereo(base), float32_stereo(aligned)
    assert len(a) == len(b), (base, aligned)
    for offset in range(reference_offset * 8, len(a), 8):
        al, ar = struct.unpack_from("<ff", a, offset)
        bl, br = struct.unpack_from("<ff", b, offset)
        assert all(map(math.isfinite, (al, ar, bl, br))), aligned
        assert abs(al - bl) <= tolerance and abs(ar - br) <= tolerance, (
            base, aligned, offset // 8, (al, ar), (bl, br)
        )


def main(exe: str):
    with tempfile.TemporaryDirectory(prefix="harmonice_quality_") as temp:
        folder = Path(temp) / "quality"
        subprocess.run([exe, "--synthetic-corpus", str(folder)], check=True,
                       timeout=180)
        disclaimer = (folder / "READ_ME_FIRST.txt").read_text()
        assert "NOT recordings of humans" in disclaimer
        for name, antiphase in CASES.items():
            source = float32_stereo(folder / (name + ".wav"))
            if antiphase:
                for k in range(2000, len(source) // 8, 397):
                    l, r = struct.unpack_from("<ff", source, k * 8)
                    assert l == -r
            else:
                for k in range(2000, len(source) // 8, 397):
                    l, r = struct.unpack_from("<ff", source, k * 8)
                    assert l == r
            output = folder / name
            metrics = json.loads((output / "latency-study.json").read_text())
            assert metrics["productionPdcFrames"] == 1536
            assert metrics["sourceRate"] == 48000
            assert metrics["physicalAsioMeasured"] is False
            assert metrics["humanSingerListeningPassed"] is False
            assert metrics["syntheticFormantsProved"] is False
            assert [x["targetMs"] for x in metrics["experimentalCandidates"]] == [24, 16]
            assert math.isfinite(metrics["baselineBackingRms"])
            reference = output / "harmonice_32ms_dry.wav"
            for candidate in metrics["experimentalCandidates"]:
                ms = candidate["targetMs"]
                expected_frames = {24: 1152, 16: 768}[ms]
                assert candidate["reportedPdcFrames"] == expected_frames
                assert candidate["offlineAlignmentFrames"] == 1536 - expected_frames
                assert candidate["qualityAccepted"] is False
                assert candidate["pitchSync65HzCausallySupported"] is (ms == 24)
                assert abs(candidate["alignedDryDifferenceRms"]) < 1e-7
                for metric in ("wetBackingRms", "wetBackingRmsRelativeTo32ms",
                               "stereoSideRms", "monoFoldRms", "maximumAbsSample",
                               "maximumOneSampleStep", "alignedMixDifferenceRms"):
                    assert math.isfinite(candidate[metric]), (name, ms, metric)
                    assert candidate[metric] >= 0
                assert candidate["maximumAbsSample"] <= 4
                check_file_pair(
                    reference, output / f"harmonice_{ms}ms_dry_aligned.wav",
                    1536 + 256
                )
            print(f"HARMONICE_SYNTHETIC_CORPUS_VALID source={name} "
                  "dry_alignment=sample_exact 16ms_bass_gate=blocked "
                  "no_human_singer_quality_claim=true")
        print("Harmonice four-fixture WAV/JSON quality contract PASS (synthetic only)")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: harmonice_quality_report_contract_test.py <harmonice-ab-exe>")
    main(sys.argv[1])
