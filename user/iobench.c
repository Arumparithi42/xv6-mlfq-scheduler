#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  int i;

  printf("I/O-bound process started\n");

  for(i = 0; i < 20; i++) {
    printf("I/O process: iteration %d\n", i);
    pause(5);
  }

  printf("I/O-bound process finished\n");

  exit(0);
}
