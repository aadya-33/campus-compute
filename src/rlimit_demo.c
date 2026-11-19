/*
 * rlimit_demo.c - FR7: process resource limits with setrlimit().
 *
 * Every demonstration runs in a forked child process, so the limits only
 * apply to that child and disappear when it exits - the parent is untouched.
 *
 *   nofile : RLIMIT_NOFILE = 16    -> open() eventually fails with EMFILE
 *   mem    : RLIMIT_AS     = 128MB -> malloc() eventually fails with ENOMEM
 *   cpu    : RLIMIT_CPU    = 1 s   -> kernel kills a busy loop with SIGXCPU
 *
 * Child exit codes: 0 = expected effect observed, 1 = effect NOT observed,
 * 77 = limit not supported here (reported as SKIPPED).
 */
#include "rlimit_demo.h"
#include "common.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define NOFILE_LIMIT 16
#define MEM_LIMIT    (128UL * 1024 * 1024)
#define CHUNK        (16UL * 1024 * 1024)
#define SKIPPED      77

static void show_limit(const char *label, int resource)
{
    struct rlimit rl;

    if (getrlimit(resource, &rl) != 0) {
        printf("    %-14s : getrlimit failed (%s)\n", label, strerror(errno));
        return;
    }
    printf("    %-14s : soft=", label);
    if (rl.rlim_cur == RLIM_INFINITY) printf("unlimited"); else printf("%lu", (unsigned long)rl.rlim_cur);
    printf("  hard=");
    if (rl.rlim_max == RLIM_INFINITY) printf("unlimited"); else printf("%lu", (unsigned long)rl.rlim_max);
    printf("\n");
}

/* Lower a limit (soft and hard) without ever trying to raise the hard limit. */
static int lower_limit(int resource, rlim_t want)
{
    struct rlimit rl;

    if (getrlimit(resource, &rl) != 0)
        return -1;
    if (rl.rlim_max != RLIM_INFINITY && rl.rlim_max < want)
        want = rl.rlim_max;
    rl.rlim_cur = want;
    rl.rlim_max = want;
    return setrlimit(resource, &rl);
}

/* ---- the three child-side demonstrations -------------------------------- */

static int child_nofile(void)
{
    int fds[NOFILE_LIMIT + 4], n = 0, err = 0;

    printf("  [child pid=%d] before:\n", (int)getpid());
    show_limit("RLIMIT_NOFILE", RLIMIT_NOFILE);
    if (lower_limit(RLIMIT_NOFILE, NOFILE_LIMIT) != 0) {
        printf("  [child] setrlimit(RLIMIT_NOFILE) not possible: %s\n", strerror(errno));
        return SKIPPED;
    }
    printf("  [child] after setrlimit(RLIMIT_NOFILE, %d):\n", NOFILE_LIMIT);
    show_limit("RLIMIT_NOFILE", RLIMIT_NOFILE);

    for (;;) {
        int fd = open("/dev/null", O_RDONLY);
        if (fd < 0) {
            err = errno;
            break;
        }
        if (n < (int)(sizeof fds / sizeof fds[0]))
            fds[n++] = fd;
        else
            close(fd);
    }
    printf("  [child] opened %d extra descriptor(s), then open() failed: %s (errno %d)\n",
           n, strerror(err), err);
    for (int i = 0; i < n; i++)
        close(fds[i]);
    return err == EMFILE ? 0 : 1;
}

static int child_mem(void)
{
    void *chunks[64];
    int n = 0, err = 0;

    printf("  [child pid=%d] before:\n", (int)getpid());
    show_limit("RLIMIT_AS", RLIMIT_AS);
    if (lower_limit(RLIMIT_AS, MEM_LIMIT) != 0) {
        printf("  [child] setrlimit(RLIMIT_AS) not possible: %s\n", strerror(errno));
        return SKIPPED;
    }
    printf("  [child] after setrlimit(RLIMIT_AS, %lu MB):\n", MEM_LIMIT / (1024 * 1024));
    show_limit("RLIMIT_AS", RLIMIT_AS);

    while (n < 64) {
        void *p = malloc(CHUNK);
        if (p == NULL) {
            err = errno;
            break;
        }
        chunks[n++] = p;
    }
    int hit_limit = n < 64;
    if (hit_limit)
        printf("  [child] allocated %d chunk(s) of %lu MB (%lu MB total), then malloc() failed: %s\n",
               n, CHUNK / (1024 * 1024), n * (CHUNK / (1024 * 1024)), strerror(err));
    else
        printf("  [child] allocated all %d chunks without hitting a limit\n", n);
    for (int i = 0; i < n; i++)
        free(chunks[i]);
    return hit_limit ? 0 : 1;
}

