#!/usr/bin/env python3
"""Run the functional tests and save their output to results/tests/.

  1. schedtest under several scheduler configurations (1 and 3 CPUs,
     MLFQ with different quanta and aging, and the round-robin baseline).
  2. A smoke-test session exercising the shell, the benchmark programs,
     background jobs, Ctrl-P and kill on the default kernel.

The upstream xv6 test suite is run separately with ./test-xv6.py.
"""

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from xv6qemu import XV6  # noqa: E402

SCHEDTEST = [
    ("mlfq-1cpu", {}),
    ("mlfq-3cpu", {"CPUS": "3"}),
    ("rr-1cpu", {"SCHED": "RR"}),
    ("rr-3cpu", {"SCHED": "RR", "CPUS": "3"}),
    ("q2-4-8", {"QUANTA": "2 4 8"}),
    ("q8-16-32", {"QUANTA": "8 16 32"}),
    ("aging0", {"AGING": "0"}),
    ("aging25", {"AGING": "25"}),
]

SMOKE = [
    "echo hello xv6",
    "ls README",
    "forktest",
    "schedtime cpubench_short",
    "schedtime cpubench_med",
    "schedtime -n 3 cpubench_med",
    "schedtime iobench 20",
    "schedtime cpubench 200",
]


def main():
    os.makedirs("results/tests", exist_ok=True)
    ok = True
    summary = []
    for name, makevars in SCHEDTEST:
        xv6 = XV6(makevars)
        try:
            out = xv6.run("schedtest", timeout=600)
        finally:
            xv6.close()
        with open(f"results/tests/schedtest-{name}.log", "w") as f:
            f.write(out)
        passed = "ALL TESTS PASSED" in out
        ok &= passed
        line = f"schedtest {name:10s} {'PASS' if passed else 'FAIL'}"
        summary.append(line)
        print(line, flush=True)

    # Smoke test on the default kernel.
    xv6 = XV6()
    log = []
    try:
        for cmd in SMOKE:
            log.append(xv6.run(cmd, timeout=300))
        # A CPU-bound job in the background; watch it with Ctrl-P as it
        # is demoted, then kill it.
        log.append(xv6.run("cpubench &"))
        for _ in range(3):
            log.append("[Ctrl-P]" + xv6.send(b"\x10", wait=0.3))
        pids = re.findall(r"^(\d+) \S+\s+cpubench", log[-1], re.M)
        if pids:
            log.append(xv6.run(f"kill {pids[-1]}"))
            log.append("[Ctrl-P after kill]" + xv6.send(b"\x10", wait=0.5))
    finally:
        xv6.close()
    smoke = "".join(log)
    with open("results/tests/smoke.log", "w") as f:
        f.write(smoke)
    smoke_ok = ("hello xv6" in smoke and "fork test OK" in smoke and
                smoke.count("turnaround=") == 7 and
                "cpubench" not in smoke.split("[Ctrl-P after kill]")[-1])
    ok &= smoke_ok
    line = f"smoke session       {'PASS' if smoke_ok else 'FAIL'}"
    summary.append(line)
    print(line)
    with open("results/tests/summary.txt", "w") as f:
        f.write("\n".join(summary) + "\n")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
