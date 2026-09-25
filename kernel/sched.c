// Scheduling policy: Multilevel Feedback Queue (MLFQ) and the
// per-process statistics used to evaluate it.
//
// proc.c keeps the mechanism of the original xv6 scheduler (the
// per-CPU scheduler() loop, sched(), yield(), sleep(), wakeup()). This
// file supplies the policy those functions call into:
//
//   proc_setstate()  every process state change goes through here, so
//                    that time spent in each state is accounted exactly.
//   sched_select()   which RUNNABLE process runs next (MLFQ).
//   sched_tick()     on a timer interrupt, should the running process
//                    give up the CPU?
//
// MLFQ policy (see README.md for the full discussion):
//
//   1. There are NQUEUE levels; Q0 has the highest priority.
//   2. A new process starts in Q0.
//   3. The scheduler runs a process from the highest non-empty level;
//      within a level, processes run in FIFO (round-robin) order.
//   4. A process may run for the quantum of its level (Q0_QUANTUM,
//      Q1_QUANTUM, Q2_QUANTUM ticks). The quantum is used up by CPU
//      time only and is NOT reset when the process sleeps, so a process
//      cannot keep a high priority by sleeping just before its quantum
//      expires.
//   5. When the quantum is used up, the process moves one level down
//      (the lowest level just starts a new quantum) and goes to the
//      tail of its new queue.
//   6. A process is also preempted, before its quantum ends, when a
//      process of a higher level becomes RUNNABLE. It keeps its place
//      at the head of its queue and the rest of its quantum.
//   7. Aging: a process that has not run for AGING_THRESHOLD ticks
//      moves one level up (one more level for every further
//      AGING_THRESHOLD ticks). If it was RUNNABLE all that time this
//      prevents starvation; if it was SLEEPING it lets a long-lived
//      interactive process (such as the shell) that was demoted
//      earlier become interactive again. Sleeping briefly, as a
//      process trying to game rule 4 would, does not help.
//
// The "queues" are not separate linked lists. A process's queue is
// p->priority and its position in the queue is p->qseq, a global
// sequence number taken when it joins the tail of a queue. Picking the
// RUNNABLE process with the smallest (priority, qseq) is therefore the
// same as taking the head of the highest non-empty FIFO queue, while
// the process table remains the only data structure, exactly as in the
// original xv6.
//
// Locking: all scheduling fields of a process are protected by p->lock,
// like p->state. No function here holds two process locks at once, so
// the original xv6 lock order is unchanged. The trace lock is only ever
// acquired while holding at most one p->lock and is never held while
// acquiring a p->lock.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "pstat.h"

extern struct proc proc[NPROC];

#define CYCLES_PER_US (TIMEBASE_HZ / 1000000)

static uint64 qseq_next; // source of queue positions; atomic
static int cpus_online;  // CPUs that entered scheduler(); atomic

static struct {
  struct spinlock lock;
  int enabled;
  int n;
  struct schedevent ev[NSCHEDTRACE];
} trace;

void
schedinit(void)
{
  initlock(&trace.lock, "schedtrace");
}

void
sched_cpu_online(void)
{
  __atomic_fetch_add(&cpus_online, 1, __ATOMIC_RELAXED);
}

// Record a scheduler event if tracing is on. Caller holds p->lock.
static void
trace_event(struct proc *p, int type, int from, int to)
{
  if (__atomic_load_n(&trace.enabled, __ATOMIC_RELAXED) == 0)
    return;
  acquire(&trace.lock);
  if (trace.enabled && trace.n < NSCHEDTRACE) {
    struct schedevent *e = &trace.ev[trace.n++];
    e->time = r_time() / CYCLES_PER_US;
    e->pid = p->pid;
    e->type = type;
    e->from = from;
    e->to = to;
    e->prio = p->priority;
    e->cpu = cpuid();
  }
  release(&trace.lock);
}

// Position at the tail of a queue.
static uint64
tail(void)
{
  return __atomic_add_fetch(&qseq_next, 1, __ATOMIC_RELAXED);
}

// Charge the time since p->state_since to p's current state.
// Caller holds p->lock.
static void
account(struct proc *p, uint64 now)
{
  uint64 d = now - p->state_since;

  switch (p->state) {
  case RUNNING:
    p->cpu_time += d;
    p->quantum_used += d;
    p->level_time[p->priority] += d;
    break;
  case RUNNABLE:
    p->wait_time += d;
    break;
  case SLEEPING:
    p->sleep_time += d;
    break;
  default:
    break;
  }
  p->state_since = now;
}

// Initialize the scheduling state of a newly allocated process.
// Caller holds p->lock.
void
sched_procinit(struct proc *p)
{
  uint64 now = r_time();

  p->priority = 0; // new processes start in the highest level
  p->quantum_used = 0;
  p->qseq = 0;
  p->age_since = now;
  p->state_since = now;
  p->ctime = now;
  p->first_run = 0;
  p->etime = 0;
  p->cpu_time = 0;
  p->wait_time = 0;
  p->sleep_time = 0;
  for (int i = 0; i < NQUEUE; i++)
    p->level_time[i] = 0;
  p->max_wait = 0;
  p->ndispatch = 0;
  p->nexpire = 0;
  p->npreempt = 0;
  p->nsleep = 0;
  p->ndemote = 0;
  p->npromote = 0;
}

