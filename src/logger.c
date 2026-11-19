/*
 * logger.c - FR3: concurrent activity logger.
 *
 * Design
 *   - A fixed-size circular queue (bounded buffer) sits between PRODUCER
 *     threads (they generate system events) and CONSUMER threads (they write
 *     the events to the log file).
 *   - One mutex protects the queue indexes and counters.
 *   - Condition variable 'not_full'  : producers wait here when the queue is full.
 *     Condition variable 'not_empty' : consumers wait here when the queue is empty.
 *     (Both waits are in while-loops, so spurious wake-ups are harmless.)
 *   - A second mutex serialises writes to the log file so lines never mix.
 *   - Shutdown: after every producer has been joined, main sets 'done' and
 *     broadcasts 'not_empty'. Consumers keep draining the queue and only exit
 *     when it is empty AND done is set, so no event is lost. All threads are
 *     then joined (no detached or leaked threads).
 */
#include "logger.h"
#include "common.h"

#include <getopt.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAX_THREADS 8
#define MSG_LEN     80

typedef struct {
    int producer;
    int seq;
    struct timespec ts;
    char msg[MSG_LEN];
} log_event_t;

typedef struct {
    log_event_t buf[LOG_MAX_CAP];
    int cap, head, tail, count;
    int done;
    int items_per_producer;

    pthread_mutex_t mtx;            /* protects everything above and the counters below */
    pthread_cond_t not_full;
    pthread_cond_t not_empty;

    int produced, consumed;
    int max_depth, full_waits, empty_waits;

    FILE *logf;
    pthread_mutex_t log_mtx;        /* serialises log-file writes */
    int log_lines;
} logq_t;

typedef struct {
    logq_t *q;
    int id;
} thread_arg_t;

static const char *SOURCES[MAX_THREADS] = {
    "Scheduler", "JobManager", "IPC-Monitor", "WorkerPool",
    "Banker", "ResourceMon", "Auditor", "Console"
};
static const char *VERBS[] = {
    "admitted to ready queue", "dispatched to CPU", "preempted by scheduler",
    "resumed on CPU", "completed successfully"
};
#define NVERBS ((int)(sizeof VERBS / sizeof VERBS[0]))

/* ------------------------------------------------------------------ */

static void format_time(const struct timespec *ts, char *out, size_t n)
{
    struct tm tmv;
    char base[32];

    localtime_r(&ts->tv_sec, &tmv);
    strftime(base, sizeof base, "%Y-%m-%d %H:%M:%S", &tmv);
    snprintf(out, n, "%s.%03ld", base, ts->tv_nsec / 1000000L);
}

/* Consumer side: append one event to the log file. */
static void write_log(logq_t *q, int consumer, const log_event_t *ev)
{
    char when[48];

    format_time(&ev->ts, when, sizeof when);
    pthread_mutex_lock(&q->log_mtx);
    q->log_lines++;
    fprintf(q->logf, "[%s] #%03d consumer=C%d source=%s(P%d) seq=%d | %s\n",
            when, q->log_lines, consumer, SOURCES[(ev->producer - 1) % MAX_THREADS],
            ev->producer, ev->seq, ev->msg);
    fflush(q->logf);
    pthread_mutex_unlock(&q->log_mtx);
}

static void *producer_main(void *arg)
{
    thread_arg_t *a = arg;
    logq_t *q = a->q;

    for (int s = 1; s <= q->items_per_producer; s++) {
        log_event_t ev;

        pthread_mutex_lock(&q->mtx);
        int announced = 0;
        while (q->count == q->cap) {                    /* queue FULL -> wait */
            if (!announced) {
                printf("  [P%d] queue FULL (%d/%d) -> waiting on 'not_full'\n",
                       a->id, q->count, q->cap);
                q->full_waits++;
                announced = 1;
            }
            pthread_cond_wait(&q->not_full, &q->mtx);
        }

        ev.producer = a->id;
        ev.seq = s;
        clock_gettime(CLOCK_REALTIME, &ev.ts);
        snprintf(ev.msg, sizeof ev.msg, "job J%d %s", a->id * 10 + s, VERBS[(s - 1) % NVERBS]);

        q->buf[q->tail] = ev;
        q->tail = (q->tail + 1) % q->cap;
        q->count++;
        q->produced++;
        if (q->count > q->max_depth)
            q->max_depth = q->count;
        printf("  [P%d] produced seq=%d \"%s\"  (queue %d/%d)\n",
               a->id, s, ev.msg, q->count, q->cap);
        pthread_cond_signal(&q->not_empty);
        pthread_mutex_unlock(&q->mtx);

        usleep(20000 + 10000 * (unsigned)a->id);        /* fixed delays -> repeatable behaviour */
    }
    printf("  [P%d] finished producing %d events\n", a->id, q->items_per_producer);
    return NULL;
}

