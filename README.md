# CampusCompute - Intelligent Job Scheduling and Resource Management

Operating Systems (UE25CS242A) - Level 3 Mini-Project.
A command-line prototype, written in C for Linux (Ubuntu / WSL Ubuntu), that demonstrates
CPU scheduling, a concurrent logger, POSIX IPC, deadlock avoidance, process failure
detection and resource limits. All output is produced by working code - nothing is hard-coded.

## Prerequisites

    sudo apt update
    sudo apt install build-essential zip unzip      # gcc, make, zip/unzip

Needs Linux with `/dev/shm` (Ubuntu or WSL Ubuntu). No other libraries are required.

## Build

    make            # builds ./campuscompute (warning-free with -Wall -Wextra -Wpedantic)
    make clean      # removes the executable and build/ directory

Without `make`, the exact build command is:

    gcc -std=gnu11 -Wall -Wextra -Wpedantic -O2 -g -Iinclude -D_GNU_SOURCE src/*.c -o campuscompute -pthread -lrt

## Run (always from the project directory)

| Command | What it shows | Requirement |
|---|---|---|
| `./campuscompute help` | all commands | FR8 |
| `./campuscompute sched fcfs` | FCFS timeline + metrics | FR1, FR2 |
| `./campuscompute sched rr -q 3` | Round Robin, quantum 3 | FR2 |
| `./campuscompute sched priority` | non-preemptive Priority, tie-breaking | FR2 |
| `./campuscompute sched compare -q 3` | all three policies on the same jobs | FR2 |
| `./campuscompute logger [-p N] [-c N] [-n N] [-s N] [-l file]` | producer-consumer logger (pthreads, mutex, condition variables) | FR3 |
| `./campuscompute ipc [-m N]` | POSIX shared memory + named semaphores between processes | FR4 |
| `./campuscompute banker safe` / `banker unsafe` / `banker -f file` | Banker's safety algorithm | FR5 |
| `./campuscompute worker normal\|fail\|crash\|all` | fork()/waitpid() failure detection (failures are SIMULATED) | FR6 |
| `./campuscompute rlimit nofile\|mem\|cpu\|all` | setrlimit() effects | FR7 |
| `./campuscompute cleanup` | removes stale program-created IPC objects and verifies cleanup | FR7 |
| `./campuscompute demo [-v]` | integrated demonstration of everything + summary | all |

Every feature is a separate command, so each one can be tested on its own. Invalid commands,
options or input files print an error plus a usage message and return exit code 2 (usage error)
or 1 (invalid input / runtime failure). `make test` runs 35 automated checks.

## Cleanup

* Threads are joined and child processes are reaped by the program itself.
* The IPC test creates `/cc_campus_shm_<pid>` and `/cc_campus_sem_{empty,full,mutex}_<pid>` and
  unlinks them before it exits (also after an error or Ctrl+C).
* If a run was ever killed with `kill -9`, run `./campuscompute cleanup`. It removes only
  objects in `/dev/shm` named `cc_campus_*_<pid>` whose owner process no longer exists; it never
  touches any other file or any other program's IPC object.
* `make clean` removes the executable and objects; `make distclean` also removes `logs/`.

## Design notes

* **Time units**: integers starting at 0; the CPU idles (shown as `idle`) when no job is ready.
  Turnaround = completion - arrival, waiting = turnaround - burst, response = first start - arrival.
* **FCFS tie-break**: equal arrival time -> lower job ID first.
* **Round Robin**: default quantum 3 (`-q` to change, 1..100). Jobs arriving at the same instant are
  queued by job ID; a job that arrives exactly when a quantum expires is queued *ahead of* the
  job that was just preempted.
* **Priority**: smaller number = higher priority. Ties: earlier arrival, then lower job ID.
  Non-preemptive (a running job always finishes).
* **Logger**: circular queue of fixed size guarded by one mutex; condition variables `not_full`
  (producers wait) and `not_empty` (consumers wait). A second mutex serialises log-file writes.
  Shutdown: producers are joined, `done` is set and `not_empty` is broadcast; consumers drain the
  queue and exit, so no event is lost. Log file: `logs/campuscompute.log`.
* **IPC**: two writer processes and one reader share a 2-slot ring buffer in POSIX shared memory.
  Named semaphores `empty` (free slots), `full` (filled slots) and `mutex` coordinate them;
  the reader uses `sem_timedwait` so it can never hang forever.
* **Banker's algorithm**: Need = Maximum - Allocation; the full safety trace is printed.
* **Failures are simulated**: worker 2 exits with status 42 and worker 4 sends itself SIGKILL
  *on purpose*; the output labels them `[SIMULATED]` - they are not real OS failures.
* **Resource limits** run in forked children so the parent's limits never change:
  `RLIMIT_NOFILE` (open() fails with EMFILE), `RLIMIT_AS` (malloc() fails), `RLIMIT_CPU`
  (kernel sends SIGXCPU to a busy loop).

## Input file formats (in `data/`)

* Jobs (`jobs.txt`): one job per line - `id arrival burst priority`; `#` starts a comment.
  `jobs_bad.txt` is deliberately malformed (negative burst, text, duplicate ID, missing field);
  `jobs_gap.txt` has an idle gap.
* Banker (`banker_safe.txt`, `banker_unsafe.txt`): `n m`, Available (m numbers), Maximum (n rows),
  Allocation (n rows). `banker_bad.txt` has Allocation > Maximum.

## Project layout

    Makefile  README.md
    include/  common.h jobs.h scheduler.h logger.h ipc.h banker.h worker.h rlimit_demo.h cleanup.h demo.h
    src/      main.c util.c jobs.c scheduler.c logger.c ipc.c banker.c worker.c rlimit_demo.c cleanup.c demo.c
    data/     sample inputs
    tests/    run_tests.sh
    logs/     created at run time
