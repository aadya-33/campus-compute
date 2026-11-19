/*
 * main.c - CampusCompute command-line interface (FR8).
 *
 * Usage: ./campuscompute <command> [options]
 * Exit codes: 0 = success, 1 = runtime/validation failure, 2 = usage error.
 */
#include "banker.h"
#include "cleanup.h"
#include "common.h"
#include "demo.h"
#include "ipc.h"
#include "logger.h"
#include "rlimit_demo.h"
#include "scheduler.h"
#include "worker.h"

#include <stdio.h>
#include <string.h>

static void print_usage(FILE *out)
{
    fprintf(out,
"%s v%s - Intelligent Job Scheduling and Resource Management\n"
"Operating Systems (UE25CS242A) Level 3 Mini-Project\n"
"\n"
"Usage: ./campuscompute <command> [options]\n"
"\n"
"Commands:\n"
"  help                          show this message\n"
"  sched fcfs|rr|priority|compare [-f jobs.txt] [-q quantum]\n"
"                                CPU scheduling (FR1, FR2); -q = Round Robin quantum (default 3)\n"
"  logger [-p N] [-c N] [-n N] [-s N] [-l file]\n"
"                                producer-consumer logger with POSIX threads (FR3)\n"
"  ipc    [-m N]                 shared memory + named semaphore IPC (FR4)\n"
"  banker [safe|unsafe] [-f file]\n"
"                                Banker's safety algorithm (FR5)\n"
"  worker normal|fail|crash|all  fork()/waitpid() failure detection (FR6)\n"
"  rlimit nofile|mem|cpu|all     setrlimit() demonstrations (FR7)\n"
"  cleanup                       remove stale program IPC objects and verify cleanup (FR7)\n"
"  demo   [-v]                   integrated demonstration of everything\n"
"\n"
"Examples:\n"
"  ./campuscompute sched compare -q 3\n"
"  ./campuscompute banker unsafe\n"
"  ./campuscompute worker all\n"
"Run a command with a bad option to see its own usage text.\n",
            CC_NAME, CC_VERSION);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);       /* line-buffered: clean output even when piped */
    g_fd_baseline = count_open_fds();

    if (argc < 2) {
        fprintf(stderr, "Error: no command given\n\n");
        print_usage(stderr);
        return 2;
    }

    const char *cmd = argv[1];
    int sub_argc = argc - 1;
    char **sub_argv = argv + 1;             /* sub_argv[0] is the command name */

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0) {
        print_usage(stdout);
        return 0;
    }
    if (strcmp(cmd, "--version") == 0 || strcmp(cmd, "version") == 0) {
        printf("%s %s\n", CC_NAME, CC_VERSION);
        return 0;
    }
    if (strcmp(cmd, "sched") == 0)    return sched_cmd(sub_argc, sub_argv);
    if (strcmp(cmd, "logger") == 0)   return logger_cmd(sub_argc, sub_argv);
    if (strcmp(cmd, "ipc") == 0)      return ipc_cmd(sub_argc, sub_argv);
    if (strcmp(cmd, "banker") == 0)   return banker_cmd(sub_argc, sub_argv);
    if (strcmp(cmd, "worker") == 0)   return worker_cmd(sub_argc, sub_argv);
    if (strcmp(cmd, "rlimit") == 0)   return rlimit_cmd(sub_argc, sub_argv);
    if (strcmp(cmd, "cleanup") == 0)  return cleanup_cmd(sub_argc, sub_argv);
    if (strcmp(cmd, "demo") == 0)     return demo_cmd(sub_argc, sub_argv);

    fprintf(stderr, "Error: unknown command '%s'\n"
                    "Run './campuscompute help' to see the available commands.\n", cmd);
    return 2;
}