// Change p's state. Every state change of a live process goes through
// here so that CPU, waiting and sleeping time are accounted exactly.
// Caller holds p->lock.
void
proc_setstate(struct proc *p, int s)
{
  uint64 now = r_time();
  enum procstate old = p->state;

  if (old == RUNNABLE && now - p->state_since > p->max_wait)
    p->max_wait = now - p->state_since;
  account(p, now);
  p->state = s;
  if (old == RUNNING)
    p->age_since = now; // aging counts from when it last ran

  switch (s) {
  case RUNNABLE:
    // The process "arrives" when it first becomes RUNNABLE, i.e. when
    // fork() has finished setting it up. Measuring creation time from
    // here (rather than from allocproc()) makes
    // cpu_time + wait_time + sleep_time == turnaround exact.
    if (old == USED)
      p->ctime = p->age_since = now;
    // A process that was just created or woken up joins the tail of
    // its queue. A process leaving RUNNING keeps its position unless
    // sched_tick() moved it to the tail because its quantum expired.
    if (old != RUNNING)
      p->qseq = tail();
    break;
  case RUNNING:
    p->ndispatch++;
    if (p->first_run == 0)
      p->first_run = now;
    break;
  case SLEEPING:
    p->nsleep++;
    break;
  case ZOMBIE:
    p->etime = now;
    break;
  default:
    break;
  }
  trace_event(p, SEV_STATE, old, s);
}

#ifndef SCHED_RR

// Quantum of a level, in timebase cycles.
static uint64
quantum(int level)
{
  static const int q[NQUEUE] = {Q0_QUANTUM, Q1_QUANTUM, Q2_QUANTUM};
  return (uint64)q[level] * TICK_CYCLES;
}

// Move p to another level with a fresh quantum. Caller holds p->lock.
static void
set_level(struct proc *p, int level)
{
  trace_event(p, SEV_LEVEL, p->priority, level);
  if (level > p->priority)
    p->ndemote++;
  else
    p->npromote++;
  p->priority = level;
  p->quantum_used = 0;
}

// Aging: move p up one level for every AGING_THRESHOLD ticks since
// it last ran (or was last promoted). Called for RUNNABLE processes
// only; a process that slept is caught up when it becomes RUNNABLE.
// Caller holds p->lock.
static void
age(struct proc *p, uint64 now)
{
#if AGING_THRESHOLD > 0
  uint64 threshold = (uint64)AGING_THRESHOLD * TICK_CYCLES;

  if (p->priority > 0 && now - p->age_since >= threshold) {
    uint64 levels = (now - p->age_since) / threshold;
    while (levels-- > 0 && p->priority > 0)
      set_level(p, p->priority - 1);
    p->qseq = tail();   // join the tail of the new queue
    p->age_since = now; // must wait again before the next promotion
  }
#endif
}

// Choose the next process to run: the RUNNABLE process with the
// highest priority (lowest level), and within that level the one that
// has been in the queue longest. Aging is applied to every RUNNABLE
// process during the same scan (and also on every tick, see
// sched_tick()), so a promotion is never more than a tick late.
//
// Like the original scheduler, the scan holds one p->lock at a time.
// Because the chosen process is unlocked for a moment before it is
// locked again, another CPU may have taken it in between; in that case
// the scan is repeated.
//
// Returns the chosen process with p->lock held, or 0 if no process is
// RUNNABLE.
struct proc *
sched_select(void)
{
  struct proc *p, *best;
  int best_prio = 0;
  uint64 best_seq = 0;

  for (;;) {
    uint64 now = r_time();

    best = 0;
    for (p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if (p->state == RUNNABLE) {
        age(p, now);
        if (best == 0 || p->priority < best_prio ||
            (p->priority == best_prio && p->qseq < best_seq)) {
          best = p;
          best_prio = p->priority;
          best_seq = p->qseq;
        }
      }
      release(&p->lock);
    }

    if (best == 0)
      return 0;
    acquire(&best->lock);
    if (best->state == RUNNABLE)
      return best;
    release(&best->lock); // another CPU took it; look again
  }
}

// Called once per tick: apply aging to every RUNNABLE process, and
// report whether one of them is at a level above prio (the level of
// the running process self).
// Must not be called with any p->lock held.
static int
higher_runnable(struct proc *self, int prio)
{
  struct proc *p;
  uint64 now = r_time();
  int found = 0;

  for (p = proc; p < &proc[NPROC]; p++) {
    if (p == self)
      continue;
    acquire(&p->lock);
    if (p->state == RUNNABLE) {
      age(p, now);
      if (p->priority < prio)
        found = 1;
    }
    release(&p->lock);
  }
  return found;
}

#endif // !SCHED_RR

