#define NPROC       64                // maximum number of processes
#define NCPU        8                 // maximum number of CPUs
#define NOFILE      16                // open files per process
#define NFILE       100               // open files per system
#define NINODE      50                // maximum number of active i-nodes
#define NDEV        10                // maximum major device number
#define ROOTDEV     1                 // device number of file system root disk
#define MAXARG      32                // max exec arguments
#define MAXOPBLOCKS 10                // max # of blocks any FS op writes
#define LOGBLOCKS   (MAXOPBLOCKS * 3) // max data blocks in on-disk log
#define NBUF        (MAXOPBLOCKS * 3) // size of disk block cache
#define FSSIZE      2000              // size of file system in blocks
#define MAXPATH     128               // maximum file path name
#define USERSTACK   1                 // user stack pages

// ---------------------------------------------------------------------
// Timer and scheduler configuration.
//
// Every value below can be overridden from the make command line, e.g.
//   make qemu SCHED=RR                      (original xv6 round robin)
//   make qemu QUANTA="2 4 8" AGING=100      (MLFQ experiments)
// ---------------------------------------------------------------------

// The RISC-V "time" counter on QEMU's virt machine runs at 10 MHz.
#define TIMEBASE_HZ 10000000

// Timer interrupts per second. Stock xv6 uses 10 (a 100 ms tick); we
// use 100 (a 10 ms tick) so that quanta of a few ticks are realistic.
#ifndef TICK_HZ
#define TICK_HZ 100
#endif
#define TICK_CYCLES (TIMEBASE_HZ / TICK_HZ) // timebase cycles per tick

// Number of MLFQ priority levels. Q0 is the highest priority.
#define NQUEUE 3

// Time quantum (in ticks) of each level. A process that uses up the
// whole quantum of its level is moved one level down.
#ifndef Q0_QUANTUM
#define Q0_QUANTUM 4
#endif
#ifndef Q1_QUANTUM
#define Q1_QUANTUM 8
#endif
#ifndef Q2_QUANTUM
#define Q2_QUANTUM 16
#endif

// A process that has not run for this many ticks (because it was
// waiting for the CPU or sleeping) is moved one level up (aging).
// 0 disables aging.
#ifndef AGING_THRESHOLD
#define AGING_THRESHOLD 50
#endif

// Size of the in-kernel scheduler event trace (see sched.c).
#define NSCHEDTRACE 8192
