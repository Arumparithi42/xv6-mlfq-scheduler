// schedtime [-n copies] command [args...]
// Run a command (optionally several concurrent copies of it) and print
// the scheduling statistics of each copy when it exits, like the Unix
// time command. Example: schedtime -n 3 cpubench_med

#include "kernel/types.h"
#include "user/user.h"
#include "user/bench.h"

int
main(int argc, char *argv[])
{
  int copies = 1;
  char **cmd = argv + 1;

  if (argc > 2 && strcmp(argv[1], "-n") == 0) {
    copies = atoi(argv[2]);
    cmd = argv + 3;
  }
  if (*cmd == 0 || copies < 1) {
    fprintf(2, "usage: schedtime [-n copies] command [args...]\n");
    exit(1);
  }

  for (int i = 0; i < copies; i++) {
    int pid = fork();
    if (pid < 0) {
      fprintf(2, "schedtime: fork failed\n");
      exit(1);
    }
    if (pid == 0) {
      exec(cmd[0], cmd);
      fprintf(2, "schedtime: exec %s failed\n", cmd[0]);
      exit(1);
    }
  }

  struct procstat st;
  int status;
  while (waitstat(&status, &st) > 0)
    print_stat(&st);
  exit(0);
}
