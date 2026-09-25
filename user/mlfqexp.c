// mlfqexp <experiment> [repetitions]
//
// Scheduling experiments. Each experiment starts a fixed set of jobs
// (child processes) at fixed arrival times, waits for all of them, and
// prints one line of statistics per job. The first repetition also
// prints the kernel's scheduler event trace, from which queue levels
// over time can be plotted.
//
// Experiments:
//   single  one CPU-bound job (500 ms of CPU)
//   multi   three identical CPU-bound jobs (400 ms each)
//   mixed   two long CPU-bound jobs, one I/O-bound job and two short
//           CPU-bound jobs that arrive while the long jobs run
//   aging   one CPU-bound job (300 ms) competing with a continuous
//           stream of 100 ms jobs for 2 seconds
//
// Each repetition is run by a fresh "driver" child process, so the jobs
// are started by a process that is in Q0 and idle, and all printing is
// done after the jobs have finished.
//
// Output lines (all times in microseconds; t=0 is the creation of the
// first job of the repetition):
//   CONFIG policy=.. quanta=.. aging=.. tick_us=.. ncpu=..
//   BEGIN exp=.. rep=.. driver=<pid>
//   JOB exp=.. rep=.. name=.. pid=.. arrive=.. first=.. end=.. cpu=..
//       wait=.. sleep=.. response=.. turnaround=.. ...
//   EV rep=.. t=.. pid=.. type=S|L from=.. to=.. prio=..
//   END exp=.. rep=..
// EV lines are trace events: type S is a state change (from/to are
// enum procstate: 2 SLEEPING, 3 RUNNABLE, 4 RUNNING, 5 ZOMBIE), type L
// a change of MLFQ level.
//
// Workloads use CPU time as measured by the kernel (see bench.h), so
// a job always does the same amount of work.

#include "kernel/types.h"
#include "user/user.h"
#include "kernel/param.h"
#include "user/bench.h"

#define CPU 0 // compute for ms milliseconds of CPU time
#define IO  1 // iters x (work_us of CPU, then block for one tick)

struct job {
  char *name;
  int kind;
  int ms;        // CPU jobs: CPU time needed
  int iters;     // IO jobs
  int work_us;   // IO jobs: CPU burst per iteration
  int arrive_ms; // arrival time after the start of the repetition
};

static struct job single[] = {
  {"cpu", CPU, 500, 0, 0, 0},
  {0},
};

static struct job multi[] = {
  {"A", CPU, 400, 0, 0, 0},
  {"B", CPU, 400, 0, 0, 0},
  {"C", CPU, 400, 0, 0, 0},
  {0},
};

static struct job mixed[] = {
  {"long1", CPU, 1000, 0, 0, 0},  {"long2", CPU, 1000, 0, 0, 0},
  {"io", IO, 0, 100, 1000, 0},    {"short1", CPU, 30, 0, 0, 300},
  {"short2", CPU, 30, 0, 0, 700}, {0},
};

// "aging": the long job starts together with two stream jobs; whenever
// a stream job finishes (and the stream is still on) a new one starts.
static struct job aging_long = {"long", CPU, 300, 0, 0, 0};
static struct job aging_stream = {"stream", CPU, 100, 0, 0, 0};
#define STREAM_JOBS 2    // stream jobs running at any time
#define STREAM_MS   2000 // how long new stream jobs keep arriving

#define MAXJOBS 64

// A finished job, sent from the repetition driver to the main process.
struct result {
  int job; // index into the job table (aging: 0 = long, 1 = stream)
  struct procstat st;
};

static struct job *jobtab; // job table of the current experiment
static int tick_ms;
static int pids[MAXJOBS]; // pids of the jobs started so far
static int njobs;

static void
work(struct job *j)
{
  if (j->kind == CPU) {
    burn_ms(j->ms);
  } else {
    for (int i = 0; i < j->iters; i++) {
      burn_us(j->work_us);
      pause(1);
    }
  }
  exit(0);
}

static void
start(struct job *j)
{
  int pid = fork();
  if (pid < 0) {
    printf("mlfqexp: fork failed\n");
    exit(1);
  }
  if (pid == 0)
    work(j);
  pids[njobs++] = pid;
}

// Index of the job with this pid in the job table.
static int
job_index(int pid, struct job *jobs)
{
  for (int i = 0; i < njobs; i++)
    if (pids[i] == pid)
      return jobs == 0 ? (i == 0 ? 0 : 1) : i;
  return -1;
}

// Wait for a job and send its statistics to the main process.
static int
collect(int fd, struct job *jobs)
{
  struct result r;
  int status;
  int pid = waitstat(&status, &r.st);

  if (pid > 0) {
    r.job = job_index(pid, jobs);
    write(fd, &r, sizeof(r));
  }
  return pid;
}

