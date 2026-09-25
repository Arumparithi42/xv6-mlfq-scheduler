// schedtest: functional tests of the scheduler and its statistics.
//
// Checks that fork/exit/wait/sleep/wakeup/kill still work, that the
// statistics are internally consistent, and (for the MLFQ kernel)
// demotion, I/O-bound behaviour, priority preemption, round robin
// within a level, aging and resistance to gaming.
//
// Run it alone on an otherwise idle system with one CPU (the default
// "make qemu"); several tests compare timings against the quanta.

#include "kernel/types.h"
#include "user/user.h"
#include "user/bench.h"

static struct schedinfo si;
static int failures;
static int tick; // microseconds per tick

#define TICKS(n) ((uint64)(n) * tick)

static void
fail(char *test, char *why)
{
  printf("  FAILED %s: %s\n", test, why);
  failures++;
}

static void
check(int ok, char *test, char *why)
{
  if (!ok)
    fail(test, why);
}

static uint64
absdiff(uint64 a, uint64 b)
{
  return a > b ? a - b : b - a;
}

// Statistics every finished process must satisfy.
static void
check_consistent(char *test, struct procstat *st)
{
  uint64 tat = st->etime - st->ctime;
  uint64 sum = st->cpu_time + st->wait_time + st->sleep_time;
  uint64 lv = 0;

  for (int i = 0; i < si.nqueue; i++)
    lv += st->level_time[i];
  check(st->ctime > 0 && st->first_run >= st->ctime, test, "first_run < ctime");
  check(st->etime >= st->first_run, test, "etime < first_run");
  // Every moment between creation and exit is in exactly one bucket
  // (allow 1 us for rounding to microseconds).
  check(absdiff(sum, tat) <= 3, test, "cpu + wait + sleep != turnaround");
  // Each value is rounded down to whole microseconds separately.
  check(absdiff(lv, st->cpu_time) <= 3, test, "level times != cpu time");
  check(st->ndispatch > 0, test, "never dispatched");
}

// Fork a child that runs fn(arg) and exits; return its pid.
static int
spawn(void (*fn)(int), int arg)
{
  int pid = fork();
  if (pid < 0) {
    printf("schedtest: fork failed\n");
    exit(1);
  }
  if (pid == 0) {
    fn(arg);
    exit(0);
  }
  return pid;
}

// Wait for a specific child and return its statistics and status.
// Other children that exit meanwhile are discarded, so use this only
// when pid is the only child that can exit.
static int
reap(int pid, struct procstat *st)
{
  int status, got;
  while ((got = waitstat(&status, st)) >= 0)
    if (got == pid)
      return status;
  printf("schedtest: lost child %d\n", pid);
  exit(1);
}

// ---- workloads ----

static void
w_burn(int ms)
{
  burn_ms(ms);
}

static void
w_io(int iters)
{
  for (int i = 0; i < iters; i++) {
    burn_us(200);
    pause(1);
  }
}

static void
w_sleep(int ticks)
{
  pause(ticks);
}

static void
w_exitprio(int unused)
{
  struct procstat st;
  getprocstat(0, &st);
  exit(st.priority);
}

// Use almost a whole Q0 quantum, then sleep, over and over.
static void
w_gamer(int rounds)
{
  for (int i = 0; i < rounds; i++) {
    burn_us(TICKS(si.quantum[0]) - tick / 2 - 1000);
    pause(1);
  }
}

// ---- tests ----

static void
test_fork_wait(void)
{
  char *t = "fork/exit/wait";
  int pids[8];
  struct procstat st;
  int status;

  for (int i = 0; i < 8; i++) {
    pids[i] = fork();
    if (pids[i] == 0) {
      burn_ms(5);
      exit(i);
    }
  }
  for (int n = 0; n < 8; n++) {
    int pid = waitstat(&status, &st);
    int i;
    for (i = 0; i < 8 && pids[i] != pid; i++)
      ;
    check(i < 8, t, "waitstat returned an unknown pid");
    check(i < 8 && status == i, t, "wrong exit status");
    check(st.pid == pid, t, "stat pid mismatch");
    check(st.cpu_time >= 5000, t, "cpu time too small");
    check_consistent(t, &st);
  }
  check(wait(0) == -1, t, "wait with no children should fail");
}

static void
test_many(void)
{
  char *t = "many processes";
  struct procstat st;
  int n = 0, status;

  for (int i = 0; i < 20; i++)
    spawn(w_burn, 20);
  while (waitstat(&status, &st) > 0) {
    check_consistent(t, &st);
    n++;
  }
  check(n == 20, t, "not all children reaped");
}

static void
test_new_q0(void)
{
  char *t = "new process in Q0";
  struct procstat st;
  int pid = spawn(w_exitprio, 0);
  check(reap(pid, &st) == 0, t, "child did not start in Q0");
}

