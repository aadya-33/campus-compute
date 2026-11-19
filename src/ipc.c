/*
 * ipc.c - FR4: POSIX shared memory + named semaphores between processes.
 *
 * Scenario: two WRITER processes (children) send messages to one READER
 * process (the parent) through a small ring buffer that lives in POSIX shared
 * memory. Three named semaphores coordinate them (classic bounded buffer):
 *
 *     sem_empty  counts free slots   (starts at IPC_SLOTS)  - writers wait on it
 *     sem_full   counts filled slots (starts at 0)           - the reader waits on it
 *     sem_mutex  binary lock protecting the 'in' index and slot writes
 *
 * Every object name contains the creator's PID, so concurrent runs cannot
 * collide and the cleanup command can tell whose objects are stale.
 * The parent always unlinks what it created, even on error paths.
 */
#include "ipc.h"
#include "common.h"

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define IPC_SLOTS    2          /* small on purpose: writers sometimes have to block */
#define IPC_WRITERS  2
#define READ_TIMEOUT 5          /* seconds the reader waits for a message */

typedef struct {
    int  writer;
    int  seq;
    long payload;
    long checksum;
    char text[40];
} ipc_msg_t;

typedef struct {
    ipc_msg_t slot[IPC_SLOTS];
    int in;                     /* next slot to write (protected by sem_mutex) */
    int out;                    /* next slot to read  (only the reader uses it) */
} ipc_shm_t;

typedef struct {
    char shm[64];
    char empty[64];
    char full[64];
    char mutex[64];
} ipc_names_t;

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

static void make_names(ipc_names_t *n)
{
    int pid = (int)getpid();

    snprintf(n->shm,   sizeof n->shm,   "/%sshm_%d",        CC_IPC_PREFIX, pid);
    snprintf(n->empty, sizeof n->empty, "/%ssem_empty_%d",  CC_IPC_PREFIX, pid);
    snprintf(n->full,  sizeof n->full,  "/%ssem_full_%d",   CC_IPC_PREFIX, pid);
    snprintf(n->mutex, sizeof n->mutex, "/%ssem_mutex_%d",  CC_IPC_PREFIX, pid);
}

/* On Linux, POSIX shm objects appear as /dev/shm/<name> and named semaphores
 * as /dev/shm/sem.<name>; we use that to PROVE creation and removal. */
static int exists_shm(const char *name)
{
    char path[320];
    struct stat st;

    snprintf(path, sizeof path, "/dev/shm%s", name);
    return stat(path, &st) == 0;
}

static int exists_sem(const char *name)
{
    char path[320];
    struct stat st;

    snprintf(path, sizeof path, "/dev/shm/sem.%s", name + 1);
    return stat(path, &st) == 0;
}

static void show_objects(const ipc_names_t *n, const char *when)
{
    printf("  IPC objects %s:\n", when);
    printf("    shared memory %-30s : %s\n", n->shm,   exists_shm(n->shm)   ? "EXISTS" : "absent");
    printf("    semaphore     %-30s : %s\n", n->empty, exists_sem(n->empty) ? "EXISTS" : "absent");
    printf("    semaphore     %-30s : %s\n", n->full,  exists_sem(n->full)  ? "EXISTS" : "absent");
    printf("    semaphore     %-30s : %s\n", n->mutex, exists_sem(n->mutex) ? "EXISTS" : "absent");
}

static int sem_wait_retry(sem_t *s)
{
    int rc;

    while ((rc = sem_wait(s)) == -1 && errno == EINTR)
        ;
    return rc;
}

static long make_checksum(long payload, int writer, int seq)
{
    return payload * 31 + writer * 7 + seq;
}

/* ------------------------------------------------------------------ */
/* Writer process: opens everything BY NAME, like an unrelated program  */
/* would, writes its messages, then exits.                              */
/* ------------------------------------------------------------------ */

