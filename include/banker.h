/*
 * banker.h - FR5: Banker's safety algorithm.
 */
#ifndef BANKER_H
#define BANKER_H

#include <stddef.h>

/* Load a state file, print it, run the safety algorithm and print the result.
 * On success returns 0, sets *is_safe (1/0) and writes the safe sequence
 * (e.g. "P1 P3 P4 P0 P2", empty if unsafe) into seq_out (may be NULL). */
int banker_run_file(const char *path, int *is_safe, char *seq_out, size_t seq_sz);

/* "banker" command: argv[0] == "banker". */
int banker_cmd(int argc, char **argv);

#endif /* BANKER_H */
