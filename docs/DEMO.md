# Demonstration guide (Kali Linux in VirtualBox)

A step-by-step script for presenting the project live, in about 15
minutes. Every command and every expected result below was run and
checked. Your numbers will differ slightly because timing depends on
the machine, but the differences between the schedulers stay the same.

---

## Part A: Preparation (do this the day before)

### A.1 VirtualBox settings

Nothing special is needed: QEMU emulates the RISC-V CPU in software, so
nested virtualisation (VT-x inside the VM) is **not** required.

* Give the Kali VM at least **2 CPUs and 4 GB RAM** (Settings → System).
  More CPUs make QEMU smoother but are not required.
* Install the VirtualBox Guest Additions if you want full-screen mode
  and clipboard sharing.
* The VirtualBox **Host key** is **Right Ctrl** by default. Inside the
  VM, use **Left Ctrl** for the xv6 key combinations (Ctrl-P, Ctrl-A),
  or VirtualBox will capture them.

### A.2 Install the tools (Kali terminal)

```bash
sudo apt update
sudo apt install -y git build-essential bc \
    gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu \
    qemu-system-misc python3-matplotlib
```

Check:

```bash
riscv64-linux-gnu-gcc --version
qemu-system-riscv64 --version      # must be 7.2 or newer
```

### A.3 Get the project and build it

```bash
git clone https://github.com/Arumparithi42/xv6-mlfq-scheduler.git
cd xv6-mlfq-scheduler
git checkout claude/zen-tesla-ppny5z   # skip once the branch is merged into main
make clean
make
```