static int writer_process(const ipc_names_t *nm, int wid, int count)
{
    char tag = (char)('A' + wid - 1);
    int rc = 1, fd = -1;
    ipc_shm_t *shm = MAP_FAILED;
    sem_t *empty = SEM_FAILED, *full = SEM_FAILED, *mutex = SEM_FAILED;

    fd = shm_open(nm->shm, O_RDWR, 0);
    if (fd < 0) { perror("writer: shm_open"); goto out; }
    shm = mmap(NULL, sizeof *shm, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) { perror("writer: mmap"); goto out; }
    empty = sem_open(nm->empty, 0);
    full  = sem_open(nm->full,  0);
    mutex = sem_open(nm->mutex, 0);
    if (empty == SEM_FAILED || full == SEM_FAILED || mutex == SEM_FAILED) {
        perror("writer: sem_open");
        goto out;
    }

    for (int i = 1; i <= count; i++) {
        long payload = wid * 100L + (long)i * i;

        if (sem_trywait(empty) != 0) {
            if (errno == EAGAIN) {
                printf("  [WRITER-%c pid=%d] buffer FULL -> blocking on semaphore 'empty'\n",
                       tag, (int)getpid());
                fflush(stdout);
                if (sem_wait_retry(empty) != 0) { perror("writer: sem_wait"); goto out; }
            } else if (errno != EINTR) {
                perror("writer: sem_trywait");
                goto out;
            } else if (sem_wait_retry(empty) != 0) {
                perror("writer: sem_wait");
                goto out;
            }
        }
        if (sem_wait_retry(mutex) != 0) { perror("writer: sem_wait(mutex)"); goto out; }

        ipc_msg_t *m = &shm->slot[shm->in];         /* critical section */
        m->writer   = wid;
        m->seq      = i;
        m->payload  = payload;
        m->checksum = make_checksum(payload, wid, i);
        snprintf(m->text, sizeof m->text, "reading %d from Writer-%c", i, tag);
        printf("  [WRITER-%c pid=%d] wrote   seq=%d payload=%ld into slot %d\n",
               tag, (int)getpid(), i, payload, shm->in);
        fflush(stdout);
        shm->in = (shm->in + 1) % IPC_SLOTS;

        sem_post(mutex);
        sem_post(full);                             /* tell the reader a slot is filled */
        usleep(40000u * (unsigned)wid);
    }
    rc = 0;

out:
    if (empty != SEM_FAILED) sem_close(empty);
    if (full  != SEM_FAILED) sem_close(full);
    if (mutex != SEM_FAILED) sem_close(mutex);
    if (shm != MAP_FAILED)   munmap(shm, sizeof *shm);
    if (fd >= 0)             close(fd);
    return rc;
}

/* ------------------------------------------------------------------ */
/* Parent: creator, reader and janitor                                  */
/* ------------------------------------------------------------------ */

int ipc_run(int msgs_per_writer)
{
    ipc_names_t nm;
    ipc_shm_t *shm = MAP_FAILED;
    sem_t *s_empty = SEM_FAILED, *s_full = SEM_FAILED, *s_mutex = SEM_FAILED;
    int fd = -1, shm_created = 0, rc = 1;
    int empty_created = 0, full_created = 0, mutex_created = 0;
    pid_t pids[IPC_WRITERS] = {0};
    int spawned = 0;
    int per_writer[IPC_WRITERS] = {0}, bad_checksums = 0, received = 0;
    int total = IPC_WRITERS * msgs_per_writer;
    struct sigaction sa, old_int, old_term;

    make_names(&nm);
    print_banner("POSIX shared-memory IPC with named semaphores");
    printf("Reader (parent) pid=%d, %d writer processes, %d messages each, %d buffer slots\n\n",
           (int)getpid(), IPC_WRITERS, msgs_per_writer, IPC_SLOTS);

    /* Ctrl+C must not leave IPC objects behind: the handler only sets a flag
     * (no SA_RESTART, so a blocked sem_timedwait returns EINTR). */
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, &old_int);
    sigaction(SIGTERM, &sa, &old_term);
    g_stop = 0;

    /* ---- create ---- */
    fd = shm_open(nm.shm, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) { perror("Error: shm_open"); goto cleanup; }
    shm_created = 1;
    if (ftruncate(fd, (off_t)sizeof *shm) != 0) { perror("Error: ftruncate"); goto cleanup; }
    shm = mmap(NULL, sizeof *shm, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) { perror("Error: mmap"); goto cleanup; }
    memset(shm, 0, sizeof *shm);

    s_empty = sem_open(nm.empty, O_CREAT | O_EXCL, 0600, IPC_SLOTS);
    if (s_empty == SEM_FAILED) { perror("Error: sem_open(empty)"); goto cleanup; }
    empty_created = 1;
    s_full = sem_open(nm.full, O_CREAT | O_EXCL, 0600, 0);
    if (s_full == SEM_FAILED) { perror("Error: sem_open(full)"); goto cleanup; }
    full_created = 1;
    s_mutex = sem_open(nm.mutex, O_CREAT | O_EXCL, 0600, 1);
    if (s_mutex == SEM_FAILED) { perror("Error: sem_open(mutex)"); goto cleanup; }
    mutex_created = 1;

    printf("Created shared memory (%zu bytes) and 3 named semaphores "
           "(empty=%d, full=0, mutex=1)\n", sizeof *shm, IPC_SLOTS);
    show_objects(&nm, "after creation");
    printf("\n");

    /* ---- fork the writers ---- */
    fflush(NULL);                                   /* never duplicate buffered output */
    for (int w = 1; w <= IPC_WRITERS; w++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("Error: fork");
            goto reap;
        }
        if (pid == 0) {
            /* children must not inherit the parent's flag-only handlers,
             * otherwise Ctrl+C / SIGTERM could never stop a blocked writer */
            signal(SIGINT, SIG_DFL);
            signal(SIGTERM, SIG_DFL);
            _exit(writer_process(&nm, w, msgs_per_writer));
        }
        pids[spawned++] = pid;
    }

    /* ---- read everything the writers send ---- */
    for (int k = 0; k < total; k++) {
        struct timespec dl;
        int r;

        if (g_stop) {                               /* Ctrl+C / SIGTERM arrived */
            fprintf(stderr, "Error: interrupted - stopping writers and cleaning up IPC objects\n");
            goto reap;
        }
        clock_gettime(CLOCK_REALTIME, &dl);
        dl.tv_sec += READ_TIMEOUT;
        while ((r = sem_timedwait(s_full, &dl)) == -1 && errno == EINTR && !g_stop)
            ;
        if (r != 0) {
            if (g_stop)
                fprintf(stderr, "Error: interrupted - cleaning up IPC objects\n");
            else
                fprintf(stderr, "Error: no message within %d s (%s) - writers stuck or dead\n",
                        READ_TIMEOUT, strerror(errno));
            goto reap;
        }

        ipc_msg_t m = shm->slot[shm->out];
        shm->out = (shm->out + 1) % IPC_SLOTS;
        int good = m.checksum == make_checksum(m.payload, m.writer, m.seq);
        printf("  [READER pid=%d] received seq=%d from Writer-%c payload=%ld \"%s\" checksum %s\n",
               (int)getpid(), m.seq, 'A' + m.writer - 1, m.payload, m.text,
               good ? "OK" : "BAD");
        fflush(stdout);
        if (!good)
            bad_checksums++;
        if (m.writer >= 1 && m.writer <= IPC_WRITERS)
            per_writer[m.writer - 1]++;
        received++;
        sem_post(s_empty);                          /* slot is free again */
        usleep(120000);                             /* slow reader -> writers sometimes block */
    }
    rc = 0;

