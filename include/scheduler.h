/*
 * scheduler.h - FR2: FCFS, Round Robin and non-preemptive Priority scheduling.
 */
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "jobs.h"

/* One slice of the CPU timeline. job_id == 0 means the CPU was idle. */
typedef struct {
    int job_id;
    int start;
    int end;
} seg_t;

typedef struct {
    const char *name;
    seg_t *seg;
    int nseg, cap;
    double avg_wt, avg_tat, avg_rt;
    int makespan;
    double cpu_util;            /* percent of [0, makespan] the CPU was busy */
} sched_result_t;

/* Each algorithm works on 'js' (its runtime fields are overwritten) and
 * fills 'r'. Return 0 on success, -1 on error (out of memory). */
int sched_fcfs(jobset_t *js, sched_result_t *r);
int sched_rr(jobset_t *js, int quantum, sched_result_t *r);
int sched_priority(jobset_t *js, sched_result_t *r);
void sched_result_free(sched_result_t *r);

/* "sched" command: argv[0] == "sched". */
int sched_cmd(int argc, char **argv);

/* Used by the integrated demo: run all three policies on one file and print
 * the comparison table. Returns 0 on success. */
int sched_compare_run(const char *file, int quantum);

#endif /* SCHEDULER_H */
