/*
 * scheduler.c - FR2: FCFS, Round Robin and non-preemptive Priority scheduling.
 *
 * Rules used by every policy (also printed in the program output):
 *   - Time is measured in integer time units, starting at 0.
 *   - If the CPU has nothing to run it sits idle until the next arrival.
 *   - Turnaround = Completion - Arrival,  Waiting = Turnaround - Burst,
 *     Response   = First start - Arrival.
 *
 * Tie-breaking:
 *   FCFS     : equal arrival time -> lower job ID first.
 *   Priority : lower priority number wins; equal priority -> earlier arrival;
 *              still equal -> lower job ID.
 *   RR       : jobs that arrive at the same instant are queued by job ID; a job
 *              that arrives exactly when a quantum expires is queued AHEAD of
 *              the job that was just preempted.
 */
#include "scheduler.h"
#include "common.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_JOB_FILE "data/jobs.txt"
#define DEFAULT_QUANTUM  3
#define GANTT_PER_ROW    10

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static void result_init(sched_result_t *r, const char *name)
{
    memset(r, 0, sizeof *r);
    r->name = name;
}

void sched_result_free(sched_result_t *r)
{
    free(r->seg);
    r->seg = NULL;
    r->nseg = r->cap = 0;
}

/* Append a timeline slice; merge it with the previous one if it is the same
 * job continuing without a gap. */
static int add_seg(sched_result_t *r, int job_id, int start, int end)
{
    if (end <= start)
        return 0;
    if (r->nseg > 0) {
        seg_t *last = &r->seg[r->nseg - 1];
        if (last->job_id == job_id && last->end == start) {
            last->end = end;
            return 0;
        }
    }
    if (r->nseg == r->cap) {
        int ncap = r->cap ? r->cap * 2 : 32;
        seg_t *p = realloc(r->seg, (size_t)ncap * sizeof *p);
        if (p == NULL) {
            fprintf(stderr, "Error: out of memory while building the timeline\n");
            return -1;
        }
        r->seg = p;
        r->cap = ncap;
    }
    r->seg[r->nseg].job_id = job_id;
    r->seg[r->nseg].start  = start;
    r->seg[r->nseg].end    = end;
    r->nseg++;
    return 0;
}

/* Compute per-job and average metrics once every job has a completion time. */
static void finalize(jobset_t *js, sched_result_t *r)
{
    double wt = 0, tat = 0, rt = 0;
    int busy = 0;

    r->makespan = 0;
    for (int i = 0; i < js->count; i++) {
        job_t *j = &js->jobs[i];
        j->turnaround = j->completion - j->arrival;
        j->waiting    = j->turnaround - j->burst;
        j->response   = j->start - j->arrival;
        wt  += j->waiting;
        tat += j->turnaround;
        rt  += j->response;
        busy += j->burst;
        if (j->completion > r->makespan)
            r->makespan = j->completion;
    }
    r->avg_wt  = wt  / js->count;
    r->avg_tat = tat / js->count;
    r->avg_rt  = rt  / js->count;
    r->cpu_util = r->makespan > 0 ? 100.0 * busy / r->makespan : 0.0;
}

/* Insertion sort of job indexes with a caller-supplied "comes before" test. */
typedef int (*before_fn)(const job_t *a, const job_t *b);

static int before_arrival(const job_t *a, const job_t *b)
{
    if (a->arrival != b->arrival)
        return a->arrival < b->arrival;
    return a->id < b->id;
}

static void sort_order(const jobset_t *js, int *order, before_fn before)
{
    for (int i = 0; i < js->count; i++) {
        int k = i;
        while (k > 0 && before(&js->jobs[i], &js->jobs[order[k - 1]]))
            k--;
        for (int m = i; m > k; m--)
            order[m] = order[m - 1];
        order[k] = i;
    }
}

/* ------------------------------------------------------------------ */
/* FCFS                                                                */
/* ------------------------------------------------------------------ */