static void *consumer_main(void *arg)
{
    thread_arg_t *a = arg;
    logq_t *q = a->q;
    int handled = 0;

    for (;;) {
        log_event_t ev;

        pthread_mutex_lock(&q->mtx);
        int announced = 0;
        while (q->count == 0 && !q->done) {             /* queue EMPTY -> wait */
            if (!announced) {
                printf("  [C%d] queue EMPTY -> waiting on 'not_empty'\n", a->id);
                q->empty_waits++;
                announced = 1;
            }
            pthread_cond_wait(&q->not_empty, &q->mtx);
        }
        if (q->count == 0 && q->done) {                 /* drained and no more producers */
            pthread_mutex_unlock(&q->mtx);
            break;
        }

        ev = q->buf[q->head];
        q->head = (q->head + 1) % q->cap;
        q->count--;
        q->consumed++;
        printf("  [C%d] consumed  P%d seq=%d  (queue %d/%d)\n",
               a->id, ev.producer, ev.seq, q->count, q->cap);
        pthread_cond_signal(&q->not_full);
        pthread_mutex_unlock(&q->mtx);

        write_log(q, a->id, &ev);                       /* slow I/O outside the queue lock */
        handled++;
        usleep(60000);
    }
    printf("  [C%d] queue drained and producers finished -> exiting (logged %d events)\n",
           a->id, handled);
    return NULL;
}

/* ------------------------------------------------------------------ */

int logger_run(int producers, int consumers, int items, int cap, const char *logfile)
{
    static logq_t q;                        /* static: the buffer is large */
    pthread_t pt[MAX_THREADS], ct[MAX_THREADS];
    thread_arg_t pa[MAX_THREADS], ca[MAX_THREADS];
    int pcreated = 0, ccreated = 0, failed = 0;
    char when[48];
    struct timespec now;

    memset(&q, 0, sizeof q);
    q.cap = cap;
    q.items_per_producer = items;

    if (ensure_parent_dir(logfile) != 0)
        return 1;
    q.logf = fopen(logfile, "w");
    if (q.logf == NULL) {
        perror("Error: cannot open log file");
        return 1;
    }
    if (pthread_mutex_init(&q.mtx, NULL) != 0 || pthread_mutex_init(&q.log_mtx, NULL) != 0 ||
        pthread_cond_init(&q.not_full, NULL) != 0 || pthread_cond_init(&q.not_empty, NULL) != 0) {
        fprintf(stderr, "Error: cannot initialise mutex/condition variables\n");
        fclose(q.logf);
        return 1;
    }

    clock_gettime(CLOCK_REALTIME, &now);
    format_time(&now, when, sizeof when);
    fprintf(q.logf, "# CampusCompute activity log - started %s\n", when);
    fprintf(q.logf, "# producers=%d consumers=%d events/producer=%d queue-size=%d\n",
            producers, consumers, items, cap);

    print_banner("Concurrent activity logger (bounded producer-consumer queue)");
    printf("Config: %d producer(s), %d consumer(s), %d event(s) each, queue capacity %d\n",
           producers, consumers, items, cap);
    printf("Sync  : 1 mutex + condition variables 'not_full' / 'not_empty'; log file: %s\n\n", logfile);

    /* consumers first, so the "queue EMPTY" wait is visible */
    for (int i = 0; i < consumers; i++) {
        ca[i].q = &q;
        ca[i].id = i + 1;
        if (pthread_create(&ct[i], NULL, consumer_main, &ca[i]) != 0) {
            fprintf(stderr, "Error: cannot create consumer thread %d\n", i + 1);
            failed = 1;
            break;
        }
        ccreated++;
    }
    for (int i = 0; i < producers && !failed; i++) {
        pa[i].q = &q;
        pa[i].id = i + 1;
        if (pthread_create(&pt[i], NULL, producer_main, &pa[i]) != 0) {
            fprintf(stderr, "Error: cannot create producer thread %d\n", i + 1);
            failed = 1;
            break;
        }
        pcreated++;
    }

    /* orderly shutdown: producers first, then tell consumers no more is coming */
    for (int i = 0; i < pcreated; i++) {
        pthread_join(pt[i], NULL);
        printf("  [main] joined producer thread P%d\n", i + 1);
    }
    pthread_mutex_lock(&q.mtx);
    q.done = 1;
    pthread_cond_broadcast(&q.not_empty);
    pthread_mutex_unlock(&q.mtx);
    printf("  [main] all producers joined -> 'done' flag set, consumers woken\n");
    for (int i = 0; i < ccreated; i++) {
        pthread_join(ct[i], NULL);
        printf("  [main] joined consumer thread C%d\n", i + 1);
    }

    clock_gettime(CLOCK_REALTIME, &now);
    fprintf(q.logf, "# log closed - %d entries written\n", q.log_lines);
    fclose(q.logf);
    pthread_mutex_destroy(&q.mtx);
    pthread_mutex_destroy(&q.log_mtx);
    pthread_cond_destroy(&q.not_full);
    pthread_cond_destroy(&q.not_empty);

    int expected = producers * items;
    int ok = !failed && q.produced == expected && q.consumed == expected &&
             q.log_lines == expected && q.count == 0;

    printf("\nSummary:\n");
    printf("  events expected / produced / consumed / logged : %d / %d / %d / %d\n",
           expected, q.produced, q.consumed, q.log_lines);
    printf("  maximum queue depth reached                    : %d of %d\n", q.max_depth, q.cap);
    printf("  producer waits (queue full) / consumer waits (queue empty): %d / %d\n",
           q.full_waits, q.empty_waits);
    printf("  threads running after joins                    : %d (main only)\n", count_threads());
    printf("  log file                                       : %s\n", logfile);
    printf("  RESULT: %s\n", ok ? "PASS - no event lost or duplicated, all threads joined"
                               : "FAIL");
    detail_set("%d produced = %d consumed = %d log lines; all %d threads joined",
               q.produced, q.consumed, q.log_lines, pcreated + ccreated);
    return ok ? 0 : 1;
}

