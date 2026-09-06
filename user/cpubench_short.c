#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  volatile int x = 0;
  int i;

  printf("Short CPU-bound process started\n");

  for(i = 0; i < 1000000000; i++)
    x++;

  printf("Short CPU-bound process finished\n");

  exit(0);
}
