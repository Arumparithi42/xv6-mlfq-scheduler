// cpubench_short
// Short CPU-bound workload: 30 ms of CPU time (3 ticks). It finishes
// within the Q0 quantum (4 ticks), so under MLFQ it is never demoted
// and runs ahead of any long-running CPU-bound process.

#include "kernel/types.h"
#include "user/user.h"
#include "user/bench.h"

int
main(void)
{
  burn_ms(30);
  exit(0);
}