int sched_fcfs(jobset_t *js, sched_result_t *r)
{
    int order[MAX_JOBS], t = 0;

    result_init(r, "FCFS (First-Come First-Served)");
    jobs_reset(js);
    sort_order(js, order, before_arrival);

    for (int k = 0; k < js->count; k++) {
        job_t *j = &js->jobs[order[k]];
        if (t < j->arrival) {                       /* CPU idle until arrival */
            if (add_seg(r, 0, t, j->arrival) != 0)
                return -1;
            t = j->arrival;
        }
        j->status = JOB_RUNNING;
        j->start = t;
        if (add_seg(r, j->id, t, t + j->burst) != 0)
            return -1;
        t += j->burst;
        j->remaining = 0;
        j->completion = t;
        j->status = JOB_COMPLETED;
    }
    finalize(js, r);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Non-preemptive Priority                                             */
/* ------------------------------------------------------------------ */

/* Is job a a better choice than job b? (lower number = higher priority) */
static int better_priority(const job_t *a, const job_t *b)
{
    if (a->priority != b->priority)
        return a->priority < b->priority;
    if (a->arrival != b->arrival)
        return a->arrival < b->arrival;
    return a->id < b->id;
}

int sched_priority(jobset_t *js, sched_result_t *r)
{
    int t = 0, done = 0, n = js->count;
    int finished[MAX_JOBS] = {0};

    result_init(r, "Priority (non-preemptive)");
    jobs_reset(js);

    while (done < n) {
        int best = -1, next_arrival = -1;

        for (int i = 0; i < n; i++) {
            const job_t *j = &js->jobs[i];
            if (finished[i])
                continue;
            if (j->arrival <= t) {
                js->jobs[i].status = JOB_READY;
                if (best < 0 || better_priority(j, &js->jobs[best]))
                    best = i;
            } else if (next_arrival < 0 || j->arrival < next_arrival) {
                next_arrival = j->arrival;
            }
        }
        if (best < 0) {                             /* nothing has arrived yet */
            if (add_seg(r, 0, t, next_arrival) != 0)
                return -1;
            t = next_arrival;
            continue;
        }

        job_t *j = &js->jobs[best];
        j->status = JOB_RUNNING;
        j->start = t;
        if (add_seg(r, j->id, t, t + j->burst) != 0)
            return -1;
        t += j->burst;
        j->remaining = 0;
        j->completion = t;
        j->status = JOB_COMPLETED;
        finished[best] = 1;
        done++;
    }
    finalize(js, r);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Round Robin                                                         */
/* ------------------------------------------------------------------ */

typedef struct {
    int a[MAX_JOBS + 1];
    int head, tail, n;
} iq_t;

static void iq_push(iq_t *q, int v)
{
    q->a[q->tail] = v;
    q->tail = (q->tail + 1) % (MAX_JOBS + 1);
    q->n++;
}

static int iq_pop(iq_t *q)
{
    int v = q->a[q->head];
    q->head = (q->head + 1) % (MAX_JOBS + 1);
    q->n--;
    return v;
}

/* Move every job that has arrived by time t into the ready queue. */
static void admit(jobset_t *js, const int *order, int *next, int t, iq_t *q)
{
    while (*next < js->count && js->jobs[order[*next]].arrival <= t) {
        js->jobs[order[*next]].status = JOB_READY;
        iq_push(q, order[*next]);
        (*next)++;
    }
}

int sched_rr(jobset_t *js, int quantum, sched_result_t *r)
{
    int order[MAX_JOBS], next = 0, t = 0, done = 0, n = js->count;
    iq_t q = { .head = 0, .tail = 0, .n = 0 };
    static char name[64];

    snprintf(name, sizeof name, "Round Robin (quantum = %d)", quantum);
    result_init(r, name);
    jobs_reset(js);
    sort_order(js, order, before_arrival);

    while (done < n) {
        admit(js, order, &next, t, &q);
        if (q.n == 0) {                             /* idle until next arrival */
            int na = js->jobs[order[next]].arrival;
            if (add_seg(r, 0, t, na) != 0)
                return -1;
            t = na;
            continue;
        }

        int i = iq_pop(&q);
        job_t *j = &js->jobs[i];
        int slice = j->remaining < quantum ? j->remaining : quantum;

        j->status = JOB_RUNNING;
        if (j->start < 0)
            j->start = t;
        if (add_seg(r, j->id, t, t + slice) != 0)
            return -1;
        t += slice;
        j->remaining -= slice;

        admit(js, order, &next, t, &q);             /* arrivals queue BEFORE the preempted job */
        if (j->remaining > 0) {
            j->status = JOB_READY;
            iq_push(&q, i);
        } else {
            j->completion = t;
            j->status = JOB_COMPLETED;
            done++;
        }
    }
    finalize(js, r);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Printing                                                            */
/* ------------------------------------------------------------------ */

static void print_gantt(const sched_result_t *r)
{
    printf("Timeline (Gantt chart, cell widths not to scale):\n");
    for (int base = 0; base < r->nseg; base += GANTT_PER_ROW) {
        int end = base + GANTT_PER_ROW < r->nseg ? base + GANTT_PER_ROW : r->nseg;
        char label[16];

        printf("  ");
        for (int k = base; k < end; k++) {
            if (r->seg[k].job_id == 0)
                snprintf(label, sizeof label, "idle");
            else
                snprintf(label, sizeof label, "J%d", r->seg[k].job_id);
            printf("| %-5s", label);
        }
        printf("|\n  ");
        for (int k = base; k < end; k++)
            printf("%-7d", r->seg[k].start);
        printf("%d\n", r->seg[end - 1].end);
    }
}

static void print_order(const sched_result_t *r)
{
    int first = 1;

    printf("Execution order: ");
    for (int k = 0; k < r->nseg; k++) {
        if (r->seg[k].job_id == 0)
            continue;
        printf("%sJ%d", first ? "" : " -> ", r->seg[k].job_id);
        first = 0;
    }
    printf("\n");
}

static void print_result(const jobset_t *js, const sched_result_t *r,
                         const char *rule1, const char *rule2)
{
    print_banner(r->name);
    if (rule1)
        printf("%s\n", rule1);
    if (rule2)
        printf("%s\n", rule2);
    printf("\n");
    print_gantt(r);
    print_order(r);

    printf("\n  %-5s %-8s %-6s %-8s %-6s %-11s %-11s %-8s %-9s %s\n",
           "Job", "Arrival", "Burst", "Priority", "Start", "Completion",
           "Turnaround", "Waiting", "Response", "Status");
    for (int i = 0; i < js->count; i++) {
        const job_t *j = &js->jobs[i];
        printf("  J%-4d %-8d %-6d %-8d %-6d %-11d %-11d %-8d %-9d %s\n",
               j->id, j->arrival, j->burst, j->priority, j->start, j->completion,
               j->turnaround, j->waiting, j->response, job_status_name(j->status));
    }
    printf("\n  Average waiting time    : %.2f\n", r->avg_wt);
    printf("  Average turnaround time : %.2f\n", r->avg_tat);
    printf("  Average response time   : %.2f\n", r->avg_rt);
    printf("  Total time (makespan)   : %d    CPU utilisation: %.1f%%\n",
           r->makespan, r->cpu_util);
}

/* ------------------------------------------------------------------ */
/* Comparison                                                          */
/* ------------------------------------------------------------------ */

int sched_compare_run(const char *file, int quantum)
{
    jobset_t base, work;
    sched_result_t res[3];
    int rc = 0, ran = 0;

    if (jobs_load(file, &base) != 0)
        return -1;

    print_banner("Scheduling comparison on the same job set");
    jobs_print_input(&base);

    work = base;
    if (sched_fcfs(&work, &res[0]) != 0) { rc = -1; goto out; }
    ran = 1;
    work = base;
    if (sched_rr(&work, quantum, &res[1]) != 0) { rc = -1; goto out; }
    ran = 2;
    work = base;
    if (sched_priority(&work, &res[2]) != 0) { rc = -1; goto out; }
    ran = 3;

    printf("\n  %-32s %-8s %-8s %-8s %-9s %s\n",
           "Policy", "Avg WT", "Avg TAT", "Avg RT", "Makespan", "CPU util");
    for (int i = 0; i < 3; i++)
        printf("  %-32s %-8.2f %-8.2f %-8.2f %-9d %.1f%%\n",
               res[i].name, res[i].avg_wt, res[i].avg_tat, res[i].avg_rt,
               res[i].makespan, res[i].cpu_util);

    int bw = 0, bt = 0;
    for (int i = 1; i < 3; i++) {
        if (res[i].avg_wt  < res[bw].avg_wt)  bw = i;
        if (res[i].avg_tat < res[bt].avg_tat) bt = i;
    }
    printf("\n  Lowest average waiting time    : %s (%.2f)\n", res[bw].name, res[bw].avg_wt);
    printf("  Lowest average turnaround time : %s (%.2f)\n", res[bt].name, res[bt].avg_tat);
    printf("\n  Execution order per policy:\n");
    for (int i = 0; i < 3; i++) {
        printf("    %-32s ", res[i].name);
        int first = 1;
        for (int k = 0; k < res[i].nseg; k++) {
            if (res[i].seg[k].job_id == 0)
                continue;
            printf("%sJ%d", first ? "" : " ", res[i].seg[k].job_id);
            first = 0;
        }
        printf("\n");
    }
    detail_set("avg WT: FCFS %.2f | RR(q=%d) %.2f | Priority %.2f",
               res[0].avg_wt, quantum, res[1].avg_wt, res[2].avg_wt);

out:
    for (int i = 0; i < ran; i++)
        sched_result_free(&res[i]);
    return rc;
}

/* ------------------------------------------------------------------ */
/* CLI                                                                 */
/* ------------------------------------------------------------------ */

static int sched_usage_error(const char *msg)
{
    fprintf(stderr, "Error: %s\n"
                    "Usage: campuscompute sched <fcfs|rr|priority|compare> [-f jobs.txt] [-q quantum]\n"
                    "  -f FILE   job file (default: %s)\n"
                    "  -q N      Round Robin time quantum, 1..100 (default: %d)\n",
            msg, DEFAULT_JOB_FILE, DEFAULT_QUANTUM);
    return 2;
}

int sched_cmd(int argc, char **argv)
{
    const char *file = DEFAULT_JOB_FILE;
    long quantum = DEFAULT_QUANTUM;
    int opt, q_given = 0;
    char msg[96];

    opterr = 0;
    optind = 1;
    while ((opt = getopt(argc, argv, ":f:q:")) != -1) {
        switch (opt) {
        case 'f':
            file = optarg;
            break;
        case 'q':
            if (parse_long(optarg, 1, 100, &quantum) != 0) {
                snprintf(msg, sizeof msg, "invalid time quantum '%s' (must be an integer 1..100)", optarg);
                return sched_usage_error(msg);
            }
            q_given = 1;
            break;
        case ':':
            snprintf(msg, sizeof msg, "option -%c needs a value", optopt);
            return sched_usage_error(msg);
        default:
            snprintf(msg, sizeof msg, "unknown option -%c", optopt);
            return sched_usage_error(msg);
        }
    }
    if (optind >= argc)
        return sched_usage_error("missing policy name");
    if (optind + 1 < argc) {
        snprintf(msg, sizeof msg, "unexpected extra argument '%s'", argv[optind + 1]);
        return sched_usage_error(msg);
    }

    const char *algo = argv[optind];
    int is_rr   = strcmp(algo, "rr") == 0;
    int is_cmp  = strcmp(algo, "compare") == 0;
    int is_fcfs = strcmp(algo, "fcfs") == 0;
    int is_prio = strcmp(algo, "priority") == 0 || strcmp(algo, "prio") == 0;

    if (!is_rr && !is_cmp && !is_fcfs && !is_prio) {
        snprintf(msg, sizeof msg, "unknown scheduling policy '%s'", algo);
        return sched_usage_error(msg);
    }
    if (q_given && !is_rr && !is_cmp)
        fprintf(stderr, "Note: -q only affects Round Robin and compare; ignored for %s\n", algo);

    if (is_cmp)
        return sched_compare_run(file, (int)quantum) == 0 ? 0 : 1;

    jobset_t js;
    sched_result_t r;

    if (jobs_load(file, &js) != 0)
        return 1;
    printf("Job file: %s\n", file);
    jobs_print_input(&js);

    int rc;
    char rule2[64];
    if (is_fcfs) {
        rc = sched_fcfs(&js, &r);
        if (rc == 0)
            print_result(&js, &r, "Rule: jobs run in order of arrival, non-preemptive.",
                         "Tie-break: equal arrival time -> lower job ID first.");
    } else if (is_rr) {
        rc = sched_rr(&js, (int)quantum, &r);
        snprintf(rule2, sizeof rule2, "Time quantum = %ld time units.", quantum);
        if (rc == 0)
            print_result(&js, &r,
                         "Rule: preemptive; a job that uses its whole quantum goes to the back of the queue.",
                         rule2);
    } else {
        rc = sched_priority(&js, &r);
        if (rc == 0)
            print_result(&js, &r,
                         "Rule: highest priority (smallest number) among arrived jobs runs next, non-preemptive.",
                         "Tie-break: equal priority -> earlier arrival -> lower job ID.");
    }
    if (rc != 0) {
        sched_result_free(&r);
        return 1;
    }
    printf("\nAll %d jobs reached status COMPLETED.\n", js.count);
    sched_result_free(&r);
    return 0;
}