// The repetition driver: a fresh process (so it starts in Q0 with a
// full quantum, whatever the main process did before) that starts the
// jobs at their arrival times and reports their statistics on fd. It
// does almost no work itself, so it does not disturb the measurement.
static void
driver(int fd, struct job *jobs)
{
  njobs = 0;
  if (jobs) {
    int t_start = uptime();
    for (struct job *j = jobs; j->name; j++) {
      int due = t_start + j->arrive_ms / tick_ms;
      if (uptime() < due)
        pause(due - uptime());
      start(j);
    }
    while (collect(fd, jobs) > 0)
      ;
  } else {
    // aging: keep STREAM_JOBS stream jobs running for STREAM_MS.
    int stop = uptime() + STREAM_MS / tick_ms;
    int pid;
    start(&aging_long);
    for (int i = 0; i < STREAM_JOBS; i++)
      start(&aging_stream);
    while ((pid = collect(fd, 0)) > 0)
      if (pid != pids[0] && uptime() < stop && njobs < MAXJOBS)
        start(&aging_stream);
  }
  exit(0);
}

static char *
job_name(int i)
{
  if (jobtab == 0)
    return i == 0 ? aging_long.name : aging_stream.name;
  return i >= 0 ? jobtab[i].name : "?";
}

static void
print_job(char *exp, int rep, uint64 t0, struct result *r)
{
  struct procstat *st = &r->st;
  printf("JOB exp=%s rep=%d name=%s pid=%d arrive=%lu first=%lu end=%lu "
         "cpu=%lu wait=%lu sleep=%lu response=%lu turnaround=%lu "
         "maxwait=%lu dispatch=%d expire=%d preempt=%d nsleep=%d "
         "demote=%d promote=%d prio=%d q0=%lu q1=%lu q2=%lu\n",
         exp, rep, job_name(r->job), st->pid, st->ctime - t0,
         st->first_run - t0, st->etime - t0, st->cpu_time, st->wait_time,
         st->sleep_time, st->first_run - st->ctime, st->etime - st->ctime,
         st->max_wait, st->ndispatch, st->nexpire, st->npreempt, st->nsleep,
         st->ndemote, st->npromote, st->priority, st->level_time[0],
         st->level_time[1], st->level_time[2]);
}

static void
print_trace(int rep, uint64 t0, struct schedevent *ev, int n)
{
  for (int i = 0; i < n; i++) {
    struct schedevent *e = &ev[i];
    if (e->time < t0)
      continue;
    printf("EV rep=%d t=%lu pid=%d type=%c from=%d to=%d prio=%d\n", rep,
           e->time - t0, e->pid, e->type == SEV_LEVEL ? 'L' : 'S', e->from,
           e->to, e->prio);
  }
}

static struct result results[MAXJOBS];

// Read exactly n bytes (a pipe read may return less). Returns 0 at EOF.
static int
readfull(int fd, void *buf, int n)
{
  char *p = buf;
  int got = 0, r;
  while (got < n && (r = read(fd, p + got, n - got)) > 0)
    got += r;
  return got == n;
}

// Run one repetition and print its results.
static void
run(char *exp, int rep, int with_trace)
{
  int fd[2], n = 0, drv;
  uint64 t0 = 0;

  if (pipe(fd) < 0) {
    printf("mlfqexp: pipe failed\n");
    exit(1);
  }
  schedtrace(1, 0, 0);
  if ((drv = fork()) == 0) {
    close(fd[0]);
    driver(fd[1], jobtab);
  }
  close(fd[1]);
  while (n < MAXJOBS && readfull(fd[0], &results[n], sizeof(results[n])))
    n++;
  close(fd[0]);
  wait(0);

  // Read the trace into memory that exists only while printing, so
  // later repetitions do not have to copy it when they fork.
  int size = NSCHEDTRACE * sizeof(struct schedevent);
  struct schedevent *ev = (struct schedevent *)sbrk(size);
  int nev = schedtrace(0, ev, NSCHEDTRACE);

  // t = 0 is the creation of the first job.
  for (int i = 0; i < n; i++)
    if (t0 == 0 || results[i].st.ctime < t0)
      t0 = results[i].st.ctime;
  printf("BEGIN exp=%s rep=%d driver=%d\n", exp, rep, drv);
  for (int i = 0; i < n; i++)
    print_job(exp, rep, t0, &results[i]);
  if (with_trace) {
    if (nev == NSCHEDTRACE)
      printf("WARNING trace buffer full, later events lost\n");
    print_trace(rep, t0, ev, nev);
  }
  printf("END exp=%s rep=%d\n", exp, rep);
  sbrk(-size);
}

int
main(int argc, char *argv[])
{
  struct schedinfo si;
  char *exp = argc > 1 ? argv[1] : "";
  int reps = argc > 2 ? atoi(argv[2]) : 1;

  if (strcmp(exp, "single") == 0)
    jobtab = single;
  else if (strcmp(exp, "multi") == 0)
    jobtab = multi;
  else if (strcmp(exp, "mixed") == 0)
    jobtab = mixed;
  else if (strcmp(exp, "aging") != 0) {
    fprintf(2, "usage: mlfqexp single|multi|mixed|aging [repetitions]\n");
    exit(1);
  }

  schedinfo(&si);
  tick_ms = si.tick_us / 1000;
  printf("CONFIG policy=%s quanta=%d,%d,%d aging=%d tick_us=%d ncpu=%d\n",
         si.policy == SCHED_POLICY_MLFQ ? "MLFQ" : "RR", si.quantum[0],
         si.quantum[1], si.quantum[2], si.aging, si.tick_us, si.ncpu);

  for (int rep = 1; rep <= reps; rep++) {
    pause(10); // let console output drain before measuring
    run(exp, rep, rep == 1);
  }
  exit(0);
}
