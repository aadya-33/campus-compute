/*
 * common.h - small helpers shared by every CampusCompute module.
 */
#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>

#define CC_NAME    "CampusCompute"
#define CC_VERSION "1.0"

/* Every IPC object created by this program has a name starting with this
 * prefix, so the cleanup command can recognise (and ONLY touch) its own
 * objects and never anybody else's. */
#define CC_IPC_PREFIX "cc_campus_"

/* Number of open file descriptors when the program started (set by main). */
extern int g_fd_baseline;

/* Descriptors currently held by mute_fd() (demo output redirection). They are
 * legitimate bookkeeping, so the leak check must not count them. */
extern int g_muted_fds;

/* Strict integer parser: the whole string must be a base-10 integer in [lo,hi].
 * Returns 0 and stores the value in *out on success, -1 otherwise. */
int parse_long(const char *s, long lo, long hi, long *out);

/* Every module leaves a one-line "evidence" string behind for the integrated
 * demo summary. */
void detail_set(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
const char *detail_get(void);

/* Print a section banner such as "=== FCFS ===". */
void print_banner(const char *title);

/* Make sure the directory part of 'path' exists (one level is created). */
int ensure_parent_dir(const char *path);

/* Temporarily send fd (1 = stdout, 2 = stderr) to /dev/null.
 * mute_fd() returns a token that must be handed back to unmute_fd(). */
int mute_fd(int fd);
void unmute_fd(int fd, int saved);

/* Count open file descriptors / threads of this process through /proc. */
int count_open_fds(void);
int count_threads(void);

#endif /* COMMON_H */
