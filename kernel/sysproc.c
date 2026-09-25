#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0; // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p, 0);
}

// int waitstat(int *status, struct procstat *st)
// Like wait(), but also returns the child's scheduling statistics.
uint64
sys_waitstat(void)
{
  uint64 p, st;
  argaddr(0, &p);
  argaddr(1, &st);
  return kwait(p, st);
}

// int getprocstat(int pid, struct procstat *st)
// Scheduling statistics of a live process (pid 0 = caller).
uint64
sys_getprocstat(void)
{
  int pid;
  uint64 st;
  argint(0, &pid);
  argaddr(1, &st);
  return getprocstat(pid, st);
}

// int schedinfo(struct schedinfo *si)
uint64
sys_schedinfo(void)
{
  uint64 si;
  argaddr(0, &si);
  return schedinfo(si);
}

// int schedtrace(int enable, struct schedevent *buf, int max)
uint64
sys_schedtrace(void)
{
  int enable, max;
  uint64 buf;
  argint(0, &enable);
  argaddr(1, &buf);
  argint(2, &max);
  return schedtrace(enable, buf, max);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if (t == SBRK_EAGER || n < 0) {
    if (growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if (addr + n < addr)
      return -1;
    if (addr + n > TRAPFRAME)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if (n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (killed(myproc())) {
      release(&tickslock);
      return -1;
    }
    sleep_prepare(&ticks);
    release(&tickslock);
    sleep();
    acquire(&tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
