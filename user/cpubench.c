// cpubench [ms]
// CPU-bound workload: computes without ever blocking. Runs until it
// has used ms milliseconds of CPU time, or forever if ms is omitted.
// Use "schedtime cpubench 500" to see its scheduling statistics, or
// press Ctrl-P while it runs to watch its queue level.

#include "kernel/types.h"
#include "user/user.h"
#include "user/bench.h"

int
main(int argc, char *argv[])
{
  int ms = argc > 1 ? atoi(argv[1]) : 0;

  if (ms <= 0) {
    for (;;)
      spin(SPIN_CHUNK);
  }
  burn_ms(ms);
  exit(0);
}
