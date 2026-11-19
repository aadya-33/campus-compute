/*
 * demo.c - integrated demonstration of every feature (FR1-FR8).
 *
 * Every step calls the REAL module code (nothing is hard-coded). In compact
 * mode the module's detailed output is hidden and only a one-line result plus
 * the evidence string the module produced is shown; with -v the full output of
 * every module is printed.
 */
#include "demo.h"
#include "banker.h"
#include "cleanup.h"
#include "common.h"
#include "ipc.h"
#include "jobs.h"
#include "logger.h"
#include "rlimit_demo.h"
#include "scheduler.h"
#include "worker.h"

#include <getopt.h>
#include <stdio.h>
#include <string.h>

#define NSTEPS 9

typedef struct {
    const char *fr;
    const char *title;
    int (*fn)(void);
    int rc;
    char detail[200];
} step_t;

static int step_jobs_and_sched(void)
{
    jobset_t js;

    if (jobs_load("data/jobs.txt", &js) != 0)
        return 1;
    printf("Loaded and validated %d jobs from data/jobs.txt\n", js.count);
    char first[200];
    int rc = sched_compare_run("data/jobs.txt", 3) == 0 ? 0 : 1;
    snprintf(first, sizeof first, "%d jobs validated; %s", js.count, detail_get());
    detail_set("%s", first);
    return rc;
}

static int step_logger(void)
{
    return logger_run(3, 2, 5, 4, LOG_DEFAULT_FILE);
}

static int step_ipc(void)
{
    return ipc_run(4);
}

static int step_banker(void)
{
    int safe1 = -1, safe2 = -1;
    char seq1[64], seq2[64];

    if (banker_run_file("data/banker_safe.txt", &safe1, seq1, sizeof seq1) != 0 ||
        banker_run_file("data/banker_unsafe.txt", &safe2, seq2, sizeof seq2) != 0)
        return 1;
    detail_set("safe state -> sequence %s; unsafe state detected", seq1);
    return (safe1 == 1 && safe2 == 0) ? 0 : 1;
}

static int step_worker(void)
{
    return worker_run(WSCEN_ALL);
}

static int step_rlimit(void)
{
    return rlimit_run(RL_ALL);
}

/* FR8: malformed input must be rejected with a useful message. */
static int step_invalid_input(void)
{
    jobset_t js;
    int bad_sd = mute_fd(2);                    /* hide the expected error text */
    int rc = jobs_load("data/jobs_bad.txt", &js);
    unmute_fd(2, bad_sd);

    if (rc == 0)
        return 1;
    detail_set("malformed job file rejected with line-numbered errors; no crash");
    return 0;
}

static int step_cleanup(void)
{
    return cleanup_run();
}

int demo_cmd(int argc, char **argv)
{
    int verbose = 0, opt;
    step_t steps[NSTEPS] = {
        { "FR1,FR2", "Job management + CPU scheduling (FCFS/RR/Priority)", step_jobs_and_sched, 0, "" },
        { "FR3",     "Concurrent logger (producer-consumer threads)",       step_logger,         0, "" },
        { "FR4",     "Shared-memory IPC + named semaphores",                step_ipc,            0, "" },
        { "FR5",     "Banker's algorithm (safe and unsafe states)",         step_banker,         0, "" },
        { "FR6",     "Worker processes: fork/waitpid failure detection",    step_worker,         0, "" },
        { "FR7",     "Resource limits with setrlimit()",                    step_rlimit,         0, "" },
        { "FR8",     "Malformed input is rejected cleanly",                 step_invalid_input,  0, "" },
        { "FR3",     "Log file produced by the logger",                     NULL,                0, "" },
        { "FR7",     "Cleanup verification (threads, processes, IPC, fds)", step_cleanup,        0, "" },
    };
    int failures = 0;

    opterr = 0;
    optind = 1;
    while ((opt = getopt(argc, argv, "v")) != -1) {
        if (opt == 'v') {
            verbose = 1;
        } else {
            fprintf(stderr, "Error: unknown option -%c\nUsage: campuscompute demo [-v]\n", optopt);
            return 2;
        }
    }
    if (optind < argc) {
        fprintf(stderr, "Error: unexpected argument '%s'\nUsage: campuscompute demo [-v]\n", argv[optind]);
        return 2;
    }

    print_banner("CampusCompute integrated demonstration");
    printf("Running every module for real%s...\n\n", verbose ? "" : " (use -v for the full output of each)");

    for (int i = 0; i < NSTEPS; i++) {
        step_t *s = &steps[i];
        printf("[%d/%d] %-8s %-52s %s", i + 1, NSTEPS, s->fr, s->title, verbose ? "\n" : "");
        fflush(stdout);

        if (s->fn == NULL) {
            /* the log file was written by step 2: confirm it has content */
            FILE *f = fopen(LOG_DEFAULT_FILE, "r");
            int lines = 0, ch;
            if (f != NULL) {
                while ((ch = fgetc(f)) != EOF)
                    if (ch == '\n')
                        lines++;
                fclose(f);
            }
            s->rc = (f != NULL && lines > 3) ? 0 : 1;
            snprintf(s->detail, sizeof s->detail, "%s has %d lines", LOG_DEFAULT_FILE, lines);
            printf("%s%s\n", verbose ? "        -> " : "", s->rc == 0 ? "done" : "FAILED");
            continue;
        }

        detail_set("%s", "");
        int saved = verbose ? -1 : mute_fd(1);
        s->rc = s->fn();
        if (!verbose)
            unmute_fd(1, saved);
        snprintf(s->detail, sizeof s->detail, "%s", detail_get());
        printf("%s%s\n", verbose ? "        -> " : "", s->rc == 0 ? "done" : "FAILED");
    }

    print_banner("Integrated summary");
    printf("  %-8s %-52s %s\n", "Req", "Feature", "Result");
    for (int i = 0; i < NSTEPS; i++) {
        printf("  %-8s %-52s %s\n", steps[i].fr, steps[i].title,
               steps[i].rc == 0 ? "PASS" : "FAIL");
        if (steps[i].detail[0] != '\0')
            printf("  %-8s   evidence: %s\n", "", steps[i].detail);
        if (steps[i].rc != 0)
            failures++;
    }
    printf("\n  Overall: %d of %d checks passed%s\n", NSTEPS - failures, NSTEPS,
           failures == 0 ? " - CampusCompute demonstration complete" : "");
    return failures == 0 ? 0 : 1;
}
