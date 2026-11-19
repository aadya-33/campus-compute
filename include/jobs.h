/*
 * jobs.h - FR1: job representation, input validation, lifecycle status.
 */
#ifndef JOBS_H
#define JOBS_H

#define MAX_JOBS 32

/* Process lifecycle as tracked by the schedulers. */
typedef enum {
    JOB_NEW,        /* read from input, not yet arrived          */
    JOB_READY,      /* arrived, waiting for the CPU              */
    JOB_RUNNING,    /* currently on the CPU                      */
    JOB_COMPLETED   /* finished, metrics available               */
} job_status_t;

typedef struct {
    /* input fields */
    int id;
    int arrival;
    int burst;
    int priority;           /* smaller number = higher priority */
    /* runtime / result fields */
    job_status_t status;
    int remaining;
    int start;              /* first time on the CPU (-1 = never) */
    int completion;
    int turnaround;         /* completion - arrival               */
    int waiting;            /* turnaround - burst                 */
    int response;           /* start - arrival                    */
} job_t;

typedef struct {
    job_t jobs[MAX_JOBS];
    int count;
} jobset_t;

/* Load and validate a job file. Prints every problem (with line numbers) to
 * stderr. Returns 0 on success, -1 if the file is unreadable or invalid. */
int jobs_load(const char *path, jobset_t *js);

/* Reset runtime fields so a job set can be scheduled again. */
void jobs_reset(jobset_t *js);

/* Print the input job table. */
void jobs_print_input(const jobset_t *js);

const char *job_status_name(job_status_t s);

#endif /* JOBS_H */