`make` should finish without errors (a linker warning about "RWX
permissions" is normal for xv6).

### A.4 Rehearse once

Run through Part B completely at least once, and run `schedtest` once
(section B.3) to make sure everything passes in your VM.

Presentation tips:

* Enlarge the terminal font (Ctrl + `+` in the Kali terminal) so the
  faculty can read the output.
* Open `README.md` on GitHub in Firefox in a second window: it shows
  the report with all figures and tables.
* Keep `kernel/param.h` and `kernel/sched.c` open in an editor to show
  the code.

---

## Part B: The live demonstration

### B.1 Show the design (2 minutes)

Open `kernel/param.h` and show the configuration block:

```c
#define NQUEUE 3
#define Q0_QUANTUM 4
#define Q1_QUANTUM 8
#define Q2_QUANTUM 16
#define AGING_THRESHOLD 50
```

Then open `kernel/sched.c`. The comment at the top lists the seven
policy rules. Point out the three key functions:

* `sched_select()`: picks the highest-priority RUNNABLE process,
  first-come-first-served within a level.
* `sched_tick()`: on each timer tick, demotes a process that used its
  whole quantum, and preempts it if a higher-priority process is
  waiting.
* `age()`: promotes a process that has not run for 50 ticks
  (starvation prevention).

What to say: *"The original xv6 switches process on every tick in
process-table order. We keep its structure and locking, and only
replace the decision of which process runs next and when to preempt
it."*

### B.2 Boot xv6 with the MLFQ scheduler

```bash
make qemu
```

Expected:

```
xv6 kernel is booting

init: starting sh
$
```

### B.3 Automated tests (1–2 minutes)

```
$ schedtest
```

Expected (takes about a minute):

```
schedtest: policy MLFQ, 1 CPU(s), tick 10000 us, quanta 4/8/16, aging 50
test fork_wait: OK
test many: OK
test sleep: OK
test kill: OK
test new_q0: OK
test demotion: OK
test io_bound: OK
test gaming: OK
test trace: OK
test preemption: OK
test round_robin: OK
test aging: OK
test idle_promotion: OK
schedtest: ALL TESTS PASSED
```

What to say: *"These check that fork, exit, wait, sleep and kill still
work, that a CPU-bound process is demoted Q0 → Q1 → Q2 after exactly
its quanta, that an I/O-bound process stays in Q0, that sleeping just
before the quantum ends does not avoid demotion, and that aging
prevents starvation."*

### B.4 Watch a process move down the queues

**(a) Exact demotion times, from the kernel's event trace.** This runs
Experiment 1 (one CPU-bound job needing 500 ms of CPU) and keeps only
the level-change events:

```
$ mlfqexp single | grep type=L
```

Expected:

```
EV rep=1 t=39274 pid=7 type=L from=0 to=1 prio=0
EV rep=1 t=121219 pid=7 type=L from=1 to=2 prio=1
```

What to say: *"`t` is in microseconds. The job moved from Q0 to Q1
after 39 ms, which is its 4-tick (40 ms) quantum, and to Q2 after
another 80 ms, its 8-tick Q1 quantum."*

**(b) Live, with Ctrl-P.**

```
$ cpubench &
```

Press **Ctrl-P** (Left Ctrl). xv6 prints its process table:

```
1 sleep  init Q0 qused=41 cpu=41 wait=41 sleep=2022 disp=25 demote=0 promote=0
2 sleep  sh Q0 qused=35 cpu=35 wait=40 sleep=1980 disp=29 demote=0 promote=0
5 run    cpubench Q2 qused=71 cpu=349 wait=4 sleep=0 disp=4 demote=2 promote=0
```

What to say: *"`cpubench` is already in Q2 with `demote=2`: it used its
Q0 and Q1 quanta within 120 ms. The shell and init stay in Q0 because
they mostly sleep. `cpu`, `wait` and `sleep` are in milliseconds."*
(Press Ctrl-P again a few seconds later: `cpu` grows and it stays in
Q2.)

Leave it running and start a second one:

```
$ cpubench &
```

**Wait about 2 seconds**, then press Ctrl-P: both `cpubench` lines
should show `Q2`. This step matters: a newly started CPU-bound process
is treated as interactive until it has used its Q0 and Q1 quanta.

### B.5 A short job while the CPU is busy

```
$ schedtime cpubench_short
```

Expected (MLFQ):

```
pid 8 cpubench_short: Q0 cpu=33.1 wait=43.8 sleep=1.4 response=0.1 turnaround=78.4 ms  dispatch=6 demote=0 promote=0
```

What to say: *"The short job needs about 33 ms of CPU. Although two
CPU-bound processes are running, it starts immediately (response
0.1 ms), because it is in Q0 and they are in Q2."*

### B.6 An I/O-bound job while the CPU is busy

```
$ schedtime iobench 20
```

Expected (MLFQ):

```
pid 10 iobench: Q0 cpu=14.7 wait=43.8 sleep=189.9 response=0.1 turnaround=248.6 ms  dispatch=26 demote=0 promote=0
```

What to say: *"It blocks 20 times, and every time it wakes up it
preempts the CPU-bound processes. It stays in Q0 (`demote=0`)."*

### B.7 Clean up and exit

Press Ctrl-P, note the two `cpubench` pids, then:

```
$ kill <pid1>
$ kill <pid2>
```

Exit QEMU: press **Ctrl-A**, release, then press **X**.

### B.8 The same thing on the original xv6 scheduler

The same source tree builds the original round-robin scheduler:

```bash
make qemu SCHED=RR
```

(It rebuilds the kernel automatically.) Repeat B.4 to B.6 exactly:

```
$ cpubench &
$ cpubench &
      (wait 2 seconds)
$ schedtime cpubench_short
$ schedtime iobench 20
```

Expected (round robin):

```
pid 8 cpubench_short: Q0 cpu=33.8 wait=175.9 sleep=1.7 response=16.7 turnaround=211.5 ms  dispatch=9 demote=0 promote=0
pid 10 iobench: Q0 cpu=16.6 wait=317.1 sleep=192.0 response=13.1 turnaround=525.8 ms  dispatch=26 demote=0 promote=0
```

Ctrl-P shows no demotions: everything stays `Q0`, because the original
scheduler has no priorities.

Summary to show on the board or a slide:

| | MLFQ | original xv6 (RR) |
|---|---:|---:|
| short job turnaround | ≈ 78 ms | ≈ 212 ms |
| short job response | ≈ 0.1 ms | ≈ 17 ms |
| I/O job turnaround | ≈ 249 ms | ≈ 525 ms |
| I/O job waiting time | ≈ 44 ms | ≈ 317 ms |

Kill the `cpubench` processes and exit (Ctrl-A, X).

### B.9 Aging and starvation (2 minutes)

This runs Experiment 4: a 300 ms job (`long`) competes with a stream
of 100 ms jobs that keep Q0 and Q1 busy for 2 seconds. First with the
default aging (threshold 50 ticks):

```bash
make qemu
```

```
$ mlfqexp aging | grep name=long
```

Expected (shortened):

```
JOB exp=aging rep=1 name=long ... turnaround=2130090 maxwait=712912 ... demote=4 promote=2 ...
```

Exit (Ctrl-A, X), then the same with aging switched off:

```bash
make qemu AGING=0
```

```
$ mlfqexp aging | grep name=long
```

Expected (shortened):

```
JOB exp=aging rep=1 name=long ... turnaround=2336505 maxwait=1952607 ... demote=2 promote=0 ...
```

What to say: *"`maxwait` is the longest time the job waited without
getting the CPU, in microseconds. Without aging it waited 1.95 s, the
whole time the stream lasted: starvation. With aging it was promoted
twice (`promote=2`) and never waited more than 0.71 s, which is the
500 ms threshold plus the time to get its turn in Q1."*

Exit (Ctrl-A, X) and rebuild the default configuration: `make qemu`.

### B.10 The controlled experiments (show results, optionally re-run)

Show `README.md` (sections 14–15) in Firefox on GitHub: timelines,
tables and analysis for the five experiments.

To prove the results are generated rather than typed in, re-run part of
the experiments live (about 3 minutes):

```bash
python3 scripts/run_experiments.py --mode icount --config mlfq rr
python3 scripts/analyze.py
xdg-open docs/figures/exp3_mixed_timeline.png
```

In this deterministic mode (QEMU's instruction-counting clock) the
numbers match the report almost exactly, even inside VirtualBox.
Afterwards, `git checkout results docs` restores the committed files.

To run a single experiment by hand inside xv6: `mlfqexp mixed`. This
prints one `JOB` line per job and a long list of `EV` trace lines,
which is meant for the analysis script, not for reading.

---

## Part C: Questions the faculty may ask

**Why three queues and 4/8/16 ticks?** The quantum doubles at each
level (the classic MLFQ choice). The short Q0 quantum gives interactive
jobs fast service; the long Q2 quantum means CPU-bound jobs switch
rarely. They can be changed without editing code:
`make qemu QUANTA="2 4 8"`. Experiment 5 compares 2/4/8, 4/8/16 and
8/16/32.

**What is a tick here?** 10 ms. We changed it from xv6's 100 ms so the
quanta are 40/80/160 ms, which is realistic (README §7.5).

**How do you prevent starvation?** Aging: a process that has not run
for 50 ticks (0.5 s) moves up one level. Experiment 4: without aging a
job starved for 1.9 s; with aging its longest wait was 0.7 s.

**Can a process cheat by sleeping just before its quantum ends?** No.
The quantum counts CPU time and is not reset by sleeping (the `gaming`
test in `schedtest`).

**How are the statistics measured?** At every state change (RUNNABLE,
RUNNING, SLEEPING, ZOMBIE) the kernel reads the RISC-V `time` counter
and adds the elapsed time to the old state's total. So CPU + wait +
sleep = turnaround exactly, and `schedtest` checks this for every
process.

**Is the comparison with the original scheduler fair?** Yes. Both are
built from the same code (`make SCHED=RR`) with the same timer,
statistics code, benchmarks and scripts. Only the scheduling decision
differs.

**Does it work with several CPUs?** Yes: `make qemu CPUS=3`. The
upstream `usertests` suite and `schedtest` pass on 3 CPUs. We found and
fixed an SMP race in aging during testing (README §16).

**What are the limitations?** Preemption only happens at a timer tick;
the scheduler scans all 64 process slots (fine for xv6, not for a real
OS); measurements come from an emulator (README §17).

**Is MLFQ always better?** No. A long CPU-bound job competing with a
continuous stream of short jobs finishes later than under round robin
(Experiment 4), and without aging it would starve. MLFQ deliberately
favours short and interactive work.

---

## Part D: If something goes wrong

| Problem | Fix |
|---|---|
| `Couldn't find a riscv64 version of GCC` | `sudo apt install gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu` |
| `ERROR: Need qemu version >= 7.2` or `bc: not found` | `sudo apt install qemu-system-misc bc` |
| Ctrl-P or Ctrl-A does nothing | Use **Left** Ctrl (Right Ctrl is the VirtualBox host key); click inside the terminal first |
| QEMU does not exit | Ctrl-A then X; otherwise from another terminal: `pkill qemu-system-riscv64` |
| `mkfs: ... assertion` or a strange build error | `make clean` then `make` |
| Numbers differ a little from this guide | Normal: timing depends on the machine. The MLFQ-vs-RR differences stay large. |
| Short job looks slow under MLFQ | You did not wait for the `cpubench` processes to reach Q2 (B.4); check with Ctrl-P |