static int child_cpu(void)
{
    struct rlimit rl = { 1, 2 };                          /* soft 1 s, hard 2 s */
    struct rlimit nocore = { 0, 0 };

    printf("  [child pid=%d] before:\n", (int)getpid());
    show_limit("RLIMIT_CPU", RLIMIT_CPU);
    setrlimit(RLIMIT_CORE, &nocore);                      /* no core file from SIGXCPU */
    if (setrlimit(RLIMIT_CPU, &rl) != 0) {
        printf("  [child] setrlimit(RLIMIT_CPU) not possible: %s\n", strerror(errno));
        return SKIPPED;
    }
    printf("  [child] after setrlimit(RLIMIT_CPU, 1 s):\n");
    show_limit("RLIMIT_CPU", RLIMIT_CPU);
    printf("  [child] starting an endless busy loop...\n");
    fflush(stdout);

    volatile unsigned long spin = 0;
    for (;;)
        spin++;
    return 1;                                             /* never reached */
}

/* ---- parent-side harness ------------------------------------------------ */

typedef struct {
    const char *title;
    int (*fn)(void);
    int expect_signal;                                    /* 0 = child exits itself, else signal number */
    const char *effect;
} demo_t;

static int run_demo(const demo_t *d)
{
    struct timespec t0, t1;
    int st = 0, ok = 0;

    printf("\n--- %s ---\n", d->title);
    fflush(NULL);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    pid_t pid = fork();
    if (pid < 0) {
        perror("Error: fork");
        return -1;
    }
    if (pid == 0) {
        int rc = d->fn();
        fflush(stdout);
        _exit(rc);
    }
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR)
        ;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double secs = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;

    if (d->expect_signal == 0 && WIFEXITED(st)) {
        int code = WEXITSTATUS(st);
        if (code == 0) {
            printf("  [parent] child exited with status 0 -> OBSERVED: %s\n", d->effect);
            ok = 1;
        } else if (code == SKIPPED) {
            printf("  [parent] limit not supported on this system -> SKIPPED\n");
            ok = 1;
        } else {
            printf("  [parent] child exited with status %d -> expected effect NOT observed\n", code);
        }
    } else if (d->expect_signal != 0 && WIFSIGNALED(st) && WTERMSIG(st) == d->expect_signal) {
        printf("  [parent] child killed by signal %d (%s) after %.1f s wall time -> OBSERVED: %s\n",
               WTERMSIG(st), strsignal(WTERMSIG(st)), secs, d->effect);
        ok = 1;
    } else if (WIFEXITED(st) && WEXITSTATUS(st) == SKIPPED) {
        printf("  [parent] limit not supported on this system -> SKIPPED\n");
        ok = 1;
    } else {
        printf("  [parent] unexpected child status 0x%x\n", st);
    }
    return ok ? 0 : 1;
}

int rlimit_run(rlimit_which_t which)
{
    static const demo_t demos[3] = {
        { "RLIMIT_NOFILE: limit the number of open files", child_nofile, 0,
          "open() refused with EMFILE (Too many open files)" },
        { "RLIMIT_AS: limit the address space (memory)", child_mem, 0,
          "malloc() returned NULL (Cannot allocate memory)" },
        { "RLIMIT_CPU: limit CPU time", child_cpu, SIGXCPU,
          "kernel enforced the CPU-time limit with SIGXCPU" },
    };
    int failures = 0, ran = 0;

    print_banner("Process resource limits (setrlimit)");
    printf("Each demo runs in a forked child, so the parent's limits never change.\n");
    for (int i = 0; i < 3; i++) {
        if (which != RL_ALL && (int)which != i)
            continue;
        ran++;
        if (run_demo(&demos[i]) != 0)
            failures++;
    }
    printf("\nSummary: %d of %d resource-limit demonstration(s) showed the expected effect\n",
           ran - failures, ran);
    printf("  RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    detail_set("%d/%d limits enforced (NOFILE->EMFILE, AS->ENOMEM, CPU->SIGXCPU)", ran - failures, ran);
    return failures == 0 ? 0 : 1;
}

/* ------------------------------------------------------------------ */

static int rlimit_usage_error(const char *msg)
{
    fprintf(stderr, "Error: %s\n"
                    "Usage: campuscompute rlimit [nofile|mem|cpu|all]\n"
                    "  nofile   RLIMIT_NOFILE: open() fails once the limit is reached\n"
                    "  mem      RLIMIT_AS:     malloc() fails once the limit is reached\n"
                    "  cpu      RLIMIT_CPU:    busy loop is killed by SIGXCPU after 1 s of CPU time\n"
                    "  all      run all three (default)\n", msg);
    return 2;
}

int rlimit_cmd(int argc, char **argv)
{
    char msg[96];

    if (argc > 2)
        return rlimit_usage_error("too many arguments");
    if (argc == 1 || strcmp(argv[1], "all") == 0) return rlimit_run(RL_ALL);
    if (strcmp(argv[1], "nofile") == 0)           return rlimit_run(RL_NOFILE);
    if (strcmp(argv[1], "mem") == 0)              return rlimit_run(RL_MEM);
    if (strcmp(argv[1], "cpu") == 0)              return rlimit_run(RL_CPU);
    snprintf(msg, sizeof msg, "unknown limit '%s'", argv[1]);
    return rlimit_usage_error(msg);
}