// Called from usertrap() and kerneltrap() on every timer interrupt
// while a process is running on this CPU. Returns 1 if the process
// should give up the CPU by calling yield().
int
sched_tick(void)
{
#ifdef SCHED_RR
  // Original xv6: switch to the next process on every timer tick.
  return 1;
#else
  struct proc *p = myproc();
  int prio;

  acquire(&p->lock);
  account(p, r_time());

  // The quantum is measured exactly but can only be checked here, once
  // per tick; count it as used up if less than half a tick remains.
  if (p->quantum_used + TICK_CYCLES / 2 >= quantum(p->priority)) {
    p->nexpire++;
    if (p->priority < NQUEUE - 1)
      set_level(p, p->priority + 1); // demote
    else
      p->quantum_used = 0; // lowest level: start a new quantum
    p->qseq = tail();      // round robin within the level
    release(&p->lock);
    return 1;
  }
  prio = p->priority;
  release(&p->lock);

  if (higher_runnable(p, prio)) {
    acquire(&p->lock);
    p->npreempt++;
    release(&p->lock);
    return 1;
  }
  return 0;
#endif
}

// Copy p's statistics, including the time spent so far in its current
// state, into *st. Caller holds p->lock.
void
proc_getstat(struct proc *p, struct procstat *st)
{
  uint64 now = r_time();
  uint64 d = now - p->state_since;
  uint64 cpu = p->cpu_time, wait = p->wait_time, slp = p->sleep_time;
  uint64 maxw = p->max_wait;

  memset(st, 0, sizeof(*st));
  if (p->state == RUNNING)
    cpu += d;
  else if (p->state == RUNNABLE) {
    wait += d;
    if (d > maxw)
      maxw = d;
  } else if (p->state == SLEEPING)
    slp += d;

  st->pid = p->pid;
  st->state = p->state;
  st->priority = p->priority;
  safestrcpy(st->name, p->name, sizeof(st->name));
  st->ctime = p->ctime / CYCLES_PER_US;
  st->first_run = p->first_run / CYCLES_PER_US;
  st->etime = p->etime / CYCLES_PER_US;
  st->cpu_time = cpu / CYCLES_PER_US;
  st->wait_time = wait / CYCLES_PER_US;
  st->sleep_time = slp / CYCLES_PER_US;
  st->quantum_used = p->quantum_used / CYCLES_PER_US;
  for (int i = 0; i < NQUEUE; i++) {
    uint64 t = p->level_time[i];
    if (p->state == RUNNING && i == p->priority)
      t += d;
    st->level_time[i] = t / CYCLES_PER_US;
  }
  st->max_wait = maxw / CYCLES_PER_US;
  st->ndispatch = p->ndispatch;
  st->nexpire = p->nexpire;
  st->npreempt = p->npreempt;
  st->nsleep = p->nsleep;
  st->ndemote = p->ndemote;
  st->npromote = p->npromote;
}

// getprocstat() system call: copy the statistics of process pid
// (0 = the caller) to user address addr.
int
getprocstat(int pid, uint64 addr)
{
  struct proc *me = myproc();
  struct procstat st;
  struct proc *p;
  int found = 0;

  if (pid == 0)
    pid = me->pid;
  for (p = proc; p < &proc[NPROC] && !found; p++) {
    acquire(&p->lock);
    if (p->pid == pid && p->state != UNUSED) {
      proc_getstat(p, &st);
      found = 1;
    }
    release(&p->lock);
  }
  if (!found)
    return -1;
  return copyout(me->pagetable, me->sz, addr, (char *)&st, sizeof(st));
}

// schedinfo() system call: describe the scheduler configuration.
int
schedinfo(uint64 addr)
{
  struct proc *me = myproc();
  struct schedinfo si;

  memset(&si, 0, sizeof(si));
#ifdef SCHED_RR
  si.policy = SCHED_POLICY_RR;
  si.nqueue = 1;
  si.quantum[0] = 1;
  si.aging = 0;
#else
  si.policy = SCHED_POLICY_MLFQ;
  si.nqueue = NQUEUE;
  for (int i = 0; i < NQUEUE; i++)
    si.quantum[i] = quantum(i) / TICK_CYCLES;
  si.aging = AGING_THRESHOLD;
#endif
  si.tick_us = TICK_CYCLES / CYCLES_PER_US;
  si.ncpu = __atomic_load_n(&cpus_online, __ATOMIC_RELAXED);
  return copyout(me->pagetable, me->sz, addr, (char *)&si, sizeof(si));
}

// schedtrace() system call.
//   schedtrace(1, 0, 0): clear the trace and start recording.
//   schedtrace(0, buf, max): stop recording, copy up to max events to
//   buf and return the number copied.
int
schedtrace(int enable, uint64 buf, int max)
{
  struct proc *me = myproc();
  int n;

  acquire(&trace.lock);
  trace.enabled = 0;
  if (enable) {
    trace.n = 0;
    trace.enabled = 1;
    release(&trace.lock);
    return 0;
  }
  n = trace.n;
  release(&trace.lock);

  // Recording is off, so trace.ev cannot change while we copy it.
  if (n > max)
    n = max;
  if (n > 0 && copyout(me->pagetable, me->sz, buf, (char *)trace.ev,
                       n * sizeof(struct schedevent)) < 0)
    return -1;
  return n;
}
