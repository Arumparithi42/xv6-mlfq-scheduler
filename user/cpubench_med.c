// cpubench_med
// Medium CPU-bound workload: 300 ms of CPU time (30 ticks). Long
// enough to use up the Q0 and Q1 quanta (4 + 8 ticks) and reach Q2,
// short enough to finish.

#include "kernel/types.h"
#include "user/user.h"
#include "user/bench.h"

int
main(void)
{
  burn_ms(300);
  exit(0);
}
