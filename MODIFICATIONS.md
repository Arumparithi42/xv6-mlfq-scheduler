# Modifications to Original xv6

## 1. Process Structure
File: kernel/proc.h

Added:
- queue_level
- ticks_used
- waiting_ticks
- cpu_ticks
- total_wait_ticks
- creation_tick
- first_run_tick
- finish_tick

## 2. Process Initialization
File: kernel/proc.c

Initialized MLFQ-related fields in allocproc().

## 3. MLFQ Scheduler
File: kernel/proc.c

Replaced the original scheduler selection logic with
priority-based queue selection.

Queue priorities:
Q0 > Q1 > Q2

## 4. Timer-Based Preemption
File: kernel/proc.c / kernel/trap.c

Added timer_yield()...

## 5. Aging
File: kernel/proc.c

Added update_aging()...

## 6. Wakeup Handling
File: kernel/proc.c

Reset scheduling-related counters when a sleeping
process becomes runnable.

## 7. Process Statistics
Files: kernel/proc.c, kernel/proc.h

Added CPU, waiting, response and turnaround measurements.

## 8. Process Completion
File: kernel/proc.c

Added statistics output when a process exits.

## 9. Benchmark Programs
Files:
- user/cpubench.c
- user/cpubench_med.c
- user/cpubench_short.c
- user/iobench.c

## 10. Makefile
Added benchmark programs to USER_PROGRAMS.
