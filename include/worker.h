/*
 * worker.h - FR6: worker processes (fork) and failure detection (waitpid).
 */
#ifndef WORKER_H
#define WORKER_H

typedef enum {
    WSCEN_NORMAL,   /* two workers that finish normally                   */
    WSCEN_FAIL,     /* one worker that exits with a non-zero status       */
    WSCEN_CRASH,    /* one worker that is killed by a signal              */
    WSCEN_ALL       /* 2 normal + 1 exit-status failure + 1 signal crash  */
} worker_scenario_t;

/* Spawn the workers of a scenario and report how each one ended.
 * Returns 0 if the parent's report matched what each worker was told to do. */
int worker_run(worker_scenario_t scenario);

/* "worker" command: argv[0] == "worker". */
int worker_cmd(int argc, char **argv);

#endif /* WORKER_H */
