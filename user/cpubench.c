#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  volatile int x = 0;

  printf("CPU-bound process started\n");

  for(;;)
    x++;

  exit(0);
}
