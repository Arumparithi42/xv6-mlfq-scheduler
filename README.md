# Implementation and Performance Analysis of a Multilevel Feedback Queue Scheduler in xv6

## 1. Project Overview

This project implements a **Multilevel Feedback Queue (MLFQ) Scheduler** in the xv6-riscv operating system and compares its performance against the original (unmodified) xv6 round-robin scheduler.

The scheduler uses three priority queues:

- **Q0** – Highest priority
- **Q1** – Medium priority
- **Q2** – Lowest priority

| Queue | Priority | Time Quantum |
|-------|----------|--------------|
| Q0 | Highest | 4 ticks |
| Q1 | Medium | 8 ticks |
| Q2 | Lowest | 16 ticks |

New processes are initially placed in **Q0**. CPU-bound processes that continuously use their complete time quantum are gradually demoted to lower-priority queues. I/O-bound processes that yield/sleep before using their full quantum are not unfairly demoted. An **aging mechanism** prevents starvation of processes waiting in lower-priority queues.

This repository contains two versions of xv6:

| Directory | Description |
|---|---|
| `xv6-riscv` | Modified version with the MLFQ scheduler |
| `xv6-riscv-backup` | Original xv6 scheduler, with identical measurement instrumentation added for fair comparison |

---

## 2. Requirements

- Linux environment (tested on Kali Linux, works on any Debian/Ubuntu-based distro)
- RISC-V GCC cross compiler (`gcc-riscv64-linux-gnu` or `riscv64-unknown-elf-gcc`)
- QEMU (`qemu-system-misc`, provides `qemu-system-riscv64`)
- GNU Make
- Git

Install on Debian/Ubuntu/Kali:

```bash
sudo apt update
sudo apt install git build-essential gdb-multiarch qemu-system-misc \
    gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu
```

---

## 3. Project Directory

```bash
cd ~/Desktop/OS_Project/xv6-riscv
ls
```

You should see:

```
Makefile
kernel/
user/
README.md
```

---

## 4. Configure the Number of CPUs

For deterministic testing, this project uses a **single CPU** in both versions.

Open the Makefile:

```bash
nano Makefile
```

Find:

```makefile
CPUS := 3
```

Change to:

```makefile
CPUS := 1
```

Save (`Ctrl+O`, `Enter`, `Ctrl+X`). A single CPU makes scheduler behavior deterministic and the two versions directly comparable.

---

## 5. Build the Project

```bash
make clean
make
```

Recommended one-shot sequence after any source change:

```bash
make clean
make
make qemu
```

---

## 6. Run xv6

```bash
make qemu
```

Once booted, you'll see:

```
xv6 kernel is booting
init: starting sh
$
```

You can now run the benchmark programs. To exit QEMU and return to the host terminal: press `Ctrl+A`, release, then press `X`.

---

## 7. MLFQ Scheduling Policy

```
        Q0
   Highest Priority
   Quantum = 4 ticks
          |
          | Full quantum used
          v
        Q1
   Medium Priority
   Quantum = 8 ticks
          |
          | Full quantum used
          v
        Q2
   Lowest Priority
   Quantum = 16 ticks
```

The scheduler always selects a runnable process from the highest-priority non-empty queue: **Q0 > Q1 > Q2**.

### Demotion rules

| Queue | Condition | Result |
|---|---|---|
| Q0 | `ticks_used >= 4` | Moves to Q1 |
| Q1 | `ticks_used >= 8` | Moves to Q2 |
| Q2 | `ticks_used >= 16` | Remains in Q2, quantum counter resets |

### Aging (starvation prevention)

Every `RUNNABLE` process accumulates `waiting_ticks`. When `waiting_ticks >= 50`, the process is promoted one queue level (e.g. `Q2 → Q1`, `Q1 → Q0`). Q0 cannot be promoted further. Aging threshold ≈ 50 ticks (~5 seconds).

### I/O-bound fairness

A process waking from `SLEEPING` gets a fresh quantum (`ticks_used` and `waiting_ticks` reset to 0) so it isn't penalized merely for having performed I/O.

---

## 8. Benchmark Programs

| Program | Purpose |
|---|---|
| `cpubench` | Infinite CPU-bound loop — used for qualitative demotion observation, never exits |
| `cpubench_med` | Finite CPU-bound loop (~9–10 seconds / ~97 ticks on this setup) — **primary benchmark for performance comparison** |
| `cpubench_short` | Very short CPU-bound loop — finishes in ~1 tick; only useful for confirming the binary runs, not for quantitative comparison |
| `iobench` | Repeated small computation + `pause()` between iterations (20 iterations) — represents I/O-bound behavior |