static void
test_sleep(void)
{
  char *t = "sleep/wakeup";
  struct procstat st;
  int pid = spawn(w_sleep, 20);

  reap(pid, &st);
  check_consistent(t, &st);
  check(st.nsleep >= 1, t, "no sleep recorded");
  check(absdiff(st.sleep_time, TICKS(20)) <= TICKS(1), t,
        "sleep time is not 20 ticks");
  check(st.cpu_time < TICKS(1), t, "sleeping process used CPU");
}

static void
test_kill(void)
{
  char *t = "kill";
  struct procstat st;
  int hog = spawn(w_burn, 100000);
  int sleeper = spawn(w_sleep, 100000);

  int status, n = 0;

  pause(5);
  check(kill(hog) == 0 && kill(sleeper) == 0, t, "kill failed");
  while (waitstat(&status, &st) > 0) {
    check(status == -1, t, "killed child exit status != -1");
    check(st.pid == hog || st.pid == sleeper, t, "unexpected child");
    check_consistent(t, &st);
    n++;
  }
  check(n == 2, t, "did not reap both killed children");
}

static void
test_demotion(void)
{
  char *t = "CPU-bound demotion";
  struct procstat st;
  int q0 = si.quantum[0], q1 = si.quantum[1], q2 = si.quantum[2];
  int pid = spawn(w_burn, (q0 + q1 + q2 / 2) * tick / 1000);

  reap(pid, &st);
  check_consistent(t, &st);
  check(st.priority == 2, t, "did not reach Q2");
  check(st.ndemote == 2, t, "expected exactly 2 demotions");
  check(absdiff(st.level_time[0], TICKS(q0)) <= TICKS(1), t,
        "time in Q0 != Q0 quantum");
  check(absdiff(st.level_time[1], TICKS(q1)) <= TICKS(1), t,
        "time in Q1 != Q1 quantum");
}

static void
test_io_bound(void)
{
  char *t = "I/O-bound stays in Q0";
  struct procstat st;
  int pid = spawn(w_io, 30);

  reap(pid, &st);
  check_consistent(t, &st);
  check(st.priority == 0 && st.ndemote == 0, t, "I/O-bound was demoted");
  check(st.nsleep >= 30, t, "expected >= 30 sleeps");
}

static void
test_gaming(void)
{
  char *t = "no gaming by sleeping";
  struct procstat st;
  // Enough rounds to use the Q0 and Q1 quanta several times over.
  int pid =
    spawn(w_gamer, 3 * (si.quantum[0] + si.quantum[1]) / (si.quantum[0] - 1));

  reap(pid, &st);
  check_consistent(t, &st);
  check(st.priority == 2, t, "sleeping before quantum end avoided demotion");
}

static void
test_preemption(void)
{
  char *t = "higher level preempts";
  struct procstat st, hogst;
  int hog = spawn(w_burn, 100000);

  pause(si.quantum[0] + si.quantum[1] + 2); // hog is now in Q2
  int io = spawn(w_io, 20);
  reap(io, &st);
  kill(hog);
  reap(hog, &hogst);

  check_consistent(t, &st);
  check(hogst.priority == 2, t, "hog was not in Q2");
  check(st.priority == 0, t, "I/O child left Q0");
  // The I/O child should get the CPU at the tick it wakes up, not
  // wait for the rest of the Q2 hog's quantum.
  check(st.max_wait < TICKS(1), t, "I/O child waited >= 1 tick for CPU");
  check(hogst.npreempt >= 10, t, "hog was not preempted by I/O child");
}

static void
test_round_robin(void)
{
  char *t = "round robin within a level";
  struct procstat st[3];
  int ms = 40 * tick / 1000;

  int status;

  for (int i = 0; i < 3; i++)
    spawn(w_burn, ms);
  for (int i = 0; i < 3; i++)
    waitstat(&status, &st[i]);
  uint64 lo = st[0].etime, hi = st[0].etime;
  for (int i = 0; i < 3; i++) {
    check_consistent(t, &st[i]);
    if (st[i].etime < lo)
      lo = st[i].etime;
    if (st[i].etime > hi)
      hi = st[i].etime;
    check(st[i].ndispatch >= 3, t, "a process was not interleaved");
  }
  // In FIFO order the three finish within about one Q2 quantum of
  // each other each; a starved process would finish much later.
  check(hi - lo <= TICKS(2 * si.quantum[2] + 2), t,
        "completion times too far apart (unfair)");
}