reap:
    /* collect every child we created (kill first if we are bailing out early) */
    for (int i = 0; i < spawned; i++) {
        int st = 0;
        if (rc != 0)
            kill(pids[i], SIGTERM);
        while (waitpid(pids[i], &st, 0) < 0 && errno == EINTR)
            ;
        if (rc == 0) {
            if (WIFEXITED(st) && WEXITSTATUS(st) == 0)
                printf("  [main] writer pid=%d exited normally (status 0)\n", (int)pids[i]);
            else {
                printf("  [main] writer pid=%d FAILED (wait status 0x%x)\n", (int)pids[i], st);
                rc = 1;
            }
        }
    }
    if (rc == 0 && (received != total || bad_checksums != 0))
        rc = 1;

cleanup:
    /* ---- remove ONLY what this run created ---- */
    printf("\nCleaning up IPC resources created by this run...\n");
    if (s_empty != SEM_FAILED) sem_close(s_empty);
    if (s_full  != SEM_FAILED) sem_close(s_full);
    if (s_mutex != SEM_FAILED) sem_close(s_mutex);
    if (shm != MAP_FAILED)     munmap(shm, sizeof *shm);
    if (fd >= 0)               close(fd);
    if (empty_created) sem_unlink(nm.empty);
    if (full_created)  sem_unlink(nm.full);
    if (mutex_created) sem_unlink(nm.mutex);
    if (shm_created)   shm_unlink(nm.shm);
    show_objects(&nm, "after cleanup");

    sigaction(SIGINT,  &old_int,  NULL);
    sigaction(SIGTERM, &old_term, NULL);

    int leaked = exists_shm(nm.shm) + exists_sem(nm.empty) +
                 exists_sem(nm.full) + exists_sem(nm.mutex);
    printf("\nSummary:\n");
    printf("  messages sent / received         : %d / %d\n", total, received);
    printf("  per writer (A, B)                : %d, %d\n", per_writer[0], per_writer[1]);
    printf("  checksum failures                : %d\n", bad_checksums);
    printf("  IPC objects left behind          : %d\n", leaked);
    printf("  RESULT: %s\n", (rc == 0 && leaked == 0) ? "PASS - data exchanged, IPC objects removed"
                                                       : "FAIL");
    if (rc == 0 && leaked == 0)
        detail_set("%d msgs via shared memory, 3 named semaphores, 0 objects left", received);
    return (rc == 0 && leaked == 0) ? 0 : 1;
}

/* ------------------------------------------------------------------ */

static int ipc_usage_error(const char *msg)
{
    fprintf(stderr, "Error: %s\n"
                    "Usage: campuscompute ipc [-m messages_per_writer]\n"
                    "  -m N   messages each writer sends, 1..50 (default 4)\n", msg);
    return 2;
}

int ipc_cmd(int argc, char **argv)
{
    long m = 4;
    int opt;
    char msg[96];

    opterr = 0;
    optind = 1;
    while ((opt = getopt(argc, argv, ":m:")) != -1) {
        switch (opt) {
        case 'm':
            if (parse_long(optarg, 1, 50, &m) != 0) {
                snprintf(msg, sizeof msg, "invalid message count '%s' (must be 1..50)", optarg);
                return ipc_usage_error(msg);
            }
            break;
        case ':':
            snprintf(msg, sizeof msg, "option -%c needs a value", optopt);
            return ipc_usage_error(msg);
        default:
            snprintf(msg, sizeof msg, "unknown option -%c", optopt);
            return ipc_usage_error(msg);
        }
    }
    if (optind < argc) {
        snprintf(msg, sizeof msg, "unexpected argument '%s'", argv[optind]);
        return ipc_usage_error(msg);
    }
    return ipc_run((int)m);
}
