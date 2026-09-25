#!/usr/bin/env python3
"""Run the scheduling experiments and save the raw xv6 output.

For every scheduler configuration below, build the kernel, boot it once
(one CPU), run "mlfqexp <experiment> <reps>" for each experiment and
save the console output to results/raw/<mode>/<config>.log.

  scripts/run_experiments.py                 # both modes, all configs
  scripts/run_experiments.py --mode icount   # deterministic clock only
  scripts/run_experiments.py --config mlfq rr

Modes:
  icount    QEMU's instruction-count clock: runs are exactly repeatable.
  realtime  QEMU's normal clock, which follows host time; results vary
            slightly between runs, so more repetitions are made.

Then run scripts/analyze.py to produce tables and figures.
"""

import argparse
import datetime
import os
import platform
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from xv6qemu import XV6  # noqa: E402

ALL = ["single", "multi", "mixed", "aging"]

# name -> (make variables, experiments)
CONFIGS = {
    "mlfq":     ({}, ALL),                          # 4/8/16, aging 50
    "rr":       ({"SCHED": "RR"}, ALL),             # original xv6
    "q2-4-8":   ({"QUANTA": "2 4 8"}, ["single", "multi", "mixed"]),
    "q8-16-32": ({"QUANTA": "8 16 32"}, ["single", "multi", "mixed"]),
    "aging0":   ({"AGING": "0"}, ["aging"]),
    "aging25":  ({"AGING": "25"}, ["aging"]),
    "aging100": ({"AGING": "100"}, ["aging"]),
}

REPS = {"icount": 3, "realtime": 5}


def run_config(mode, name, reps, outdir):
    makevars, exps = CONFIGS[name]
    print(f"[{mode}] {name}: {makevars or 'defaults'} -> {', '.join(exps)}",
          flush=True)
    xv6 = XV6(makevars, icount=(mode == "icount"))
    try:
        log = [f"# config={name} mode={mode} makevars={makevars}\n"]
        for exp in exps:
            log.append(xv6.run(f"mlfqexp {exp} {reps}", timeout=900))
    finally:
        xv6.close()
    path = os.path.join(outdir, f"{name}.log")
    with open(path, "w") as f:
        f.write("".join(log))
    print(f"  wrote {path}", flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", choices=["icount", "realtime", "both"],
                    default="both")
    ap.add_argument("--config", nargs="*", default=list(CONFIGS))
    ap.add_argument("--reps", type=int, help="override repetitions")
    args = ap.parse_args()

    modes = ["icount", "realtime"] if args.mode == "both" else [args.mode]
    for mode in modes:
        outdir = os.path.join("results", "raw", mode)
        os.makedirs(outdir, exist_ok=True)
        with open(os.path.join(outdir, "environment.txt"), "w") as f:
            qemu = subprocess.run(["qemu-system-riscv64", "--version"],
                                  capture_output=True, text=True).stdout
            commit = subprocess.run(["git", "rev-parse", "--short", "HEAD"],
                                    capture_output=True, text=True).stdout
            f.write(f"date: {datetime.datetime.now().isoformat()}\n"
                    f"host: {platform.platform()}\n"
                    f"qemu: {qemu.splitlines()[0] if qemu else '?'}\n"
                    f"git: {commit.strip()}\n"
                    f"mode: {mode}\n")
        for name in args.config:
            run_config(mode, name, args.reps or REPS[mode], outdir)
    # Leave the default configuration built.
    subprocess.run(["make", "-s", "kernel/kernel", "fs.img"],
                   stdout=subprocess.DEVNULL)


if __name__ == "__main__":
    main()
