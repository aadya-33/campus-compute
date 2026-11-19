/*
 * jobs.c - FR1: job representation, input validation, lifecycle status.
 *
 * Job file format (one job per line, '#' starts a comment):
 *     <id> <arrival> <burst> <priority>
 */
#include "jobs.h"
#include "common.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

const char *job_status_name(job_status_t s)
{
    switch (s) {
    case JOB_NEW:       return "NEW";
    case JOB_READY:     return "READY";
    case JOB_RUNNING:   return "RUNNING";
    case JOB_COMPLETED: return "COMPLETED";
    }
    return "UNKNOWN";
}

void jobs_reset(jobset_t *js)
{
    for (int i = 0; i < js->count; i++) {
        job_t *j = &js->jobs[i];
        j->status     = JOB_NEW;
        j->remaining  = j->burst;
        j->start      = -1;
        j->completion = 0;
        j->turnaround = 0;
        j->waiting    = 0;
        j->response   = 0;
    }
}

/* Parse one numeric field and report a precise error if it is invalid. */
static int field(const char *file, int lineno, const char *name,
                 const char *text, long lo, long hi, long *out)
{
    if (parse_long(text, lo, hi, out) != 0) {
        fprintf(stderr, "Error: %s:%d: %s must be an integer in [%ld, %ld] (got '%s')\n",
                file, lineno, name, lo, hi, text);
        return -1;
    }
    return 0;
}

int jobs_load(const char *path, jobset_t *js)
{
    FILE *f = fopen(path, "r");
    char line[256];
    int lineno = 0, bad = 0;

    if (f == NULL) {
        fprintf(stderr, "Error: cannot open job file '%s': %s\n"
                        "       (run from the project directory or pass -f <file>)\n",
                path, strerror(errno));
        return -1;
    }

    js->count = 0;
    while (fgets(line, sizeof line, f) != NULL) {
        char *tok[8];
        int nt = 0;
        long v[4];
        size_t len = strlen(line);

        lineno++;
        if (len == sizeof line - 1 && line[len - 1] != '\n') {
            int ch;
            fprintf(stderr, "Error: %s:%d: line too long\n", path, lineno);
            while ((ch = fgetc(f)) != '\n' && ch != EOF)
                ;
            bad++;
            continue;
        }

        char *hash = strchr(line, '#');
        if (hash != NULL)
            *hash = '\0';                       /* strip comment */

        for (char *p = strtok(line, " \t\r\n"); p != NULL; p = strtok(NULL, " \t\r\n")) {
            if (nt < 8)
                tok[nt] = p;
            nt++;
        }
        if (nt == 0)
            continue;                           /* blank / comment-only line */
        if (nt != 4) {
            fprintf(stderr, "Error: %s:%d: expected 4 fields (id arrival burst priority), found %d\n",
                    path, lineno, nt);
            bad++;
            continue;
        }

        int ok = 1;
        ok &= field(path, lineno, "job id",   tok[0], 1, 9999,  &v[0]) == 0;
        ok &= field(path, lineno, "arrival",  tok[1], 0, 10000, &v[1]) == 0;
        ok &= field(path, lineno, "burst",    tok[2], 1, 1000,  &v[2]) == 0;
        ok &= field(path, lineno, "priority", tok[3], 1, 99,    &v[3]) == 0;
        if (!ok) {
            bad++;
            continue;
        }

        for (int i = 0; i < js->count; i++) {
            if (js->jobs[i].id == (int)v[0]) {
                fprintf(stderr, "Error: %s:%d: duplicate job id %ld\n", path, lineno, v[0]);
                ok = 0;
                break;
            }
        }
        if (ok && js->count >= MAX_JOBS) {
            fprintf(stderr, "Error: %s:%d: too many jobs (maximum is %d)\n", path, lineno, MAX_JOBS);
            ok = 0;
        }
        if (!ok) {
            bad++;
            continue;
        }

        job_t *j = &js->jobs[js->count++];
        memset(j, 0, sizeof *j);
        j->id       = (int)v[0];
        j->arrival  = (int)v[1];
        j->burst    = (int)v[2];
        j->priority = (int)v[3];
    }
    fclose(f);

    if (bad > 0) {
        fprintf(stderr, "Error: %d invalid line(s) in '%s' - job set rejected\n", bad, path);
        return -1;
    }
    if (js->count == 0) {
        fprintf(stderr, "Error: '%s' contains no jobs\n", path);
        return -1;
    }
    jobs_reset(js);
    return 0;
}

void jobs_print_input(const jobset_t *js)
{
    printf("Input job set (%d jobs):\n", js->count);
    printf("  %-5s %-8s %-6s %-8s %-10s\n", "Job", "Arrival", "Burst", "Priority", "Status");
    for (int i = 0; i < js->count; i++) {
        const job_t *j = &js->jobs[i];
        printf("  J%-4d %-8d %-6d %-8d %-10s\n",
               j->id, j->arrival, j->burst, j->priority, job_status_name(j->status));
    }
}
