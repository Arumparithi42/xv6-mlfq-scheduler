// Scheduler statistics shared between the kernel and user programs.
// Include "kernel/types.h" before this file.
//
// All times are in microseconds, measured with the RISC-V time counter.

// Per-process statistics, returned by getprocstat() and waitstat().
//
//   turnaround = etime - ctime = cpu_time + wait_time + sleep_time
//   response   = first_run - ctime
struct procstat {
  int pid;
  int state;    // enum procstate (see proc.h), 5 = ZOMBIE
  int priority; // current MLFQ level, 0 = highest
  char name[16];
  uint64 ctime;         // creation time (end of fork: first RUNNABLE)
  uint64 first_run;     // time of first dispatch; 0 = never ran
  uint64 etime;         // exit time; 0 = still alive
  uint64 cpu_time;      // total time RUNNING
  uint64 wait_time;     // total time RUNNABLE (ready, waiting for a CPU)
  uint64 sleep_time;    // total time SLEEPING (blocked, e.g. on I/O)
  uint64 quantum_used;  // part of the current level's quantum used
  uint64 level_time[4]; // CPU time spent at each level (NQUEUE <= 4)
  uint64 max_wait;      // longest single RUNNABLE period
  int ndispatch;        // number of times given the CPU
  int nexpire;          // quantum expirations (full quantum used)
  int npreempt;         // preempted by a higher-priority process
  int nsleep;           // voluntary sleeps (blocking)
  int ndemote;          // moves to a lower-priority level
  int npromote;         // moves to a higher-priority level (aging)
};

// Scheduler configuration of the running kernel, returned by schedinfo().
struct schedinfo {
  int policy;     // SCHED_POLICY_RR or SCHED_POLICY_MLFQ
  int nqueue;     // number of levels
  int quantum[4]; // quantum of each level, in ticks
  int aging;      // aging threshold in ticks, 0 = disabled
  int tick_us;    // length of one tick in microseconds
  int ncpu;       // number of CPUs that booted
};

#define SCHED_POLICY_RR   0
#define SCHED_POLICY_MLFQ 1

// One entry of the scheduler event trace (see schedtrace()).
struct schedevent {
  uint64 time; // microseconds
  int pid;
  uchar type; // SEV_*
  uchar from; // old state (SEV_STATE) or old level (SEV_LEVEL)
  uchar to;   // new state (SEV_STATE) or new level (SEV_LEVEL)
  uchar prio; // level of the process when the event happened
  uchar cpu;
  uchar pad[3];
};

#define SEV_STATE 1 // process changed state (from/to = enum procstate)
#define SEV_LEVEL 2 // process changed MLFQ level (from/to = levels)
