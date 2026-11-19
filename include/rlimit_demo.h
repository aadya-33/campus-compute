/*
 * rlimit_demo.h - FR7: process resource limits with setrlimit().
 */
#ifndef RLIMIT_DEMO_H
#define RLIMIT_DEMO_H

typedef enum {
    RL_NOFILE,      /* RLIMIT_NOFILE: open() fails with EMFILE            */
    RL_MEM,         /* RLIMIT_AS    : malloc() fails with ENOMEM          */
    RL_CPU,         /* RLIMIT_CPU   : the kernel sends SIGXCPU            */
    RL_ALL
} rlimit_which_t;

/* Run one (or all) demonstrations, each in a forked child so the parent's own
 * limits never change. Returns 0 if every observed effect was as expected. */
int rlimit_run(rlimit_which_t which);

/* "rlimit" command: argv[0] == "rlimit". */
int rlimit_cmd(int argc, char **argv);

#endif /* RLIMIT_DEMO_H */
