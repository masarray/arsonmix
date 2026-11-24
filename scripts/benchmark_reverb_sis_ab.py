#!/usr/bin/env python3
"""Non-gating timing, same-runner A/B of real core with identical settings."""
import argparse
import pathlib
import statistics
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
CORE = pathlib.Path("third_party/arsonverb")
SOURCES = ("src/Reverb.cpp", "src/Presets.cpp", "src/dsp/Primitives.hpp", "include/arverb/Reverb.hpp")

def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)

def compile_core(base, binary):
    subprocess.run(["g++", "-std=c++17", "-O2", "-fno-fast-math",
                    "-I" + str(base / "include"), str(base / "src/Reverb.cpp"),
                    str(base / "src/Presets.cpp"), str(ROOT / CORE / "tests/benchmark_live.cpp"),
                    "-o", str(binary)], check=True)

def run(binary):
    rows = {}
    for line in subprocess.check_output([str(binary)], text=True).splitlines():
        fs, block, mode, count, median, p99 = line.split(",")
        rows[tuple(map(int, (fs, block, mode, count)))] = (float(median), float(p99))
    if len(rows) != 32:
        raise RuntimeError("incomplete benchmark")
    return rows

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--reference", required=True)
    parser.add_argument("--verify-pcm", action="store_true")
    args = parser.parse_args()
    if len(args.reference) != 40 or any(c not in "0123456789abcdef" for c in args.reference):
        parser.error("reference must be full SHA")
    try:
        git("cat-file", "-e", args.reference + "^{commit}")
    except subprocess.CalledProcessError:
        git("fetch", "--no-tags", "--depth=1", "origin", args.reference)
    with tempfile.TemporaryDirectory(prefix="reverb-ab-") as temp:
        work = pathlib.Path(temp)
        old = work / "reference"
        for name in SOURCES:
            p = old / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(git("show", args.reference + ":" + str(CORE / name)))
        ref, new = work / "ref", work / "new"
        compile_core(old, ref)
        compile_core(ROOT / CORE, new)
        # Exact sound-preservation gate on a separate, untimed render.
        # Only enable for topology-equivalent optimization references.
        if args.verify_pcm:
            a = subprocess.check_output([str(ref), "--pcm"], text=True)
            b = subprocess.check_output([str(new), "--pcm"], text=True)
            old_rows, new_rows = a.splitlines(), b.splitlines()
            if len(old_rows) != 40 or len(new_rows) != 40 or a != b:
                for old, new_row in zip(old_rows, new_rows):
                    if old != new_row:
                        print(f"PCM mismatch reference={old} candidate={new_row}", flush=True)
                raise RuntimeError("cross-build PCM fingerprint mismatch")
            print("PASS Reverb-Sis cross-build PCM fingerprints: 40 profile/rate/mode cases")
        # Counterbalance run order to reduce thermal / scheduling bias.
        a1, b1, b2, a2 = run(ref), run(new), run(new), run(ref)
        print("Reverb-Sis same-runner core benchmark (microseconds; p99 wall time, non-gating)")
        speedups = []
        for key in sorted(a1):
            fs, block, mode, count = key
            a = statistics.mean([a1[key][0], a2[key][0]])
            b = statistics.mean([b1[key][0], b2[key][0]])
            p99 = max(b1[key][1], b2[key][1])
            budget = block / fs * 1e6
            speedups.append(a / b)
            print(f"Reverb-Sis perf fs={fs} block={block} mode={mode} instances={count} "
                  f"refP50={a:.2f} newP50={b:.2f} speedup={a/b:.3f} "
                  f"newP99={p99:.2f} callbackBudgetPct={100*p99/budget:.2f}")
        print(f"Reverb-Sis perf median speedup={statistics.median(speedups):.3f} "
              "(portable core only; not whole mixer/physical ASIO)")
if __name__ == "__main__":
    main()