/* ------------------------------------------------------------------ */

static int logger_usage_error(const char *msg)
{
    fprintf(stderr, "Error: %s\n"
                    "Usage: campuscompute logger [-p producers] [-c consumers] [-n events] [-s queue_size] [-l logfile]\n"
                    "  -p N   producer threads, 1..%d   (default 3)\n"
                    "  -c N   consumer threads, 1..%d   (default 2)\n"
                    "  -n N   events per producer, 1..100 (default 6)\n"
                    "  -s N   queue capacity, 1..%d     (default 4)\n"
                    "  -l F   log file                  (default %s)\n",
            msg, MAX_THREADS, MAX_THREADS, LOG_MAX_CAP, LOG_DEFAULT_FILE);
    return 2;
}

int logger_cmd(int argc, char **argv)
{
    long p = 3, c = 2, n = 6, s = 4;
    const char *logfile = LOG_DEFAULT_FILE;
    int opt;
    char msg[96];

    opterr = 0;
    optind = 1;
    while ((opt = getopt(argc, argv, ":p:c:n:s:l:")) != -1) {
        long *target = NULL, hi = 0;
        switch (opt) {
        case 'p': target = &p; hi = MAX_THREADS; break;
        case 'c': target = &c; hi = MAX_THREADS; break;
        case 'n': target = &n; hi = 100;         break;
        case 's': target = &s; hi = LOG_MAX_CAP; break;
        case 'l': logfile = optarg;              break;
        case ':':
            snprintf(msg, sizeof msg, "option -%c needs a value", optopt);
            return logger_usage_error(msg);
        default:
            snprintf(msg, sizeof msg, "unknown option -%c", optopt);
            return logger_usage_error(msg);
        }
        if (target != NULL && parse_long(optarg, 1, hi, target) != 0) {
            snprintf(msg, sizeof msg, "invalid value '%s' for -%c (must be 1..%ld)", optarg, opt, hi);
            return logger_usage_error(msg);
        }
    }
    if (optind < argc) {
        snprintf(msg, sizeof msg, "unexpected argument '%s'", argv[optind]);
        return logger_usage_error(msg);
    }
    return logger_run((int)p, (int)c, (int)n, (int)s, logfile);
}
