#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  volatile long x = 0;
  long i;

  printf("Medium CPU-bound process started\n");

  for(i = 0; i < 5000000000L; i++)
    x++;

  printf("Medium CPU-bound process finished\n");

  exit(0);
}
