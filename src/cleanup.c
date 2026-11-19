/*
 * cleanup.c - FR7: cleanup and cleanup verification.
 *
 * SAFETY RULE: this module only ever touches objects in /dev/shm whose name
 * starts with CC_IPC_PREFIX ("cc_campus_") AND whose embedded owner PID no
 * longer exists. Unrelated files and other programs' IPC objects are never
 * removed.
 */
#include "cleanup.h"
#include "common.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int pid_alive(pid_t pid)
{
    return kill(pid, 0) == 0 || errno == EPERM;
}

/* Our object names end in "_<pid>". */
static pid_t pid_from_name(const char *name)
{
    const char *us = strrchr(name, '_');
    long v;

    if (us == NULL || parse_long(us + 1, 1, INT_MAX, &v) != 0)
        return -1;
    return (pid_t)v;
}

/* Count (and optionally remove) our own stale objects. */
static int scan_dev_shm(int remove_stale, int *removed, int *skipped)
{
    DIR *d = opendir("/dev/shm");
    struct dirent *e;
    int found = 0;
    size_t plen = strlen(CC_IPC_PREFIX);

    if (d == NULL) {
        printf("  /dev/shm is not available on this system: %s\n", strerror(errno));
        return -1;
    }
    while ((e = readdir(d)) != NULL) {
        const char *base = e->d_name;
        int is_sem = 0;

        if (strncmp(base, "sem.", 4) == 0) {
            is_sem = 1;
            base += 4;
        }
        if (strncmp(base, CC_IPC_PREFIX, plen) != 0)
            continue;                                   /* not ours: never touch */

        /* Our names always end in "_<pid>"; anything else is not ours. */
        pid_t owner = pid_from_name(base);
        if (owner <= 0)
            continue;
        found++;
        if (pid_alive(owner)) {
            printf("  keep    %-40s (owner pid %d is still running)\n", e->d_name, (int)owner);
            (*skipped)++;
            found--;                                    /* in use, not stale */
            continue;
        }
        if (!remove_stale)
            continue;

        char name[300];
        snprintf(name, sizeof name, "/%s", base);
        int rc = is_sem ? sem_unlink(name) : shm_unlink(name);
        if (rc == 0) {
            printf("  removed %-40s (stale %s from finished pid %d)\n", e->d_name,
                   is_sem ? "semaphore" : "shared memory", (int)owner);
            (*removed)++;
            found--;
        } else {
            printf("  FAILED to remove %s: %s\n", e->d_name, strerror(errno));
        }
    }
    closedir(d);
    return found;
}

int cleanup_run(void)
{
    int removed = 0, skipped = 0, ok = 1;

    print_banner("Cleanup and verification");
    printf("Scanning /dev/shm for stale objects named %s* (other files are never touched):\n",
           CC_IPC_PREFIX);
    int leftover = scan_dev_shm(1, &removed, &skipped);
    if (removed == 0 && skipped == 0 && leftover == 0)
        printf("  nothing to remove\n");

    int threads = count_threads();
    int fds = count_open_fds() - g_muted_fds;   /* ignore the demo's own redirection fd */
    errno = 0;
    pid_t child = waitpid(-1, NULL, WNOHANG);
    int no_children = (child == -1 && errno == ECHILD);

    printf("\nVerification:\n");
    printf("  [%s] threads in this process          : %d (expected 1 = main thread only)\n",
           threads == 1 ? " OK " : "FAIL", threads);
    printf("  [%s] child processes not yet reaped   : %s\n",
           no_children ? " OK " : "FAIL", no_children ? "none (waitpid -> ECHILD)" : "some remain");
    printf("  [%s] leftover shared memory/semaphores: %d\n",
           leftover == 0 ? " OK " : "FAIL", leftover < 0 ? 0 : leftover);
    printf("  [%s] open file descriptors            : %d (at program start: %d)\n",
           (fds == g_fd_baseline) ? " OK " : "FAIL", fds, g_fd_baseline);

    if (threads != 1 || !no_children || leftover > 0 || fds != g_fd_baseline)
        ok = 0;
    printf("  RESULT: %s\n", ok ? "PASS - everything this program created has been released"
                               : "FAIL - see the checks above");
    if (ok)
        detail_set("1 thread, no zombies, %d IPC objects left, no fd leaks", leftover < 0 ? 0 : leftover);
    else
        detail_set("a check failed - run './campuscompute cleanup' for details");
    return ok ? 0 : 1;
}

int cleanup_cmd(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) {
        fprintf(stderr, "Error: 'cleanup' takes no arguments\n"
                        "Usage: campuscompute cleanup\n");
        return 2;
    }
    return cleanup_run();
}
