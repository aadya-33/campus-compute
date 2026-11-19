/*
 * worker.c - FR6: worker processes (fork) and failure detection (waitpid).
 *
 * The parent forks several "worker" processes. Each worker is told in
 * advance how to behave:
 *   NORMAL      - does a small computation and exits with status 0.
 *   EXIT_FAIL   - SIMULATED failure: exits on purpose with status 42.
 *   SIGNAL_FAIL - SIMULATED crash: sends SIGKILL to itself on purpose.
 * The parent collects every child with waitpid() and classifies the wait
 * status with WIFEXITED / WEXITSTATUS / WIFSIGNALED / WTERMSIG. The failures
 * are deliberate and are labelled "[SIMULATED]" - they are NOT real OS faults.
 */
#include "worker.h"
#include "common.h"

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define SIMULATED_EXIT_CODE 42
#define MAX_WORKERS 8

typedef enum { WK_NORMAL, WK_EXIT_FAIL, WK_SIGNAL_FAIL } wk_kind_t;

typedef struct {
    pid_t pid;
    int id;
    wk_kind_t kind;
} wk_t;

static const char *kind_name(wk_kind_t k)
{
    switch (k) {
    case WK_NORMAL:      return "normal task";
    case WK_EXIT_FAIL:   return "SIMULATED failure (non-zero exit)";
    case WK_SIGNAL_FAIL: return "SIMULATED crash (killed by signal)";
    }
    return "?";
}

/* Code that runs only in the child process. Never returns. */
static void worker_child(int id, wk_kind_t kind)
{
    unsigned long sum = 0;

    printf("  [WORKER %d pid=%d] started (%s)\n", id, (int)getpid(), kind_name(kind));
    fflush(stdout);
    for (unsigned long i = 1; i <= 2000000UL; i++)        /* the "job" */
        sum += i % 7;
    usleep(100000u * (unsigned)id);                       /* stagger the finish times */

    switch (kind) {
    case WK_NORMAL:
        printf("  [WORKER %d pid=%d] task finished, result=%lu -> exit(0)\n",
               id, (int)getpid(), sum);
        fflush(stdout);
        _exit(0);
    case WK_EXIT_FAIL:
        printf("  [WORKER %d pid=%d] *** SIMULATED FAILURE (intentional, not a real OS fault): exit(%d) ***\n",
               id, (int)getpid(), SIMULATED_EXIT_CODE);
        fflush(stdout);
        _exit(SIMULATED_EXIT_CODE);
    case WK_SIGNAL_FAIL:
        printf("  [WORKER %d pid=%d] *** SIMULATED CRASH (intentional, not a real OS fault): raising SIGKILL ***\n",
               id, (int)getpid());
        fflush(stdout);
        kill(getpid(), SIGKILL);
        break;
    }
    _exit(99);                                            /* not reached */
}