// Runs in a fresh child so that the process starting the stream jobs
// is itself in Q0, whatever the test program did before.
static void
aging_driver(int unused)
{
  char *t = "aging prevents starvation";
  struct procstat st;
  int status;
  int q0 = si.quantum[0], q1 = si.quantum[1];
  // Stream jobs finish before leaving Q1, so they keep Q0/Q1 busy.
  int stream_ms = (q0 + q1 - 2) * tick / 1000;
  int end = uptime() + 4 * si.aging;
  int longpid = spawn(w_burn, (q0 + q1 + si.aging) * tick / 1000);
  int longdone = 0;

  spawn(w_burn, stream_ms);
  spawn(w_burn, stream_ms);
  for (;;) {
    int pid = waitstat(&status, &st);
    if (pid < 0)
      break;
    if (pid == longpid) {
      longdone = 1;
      check_consistent(t, &st);
      check(st.npromote >= 1, t, "long job never promoted");
      // Without aging it would wait for the whole stream (4 x aging).
      check(st.max_wait < TICKS(2 * si.aging), t,
            "long job waited more than 2x the aging threshold");
    }
    if (uptime() < end)
      spawn(w_burn, stream_ms);
  }
  check(longdone, t, "long job not reaped");
  exit(failures);
}

static void
test_aging(void)
{
  struct procstat st;
  failures += reap(spawn(aging_driver, 0), &st);
}

// Burn into Q2, then block (in wait(), which has no spurious wakeups,
// unlike pause() whose sleepers wake on every tick) for longer than
// the aging threshold; exit with the resulting level.
static void
w_idle_then_level(int unused)
{
  struct procstat st;
  burn_us(TICKS(si.quantum[0] + si.quantum[1] + 1));
  reap(spawn(w_sleep, si.aging + 5), &st);
  getprocstat(0, &st);
  exit(st.priority);
}

static void
test_idle_promotion(void)
{
  char *t = "long sleep promotes";
  struct procstat st;
  int level = reap(spawn(w_idle_then_level, 0), &st);
  check(st.ndemote == 2, t, "child did not reach Q2");
  check(level == 1, t, "not promoted to Q1 after sleeping > aging");
}

static void
test_trace(void)
{
  char *t = "scheduler trace";
  static struct schedevent ev[512];
  struct procstat st;
  int q0 = si.quantum[0], q1 = si.quantum[1];
  int down01 = 0, down12 = 0, runs = 0;

  schedtrace(1, 0, 0);
  int pid = spawn(w_burn, (q0 + q1 + 2) * tick / 1000);
  reap(pid, &st);
  int n = schedtrace(0, ev, 512);
  check(n > 0, t, "no events recorded");
  for (int i = 0; i < n; i++) {
    if (ev[i].pid != pid)
      continue;
    if (ev[i].type == SEV_LEVEL && ev[i].from == 0 && ev[i].to == 1)
      down01 = 1;
    if (ev[i].type == SEV_LEVEL && ev[i].from == 1 && ev[i].to == 2)
      down12 = down01;
    if (ev[i].type == SEV_STATE && ev[i].to == 4) // RUNNING
      runs++;
    check(i == 0 || ev[i].time >= ev[i - 1].time, t, "events out of order");
  }
  check(down01 && down12, t, "missing Q0->Q1->Q2 events");
  check(runs == st.ndispatch, t, "RUNNING events != dispatch count");
}

static void
run(char *name, void (*fn)(void))
{
  int before = failures;
  printf("test %s: ", name);
  fn();
  if (failures == before)
    printf("OK\n");
}

int
main(void)
{
  if (schedinfo(&si) < 0) {
    printf("schedtest: schedinfo failed\n");
    exit(1);
  }
  tick = si.tick_us;
  printf("schedtest: policy %s, %d CPU(s), tick %d us",
         si.policy == SCHED_POLICY_MLFQ ? "MLFQ" : "RR", si.ncpu, tick);
  if (si.policy == SCHED_POLICY_MLFQ)
    printf(", quanta %d/%d/%d, aging %d", si.quantum[0], si.quantum[1],
           si.quantum[2], si.aging);
  printf("\n");

  run("fork_wait", test_fork_wait);
  run("many", test_many);
  run("sleep", test_sleep);
  run("kill", test_kill);
  run("new_q0", test_new_q0);
  if (si.policy == SCHED_POLICY_MLFQ) {
    run("demotion", test_demotion);
    run("io_bound", test_io_bound);
    run("gaming", test_gaming);
    run("trace", test_trace);
    if (si.ncpu == 1) {
      // These compare timings that only hold with a single CPU.
      run("preemption", test_preemption);
      run("round_robin", test_round_robin);
      if (si.aging > 0) {
        run("aging", test_aging);
        run("idle_promotion", test_idle_promotion);
      }
    }
  }

  if (failures == 0)
    printf("schedtest: ALL TESTS PASSED\n");
  else
    printf("schedtest: %d CHECKS FAILED\n", failures);
  exit(failures != 0);
}
