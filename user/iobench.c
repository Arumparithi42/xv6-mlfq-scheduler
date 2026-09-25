// iobench [iterations] [work_us]
// I/O-bound workload: each iteration does a short CPU burst (work_us
// microseconds, default 500) and then blocks for one clock tick, as a
// process waiting for a device would. Default: 50 iterations.

#include "kernel/types.h"
#include "user/user.h"
#include "user/bench.h"

int
main(int argc, char *argv[])
{
  int n = argc > 1 ? atoi(argv[1]) : 50;
  int work = argc > 2 ? atoi(argv[2]) : 500;

  for (int i = 0; i < n; i++) {
    burn_us(work);
    pause(1); // block until the next clock tick
  }
  exit(0);
}