int worker_run(worker_scenario_t sc)
{
    wk_t w[MAX_WORKERS];
    int n = 0, ok = 1;
    int exit0 = 0, exitN = 0, sig = 0;

    switch (sc) {
    case WSCEN_NORMAL:
        w[n++] = (wk_t){0, 1, WK_NORMAL};
        w[n++] = (wk_t){0, 2, WK_NORMAL};
        break;
    case WSCEN_FAIL:
        w[n++] = (wk_t){0, 1, WK_EXIT_FAIL};
        break;
    case WSCEN_CRASH:
        w[n++] = (wk_t){0, 1, WK_SIGNAL_FAIL};
        break;
    case WSCEN_ALL:
        w[n++] = (wk_t){0, 1, WK_NORMAL};
        w[n++] = (wk_t){0, 2, WK_EXIT_FAIL};
        w[n++] = (wk_t){0, 3, WK_NORMAL};
        w[n++] = (wk_t){0, 4, WK_SIGNAL_FAIL};
        break;
    }

    print_banner("Worker processes: fork() and waitpid() failure detection");
    printf("Parent pid=%d will fork %d worker(s)\n\n", (int)getpid(), n);

    int spawned = 0;
    fflush(NULL);                                         /* avoid duplicated buffered output */
    for (int i = 0; i < n; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("Error: fork");
            ok = 0;
            break;
        }
        if (pid == 0)
            worker_child(w[i].id, w[i].kind);
        w[i].pid = pid;
        spawned++;
        printf("  [PARENT] forked worker %d -> pid %d (%s)\n", w[i].id, (int)pid, kind_name(w[i].kind));
    }
    printf("\nParent waiting with waitpid(-1, ...) for %d child(ren):\n", spawned);

    int remaining = spawned;
    while (remaining > 0) {
        int st = 0;
        pid_t p = waitpid(-1, &st, 0);

        if (p < 0) {
            if (errno == EINTR)
                continue;
            if (errno != ECHILD)
                perror("Error: waitpid");
            ok = 0;
            break;
        }
        remaining--;

        wk_t *cur = NULL;
        for (int i = 0; i < spawned; i++)
            if (w[i].pid == p)
                cur = &w[i];
        if (cur == NULL)
            continue;

        int expect_fail = cur->kind != WK_NORMAL;
        if (WIFEXITED(st)) {
            int code = WEXITSTATUS(st);
            if (code == 0) {
                exit0++;
                printf("  [PARENT] worker %d (pid %d): exited normally, status 0 -> SUCCESS\n",
                       cur->id, (int)p);
                if (expect_fail)
                    ok = 0;
            } else {
                exitN++;
                printf("  [PARENT] worker %d (pid %d): exited with NON-ZERO status %d -> FAILURE detected%s\n",
                       cur->id, (int)p, code, expect_fail ? " [SIMULATED]" : " [UNEXPECTED]");
                if (cur->kind != WK_EXIT_FAIL || code != SIMULATED_EXIT_CODE)
                    ok = 0;
            }
        } else if (WIFSIGNALED(st)) {
            int s = WTERMSIG(st);
            sig++;
            printf("  [PARENT] worker %d (pid %d): terminated by signal %d (%s) -> FAILURE detected%s\n",
                   cur->id, (int)p, s, strsignal(s), expect_fail ? " [SIMULATED]" : " [UNEXPECTED]");
            if (cur->kind != WK_SIGNAL_FAIL || s != SIGKILL)
                ok = 0;
        } else {
            printf("  [PARENT] worker %d (pid %d): unusual wait status 0x%x\n", cur->id, (int)p, st);
            ok = 0;
        }
    }

    printf("\nSummary:\n");
    printf("  workers forked / reaped         : %d / %d\n", spawned, spawned - remaining);
    printf("  normal completions (status 0)   : %d\n", exit0);
    printf("  non-zero exit statuses detected : %d\n", exitN);
    printf("  signal terminations detected    : %d\n", sig);
    printf("  zombie children left            : %d\n", remaining);
    printf("  RESULT: %s\n", ok && remaining == 0
           ? "PASS - every worker outcome was detected and reported accurately"
           : "FAIL");
    detail_set("%d normal, %d exit-status failure, %d signal failure - all detected",
               exit0, exitN, sig);
    return (ok && remaining == 0) ? 0 : 1;
}

/* ------------------------------------------------------------------ */

static int worker_usage_error(const char *msg)
{
    fprintf(stderr, "Error: %s\n"
                    "Usage: campuscompute worker <normal|fail|crash|all>\n"
                    "  normal   two workers that finish with status 0\n"
                    "  fail     one worker with a SIMULATED failure (exit status 42)\n"
                    "  crash    one worker with a SIMULATED crash (killed by SIGKILL)\n"
                    "  all      2 normal + 1 failure + 1 crash together\n", msg);
    return 2;
}

int worker_cmd(int argc, char **argv)
{
    char msg[96];

    if (argc != 2)
        return worker_usage_error(argc < 2 ? "missing scenario" : "too many arguments");
    if (strcmp(argv[1], "normal") == 0) return worker_run(WSCEN_NORMAL);
    if (strcmp(argv[1], "fail") == 0)   return worker_run(WSCEN_FAIL);
    if (strcmp(argv[1], "crash") == 0)  return worker_run(WSCEN_CRASH);
    if (strcmp(argv[1], "all") == 0)    return worker_run(WSCEN_ALL);
    snprintf(msg, sizeof msg, "unknown worker scenario '%s'", argv[1]);
    return worker_usage_error(msg);
}
