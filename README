# Implementation and Performance Analysis of a Multilevel Feedback Queue Scheduler in xv6

## 📌 Project Overview

This project focuses on the **implementation of a Multilevel Feedback Queue (MLFQ) Scheduler** in the RISC-V version of the **xv6 operating system**.

The original xv6 operating system uses a simple scheduling mechanism. In this project, the scheduler is extended to implement multiple priority levels, different CPU time quanta, dynamic priority demotion, aging-based priority promotion, timer-based preemption, and process-level scheduling statistics.

The main purpose of this project is to understand the internal working of an operating-system scheduler and study how process priority can dynamically change according to CPU usage and waiting time.

---

## 🎯 Objectives

The major objectives of this project are:

- Implement a Multilevel Feedback Queue scheduler in xv6.
- Introduce multiple priority levels for processes.
- Assign different time quanta to different priority levels.
- Start newly created processes at the highest priority.
- Dynamically demote CPU-bound processes.
- Implement aging to prevent starvation.
- Handle sleeping and waking processes.
- Implement timer-based preemption.
- Track CPU and waiting time of processes.
- Measure response time and turnaround time.
- Provide process-level scheduling information.
- Test the scheduler using CPU-bound and I/O-style workloads.

---

# 🧠 Multilevel Feedback Queue Scheduling

## What is MLFQ?

A **Multilevel Feedback Queue (MLFQ)** scheduler organizes processes into multiple priority queues.

Processes can move between queues depending on their CPU usage and waiting time.

The main characteristics of the implemented scheduler are:

- Higher-priority processes are scheduled before lower-priority processes.
- CPU-intensive processes gradually move to lower-priority queues.
- Processes waiting for a long time can be promoted using aging.
- Different queues have different time quanta.

---

# 📊 Queue Design

The implementation contains **three priority queues**.

| Queue | Priority | Time Quantum |
|-------|----------|--------------|
| Q0 | Highest | 4 ticks |
| Q1 | Medium | 8 ticks |
| Q2 | Lowest | 16 ticks |

The queue priority is:

```text
Q0 > Q1 > Q2








xv6 is a re-implementation of Dennis Ritchie's and Ken Thompson's Unix
Version 6 (v6).  xv6 loosely follows the structure and style of v6,
but is implemented for a modern RISC-V multiprocessor using ANSI C.

ACKNOWLEDGMENTS

xv6 is inspired by John Lions's Commentary on UNIX 6th Edition (Peer
to Peer Communications; ISBN: 1-57398-013-7; 1st edition (June 14,
2000)).  See also https://pdos.csail.mit.edu/6.1810/, which provides
pointers to on-line resources for v6.

The following people have made contributions: Russ Cox (context switching,
locking), Cliff Frey (MP), Xiao Yu (MP), Nickolai Zeldovich, and Austin
Clements.

We are also grateful for the bug reports and patches contributed by
Abhinavpatel00, Takahiro Aoyagi, Marcelo Arroyo, Hirbod Behnam, Silas
Boyd-Wickizer, Anton Burtsev, carlclone, Ian Chen, clivezeng, Dan
Cross, Cody Cutler, Mike CAT, Tej Chajed, Asami Doi,Wenyang Duan,
echtwerner, eyalz800, Nelson Elhage, Saar Ettinger, Alice Ferrazzi,
Nathaniel Filardo, flespark, Peter Froehlich, Yakir Goaron, Shivam
Handa, Matt Harvey, Bryan Henry, jaichenhengjie, Jim Huang, Matúš
Jókay, John Jolly, Alexander Kapshuk, Anders Kaseorg, kehao95,
Wolfgang Keller, Jungwoo Kim, Jonathan Kimmitt, Eddie Kohler, Vadim
Kolontsov, Austin Liew, Bruce Lowekamp, l0stman, Pavan Maddamsetti,
Imbar Marinescu, Yandong Mao, Matan Shabtay, Hitoshi Mitake, Carmi
Merimovich, mes900903, Mark Morrissey, mtasm, Joel Nider, Hayato Ohhashi,
OptimisticSide, papparapa, phosphagos, Harry Porter, Greg Price, Zheng
qhuo, Quancheng, RayAndrew, Jude Rich, segfault, Ayan Shafqat, Eldar
Sehayek, Yongming Shen, Fumiya Shigemitsu, snoire, Taojie, Cam Tenny,
tyfkda, Warren Toomey, Stephen Tu, Alissa Tung, Rafael Ubal, unicornx,
Amane Uehara, Pablo Ventura, Luc Videau, Xi Wang, WaheedHafez, Keiichi
Watanabe, Lucas Wolf, Nicolas Wolovick, wxdao, Grant Wu, x653, Andy
Zhang, Jindong Zhang, Icenowy Zheng, ZhUyU1997, and Zou Chang Wei.

ERROR REPORTS

Please send errors and suggestions to Frans Kaashoek and Robert Morris
(kaashoek,rtm@mit.edu).  The main purpose of xv6 is as a teaching
operating system for MIT's 6.1810, so we are more interested in
simplifications and clarifications than new features.

BUILDING AND RUNNING XV6

You will need a RISC-V "newlib" tool chain from
https://github.com/riscv/riscv-gnu-toolchain, and qemu compiled for
riscv64-softmmu.  Once they are installed, and in your shell
search path, you can run "make qemu".