Run any of them directly at the xv6 shell prompt:

```
$ cpubench_med
$ iobench
```

> **Filename note:** xv6's filesystem limits names to 14 characters (`DIRSIZ` in `kernel/fs.h`). `cpubench_medium` (15 chars) will fail `mkfs` with an assertion error — use `cpubench_med` instead.

---

## 9. Reading Process Statistics

When a benchmark process exits, its statistics are printed automatically:

```
PID <pid> finished: CPU=<ticks> Wait=<ticks> Response=<ticks> Turnaround=<ticks>
```

| Field | Meaning |
|---|---|
| `CPU` | Total CPU ticks the process actually ran for |
| `Wait` | Total ticks spent `RUNNABLE` but not running (accumulated over the process's life) |
| `Response` | `first_run_tick - creation_tick` — time from creation to first getting the CPU |
| `Turnaround` | `finish_tick - creation_tick` — total time from creation to completion |

You can also press **`Ctrl+P`** at any time while xv6 is running to dump a live snapshot of the process table (not a shell command — a direct keypress). In the MLFQ version this additionally shows each process's current queue (`Q0`/`Q1`/`Q2`) and in-quantum tick usage.

---

## 10. Testing Procedures

### 10.1 Single CPU-bound process (demotion test — MLFQ only)

```
$ cpubench_med
```

Press `Ctrl+P` periodically while it runs to watch it move `Q0 → Q1 → Q2` as it exhausts each quantum.

### 10.2 Multiple competing CPU-bound processes (MLFQ only)

```
$ cpubench_med &
$ cpubench_med &
$ cpubench_med
```

All three should gradually converge toward Q2. Watch with `Ctrl+P`.

### 10.3 Aging (MLFQ only)

With several CPU-bound processes competing (as above), a process waiting long enough in Q2 should be observed promoting back to Q1, and eventually Q0, if it keeps waiting.

### 10.4 I/O-bound process alone

```
$ iobench
```

Watch it stay in Q0 across its sleep/wake cycles via `Ctrl+P` (MLFQ only — the backup has no queues to observe, but the same stats print on exit).

### 10.5 Mixed CPU-bound + I/O-bound workload (main comparison test)

Run this **identically on both `xv6-riscv` and `xv6-riscv-backup`**, waiting for the `$` prompt to return between commands:

```
$ cpubench_med &
$ iobench
```

Sample test cases are showned in sampletest folder

## 11. Main Modified Files

### MLFQ version (`xv6-riscv`)

| File | Contents |
|---|---|
| `kernel/proc.h` | Added fields: `queue_level`, `ticks_used`, `waiting_ticks`, `cpu_ticks`, `total_wait_ticks`, `creation_tick`, `first_run_tick`, `finish_tick` |
| `kernel/proc.c` | Modified `allocproc()`, `scheduler()`, `wakeup()`, `kexit()`, `procdump()`; added `update_aging()` |
| `kernel/trap.c` | Added `timer_yield()`; hooked into `usertrap()`/`kerneltrap()`; `clockintr()` calls `update_aging()` |
| `kernel/defs.h` | Declarations for `timer_yield()`, `update_aging()` |
| `Makefile` | `CPUS := 1`; benchmark program entries |

### Baseline version (`xv6-riscv-backup`)

The scheduler itself (`scheduler()`, `yield()`, `sched()`) is **completely unmodified stock xv6** — only measurement instrumentation was added, so the comparison isolates the scheduling policy as the only variable.

| File | Contents |
|---|---|
| `kernel/proc.h` | Added fields: `cpu_ticks`, `total_wait_ticks`, `creation_tick`, `first_run_tick`, `finish_tick` (no queue-related fields — not applicable to RR) |
| `kernel/proc.c` | `allocproc()` initializes the new fields; added `update_stats()`; `kexit()` prints finish statistics via `printk()` |
| `kernel/trap.c` | `clockintr()` calls `update_stats()` once per tick |
| `kernel/defs.h` | Declaration for `update_stats()` |
| `Makefile` | `CPUS := 1`; same benchmark program entries as the MLFQ version |

## 13. Summary Checklist

- [x] New processes start in Q0
- [x] CPU-bound processes demoted based on quantum usage (Q0: 4 ticks, Q1: 8 ticks, Q2: 16 ticks)
- [x] I/O-bound processes retain priority across sleep/wake cycles
- [x] Aging promotes starved processes after 50 waiting ticks
- [x] CPU, Wait, Response, and Turnaround statistics recorded on process exit
- [x] Identical measurement instrumentation added to baseline for fair comparison
