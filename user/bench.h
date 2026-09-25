// Helpers shared by the scheduler benchmark and test programs.
// Include after "kernel/types.h" and "user/user.h".
//
// Workloads are defined by the amount of CPU time they need (as
// measured by the kernel), not by a number of loop iterations. The
// same workload therefore does the same amount of work whatever the
// host speed or the competition from other processes, which keeps
// experiments repeatable.

#include "kernel/pstat.h"

#define SPIN_CHUNK 2000 // loop iterations between CPU-time checks

// Busy loop of n iterations.
static inline void
spin(int n)
{
  volatile int x = 0;
  for (int i = 0; i < n; i++)
    x++;
}

// CPU time used so far by the calling process, in microseconds.
static inline uint64
cputime(void)
{
  struct procstat st;
  if (getprocstat(0, &st) < 0)
    return 0;
  return st.cpu_time;
}

// Consume us microseconds of CPU time.
static inline void
burn_us(uint64 us)
{
  uint64 end = cputime() + us;
  while (cputime() < end)
    spin(SPIN_CHUNK);
}

// Consume ms milliseconds of CPU time.
static inline void
burn_ms(int ms)
{
  burn_us((uint64)ms * 1000);
}

// Print microseconds as milliseconds with one decimal.
static inline void
print_ms(uint64 us)
{
  printf("%lu.%lu", us / 1000, (us % 1000) / 100);
}

// Print the main metrics of a finished process on one line.
static inline void
print_stat(struct procstat *st)
{
  printf("pid %d %s: Q%d cpu=", st->pid, st->name, st->priority);
  print_ms(st->cpu_time);
  printf(" wait=");
  print_ms(st->wait_time);
  printf(" sleep=");
  print_ms(st->sleep_time);
  printf(" response=");
  print_ms(st->first_run - st->ctime);
  printf(" turnaround=");
  print_ms(st->etime - st->ctime);
  printf(" ms  dispatch=%d demote=%d promote=%d\n", st->ndispatch, st->ndemote,
         st->npromote);
}
