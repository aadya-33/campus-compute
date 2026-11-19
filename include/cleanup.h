/*
 * cleanup.h - FR7: remove stale program-created IPC objects and verify that
 * nothing created by the program is still alive.
 */
#ifndef CLEANUP_H
#define CLEANUP_H

/* Remove leftover IPC objects whose names start with CC_IPC_PREFIX and whose
 * owner process is gone, then verify: single thread, no child processes, no
 * IPC objects, no leaked file descriptors. Returns 0 if everything checks out. */
int cleanup_run(void);

/* "cleanup" command: argv[0] == "cleanup". */
int cleanup_cmd(int argc, char **argv);

#endif /* CLEANUP_H */
