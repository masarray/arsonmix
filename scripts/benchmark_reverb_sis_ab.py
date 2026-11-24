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

def compile_core(base, binary, album=False, signature=False, anchor=False):
    subprocess.run(["g++", "-std=c++17", "-O2", "-fno-fast-math",
                    "-I" + str(base / "include"), str(base / "src/Reverb.cpp"),
                    str(base / "src/Presets.cpp"), str(ROOT / CORE / "tests/benchmark_live.cpp"),
                    "-o", str(binary)] + (["-DARVERB_ALBUM_BENCHMARK"] if album else []) + (["-DARVERB_SIGNATURE_BENCHMARK"] if signature else []) + (["-DARVERB_ANCHOR_BENCHMARK"] if anchor else []), check=True)

def run(binary, *args):
    rows = {}
    for line in subprocess.check_output([str(binary), *args], text=True).splitlines():
        fs, block, mode, count, median, p99, p999, worst = line.split(",")
        rows[tuple(map(int, (fs, block, mode, count)))] = tuple(map(float, (median,p99,p999,worst)))
    if len(rows) != 48:
        raise RuntimeError("incomplete benchmark")
    return rows

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--reference", required=True)
    parser.add_argument("--verify-pcm", action="store_true")
    parser.add_argument("--album", action="store_true")
    parser.add_argument("--signature", action="store_true")
    parser.add_argument("--preserve-signature", action="store_true", help="reference already has Signature; compare identical steady algorithms")
    parser.add_argument("--anchor", action="store_true")
    args = parser.parse_args()
    if args.anchor and not args.preserve_signature:
        parser.error("--anchor requires --preserve-signature")
    if args.preserve_signature and not (args.signature and args.verify_pcm):
        parser.error('--preserve-signature requires --signature and --verify-pcm')
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
        compile_core(old, ref, args.signature, args.preserve_signature)
        compile_core(ROOT / CORE, new, args.album or args.signature, args.signature, args.anchor)
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
            if args.signature:
                a = subprocess.check_output([str(ref), "--pcm-album"], text=True)
                b = subprocess.check_output([str(new), "--pcm-album"], text=True)
                if len(a.splitlines()) != 40 or a != b:
                    raise RuntimeError("historical Album PCM fingerprint mismatch")
                print("PASS historical Album PCM fingerprints: 40 profile/rate/mode cases")
            if args.preserve_signature:
                a = subprocess.check_output([str(ref), "--pcm-signature"], text=True)
                b = subprocess.check_output([str(new), "--pcm-signature"], text=True)
                if len(a.splitlines()) != 20 or a != b:
                    for old_row, new_row in zip(a.splitlines(), b.splitlines()):
                        if old_row != new_row:
                            print(f"Signature PCM mismatch reference={old_row} candidate={new_row}", flush=True)
                    raise RuntimeError("Signature steady-algorithm PCM fingerprint mismatch")
                print("PASS Signature steady-algorithm PCM fingerprints: 20 rate/mode cases, all 13 controls automated")
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
        if args.album:
            # Same candidate binary/settings, change only the new profile.
            on1, off1, off2, on2 = run(new, "--album"), run(new), run(new), run(new, "--album")
            overheads = []
            for key in sorted(on1):
                fs, block, mode, count = key
                off = statistics.mean([off1[key][0], off2[key][0]])
                on = statistics.mean([on1[key][0], on2[key][0]])
                p99 = max(on1[key][1], on2[key][1])
                overheads.append(on / off)
                print(f"Reverb-Sis album perf fs={fs} block={block} mode={mode} instances={count} "
                      f"offP50={off:.2f} onP50={on:.2f} ratio={on/off:.3f} "
                      f"onP99={p99:.2f} callbackBudgetPct={100*p99/(block/fs*1e6):.2f}")
            print(f"Reverb-Sis album perf median cost ratio={statistics.median(overheads):.3f}")
        if args.signature:
            reference_mode = "--signature" if args.preserve_signature else "--album"
            a1, b1, b2, a2 = run(ref, reference_mode), run(new, "--signature"), run(new, "--signature"), run(ref, reference_mode)
            ratios = []
            for key in sorted(a1):
                fs, block, mode, count = key
                a = statistics.mean([a1[key][0], a2[key][0]])
                b = statistics.mean([b1[key][0], b2[key][0]])
                ratios.append(b/a)
                p99 = max(b1[key][1], b2[key][1])
                p999 = max(b1[key][2], b2[key][2])
                worst = max(b1[key][3], b2[key][3])
                print(f"Signature perf fs={fs} block={block} mode={mode} instances={count} refP50={a:.2f} signatureP50={b:.2f} ratio={b/a:.3f} signatureP99={p99:.2f} signatureP999={p999:.2f} worst={worst:.2f} budgetPct={100*p99/(block/fs*1e6):.2f} p999BudgetPct={100*p999/(block/fs*1e6):.2f}")
            label = "previous Signature" if args.preserve_signature else "historical Album"
            print(f"Signature median cost ratio vs {label}={statistics.median(ratios):.3f} (48 configurations, 2048 measured blocks per run; wall-clock non-gating)")
        if args.anchor:
            a1,b1,b2,a2=run(new,"--signature"),run(new,"--anchor"),run(new,"--anchor"),run(new,"--signature")
            ratios=[(b1[key][0]+b2[key][0])/(a1[key][0]+a2[key][0]) for key in a1]
            print(f"Vocal Anchor median cost ratio vs Signature={statistics.median(ratios):.3f} (same binary, 48 cases)")
if __name__ == "__main__":
    main()
