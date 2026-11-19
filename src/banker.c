/*
 * banker.c - FR5: Banker's safety algorithm.
 *
 * State file format ('#' starts a comment, whitespace separated integers):
 *     n m                 number of processes, number of resource types
 *     Available[m]
 *     Maximum[n][m]       n rows of m numbers
 *     Allocation[n][m]    n rows of m numbers
 *
 * Need = Maximum - Allocation.
 *
 * Safety algorithm:
 *   1. Work = Available, Finish[i] = false for all i.
 *   2. Find an i with Finish[i] == false and Need[i] <= Work.
 *   3. Work = Work + Allocation[i]; Finish[i] = true; append i to the sequence;
 *      go back to step 2.
 *   4. If every Finish[i] is true the state is SAFE (the sequence is a safe
 *      sequence); if no such i exists and some are still false it is UNSAFE.
 */
#include "banker.h"
#include "common.h"

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define B_MAXP 16
#define B_MAXR 8
#define SAFE_FILE   "data/banker_safe.txt"
#define UNSAFE_FILE "data/banker_unsafe.txt"
#define MAX_TOKENS  (2 + B_MAXR + 2 * B_MAXP * B_MAXR)

typedef struct {
    int n, m;
    int avail[B_MAXR];
    int max[B_MAXP][B_MAXR];
    int alloc[B_MAXP][B_MAXR];
    int need[B_MAXP][B_MAXR];
} bank_t;

/* ------------------------------------------------------------------ */
/* loading and validation                                              */
/* ------------------------------------------------------------------ */

static int bank_load(const char *path, bank_t *b)
{
    FILE *f = fopen(path, "r");
    long vals[MAX_TOKENS + 1];
    int nv = 0, lineno = 0, overflow = 0;
    char line[512];

    if (f == NULL) {
        fprintf(stderr, "Error: cannot open state file '%s': %s\n"
                        "       (run from the project directory or pass -f <file>)\n",
                path, strerror(errno));
        return -1;
    }
    while (fgets(line, sizeof line, f) != NULL) {
        lineno++;
        char *hash = strchr(line, '#');
        if (hash != NULL)
            *hash = '\0';
        for (char *p = strtok(line, " \t\r\n"); p != NULL; p = strtok(NULL, " \t\r\n")) {
            long v;
            if (parse_long(p, 0, 100000, &v) != 0) {
                fprintf(stderr, "Error: %s:%d: '%s' is not a non-negative integer\n", path, lineno, p);
                fclose(f);
                return -1;
            }
            if (nv < MAX_TOKENS)
                vals[nv++] = v;
            else
                overflow = 1;
        }
    }
    fclose(f);

    if (overflow || nv < 2) {
        fprintf(stderr, "Error: %s: %s\n", path, overflow ? "too many values" : "missing 'n m' header");
        return -1;
    }
    if (vals[0] < 1 || vals[0] > B_MAXP || vals[1] < 1 || vals[1] > B_MAXR) {
        fprintf(stderr, "Error: %s: need 1..%d processes and 1..%d resource types (got %ld and %ld)\n",
                path, B_MAXP, B_MAXR, vals[0], vals[1]);
        return -1;
    }
    b->n = (int)vals[0];
    b->m = (int)vals[1];
    int expected = 2 + b->m + 2 * b->n * b->m;
    if (nv != expected) {
        fprintf(stderr, "Error: %s: expected %d numbers (n m, Available, Maximum, Allocation) but found %d\n",
                path, expected, nv);
        return -1;
    }

    int k = 2;
    for (int j = 0; j < b->m; j++)
        b->avail[j] = (int)vals[k++];
    for (int i = 0; i < b->n; i++)
        for (int j = 0; j < b->m; j++)
            b->max[i][j] = (int)vals[k++];
    for (int i = 0; i < b->n; i++)
        for (int j = 0; j < b->m; j++)
            b->alloc[i][j] = (int)vals[k++];

    for (int i = 0; i < b->n; i++) {
        for (int j = 0; j < b->m; j++) {
            if (b->alloc[i][j] > b->max[i][j]) {
                fprintf(stderr, "Error: %s: Allocation[P%d][%c] = %d exceeds Maximum = %d\n",
                        path, i, 'A' + j, b->alloc[i][j], b->max[i][j]);
                return -1;
            }
            b->need[i][j] = b->max[i][j] - b->alloc[i][j];     /* Need = Max - Alloc */
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* printing                                                            */
/* ------------------------------------------------------------------ */

static void print_vec(const int *v, int m)
{
    printf("[");
    for (int j = 0; j < m; j++)
        printf("%s%d", j ? " " : "", v[j]);
    printf("]");
}

/* Render one matrix row such as "0 1 0" into buf. */
static void row_str(char *buf, size_t n, const int *row, int m)
{
    size_t len = 0;

    buf[0] = '\0';
    for (int j = 0; j < m && len < n; j++)
        len += (size_t)snprintf(buf + len, n - len, "%s%d", j ? " " : "", row[j]);
}

static void print_matrices(const bank_t *b)
{
    int w = b->m * 2 - 1;                       /* printed width of "0 1 0" */
    char hdr[64] = "", a[64], x[64], d[64];

    if (w < 5)
        w = 5;                                  /* wide enough for the word "Alloc" */
    for (int j = 0; j < b->m; j++)
        snprintf(hdr + strlen(hdr), sizeof hdr - strlen(hdr), "%s%c", j ? " " : "", 'A' + j);

    printf("  %-4s | %-*s | %-*s | %-*s |\n", "", w, "Alloc", w, "Max", w, "Need");
    printf("  %-4s | %-*s | %-*s | %-*s |\n", "", w, hdr, w, hdr, w, hdr);
    for (int i = 0; i < b->n; i++) {
        row_str(a, sizeof a, b->alloc[i], b->m);
        row_str(x, sizeof x, b->max[i], b->m);
        row_str(d, sizeof d, b->need[i], b->m);
        printf("  P%-3d | %-*s | %-*s | %-*s |\n", i, w, a, w, x, w, d);
    }
    printf("  Available = ");
    print_vec(b->avail, b->m);
    printf("\n");
}

/* ------------------------------------------------------------------ */
/* the safety algorithm                                                */
/* ------------------------------------------------------------------ */

static int vec_le(const int *a, const int *b, int m)
{
    for (int j = 0; j < m; j++)
        if (a[j] > b[j])
            return 0;
    return 1;
}

/* Returns 1 if safe (sequence in seq[0..*nseq-1]), 0 if unsafe. */
static int bank_safety(const bank_t *b, int *seq, int *nseq)
{
    int work[B_MAXR], finish[B_MAXP] = {0}, progress = 1, step = 0;

    memcpy(work, b->avail, sizeof work);
    *nseq = 0;
    printf("\nSafety algorithm trace (Work starts as Available):\n");
    while (*nseq < b->n && progress) {
        progress = 0;
        for (int i = 0; i < b->n; i++) {
            if (finish[i] || !vec_le(b->need[i], work, b->m))
                continue;
            step++;
            printf("  step %d: P%d  Need ", step, i);
            print_vec(b->need[i], b->m);
            printf(" <= Work ");
            print_vec(work, b->m);
            for (int j = 0; j < b->m; j++)
                work[j] += b->alloc[i][j];              /* P(i) finishes and releases */
            printf("  -> P%d can finish, Work = ", i);
            print_vec(work, b->m);
            printf("\n");
            finish[i] = 1;
            seq[(*nseq)++] = i;
            progress = 1;
        }
    }
    if (*nseq == b->n)
        return 1;

    printf("  no further process can finish with Work = ");
    print_vec(work, b->m);
    printf("\n  blocked processes:\n");
    for (int i = 0; i < b->n; i++) {
        if (finish[i])
            continue;
        printf("    P%d  Need ", i);
        print_vec(b->need[i], b->m);
        printf(" > Work ");
        print_vec(work, b->m);
        printf("\n");
    }
    return 0;
}

int banker_run_file(const char *path, int *is_safe, char *seq_out, size_t seq_sz)
{
    bank_t b;
    int seq[B_MAXP], nseq = 0;

    if (bank_load(path, &b) != 0)
        return -1;

    print_banner("Banker's algorithm - safety check");
    printf("State file: %s  (%d processes, %d resource types)\n\n", path, b.n, b.m);
    print_matrices(&b);

    int safe = bank_safety(&b, seq, &nseq);

    if (seq_out != NULL && seq_sz > 0)
        seq_out[0] = '\0';
    printf("\n");
    if (safe) {
        printf("RESULT: the system is in a SAFE state.\n");
        printf("Safe sequence: <");
        for (int i = 0; i < nseq; i++) {
            printf("%sP%d", i ? ", " : "", seq[i]);
            if (seq_out != NULL) {
                size_t len = strlen(seq_out);
                snprintf(seq_out + len, seq_sz - len, "%sP%d", i ? " " : "", seq[i]);
            }
        }
        printf(">\n");
    } else {
        printf("RESULT: the system is in an UNSAFE state - no safe sequence exists.\n");
        printf("Processes that can never be guaranteed to finish:");
        for (int i = 0; i < b.n; i++) {
            int in_seq = 0;
            for (int k = 0; k < nseq; k++)
                if (seq[k] == i)
                    in_seq = 1;
            if (!in_seq)
                printf(" P%d", i);
        }
        printf("\nGranting further requests could lead to deadlock.\n");
    }
    if (is_safe != NULL)
        *is_safe = safe;
    detail_set("%s state%s%s", safe ? "SAFE" : "UNSAFE", safe ? ", sequence " : "",
               (safe && seq_out) ? seq_out : "");
    return 0;
}

/* ------------------------------------------------------------------ */

static int banker_usage_error(const char *msg)
{
    fprintf(stderr, "Error: %s\n"
                    "Usage: campuscompute banker [safe|unsafe] [-f state_file]\n"
                    "  safe     check the bundled safe example   (%s)\n"
                    "  unsafe   check the bundled unsafe example (%s)\n"
                    "  -f FILE  check your own state file\n", msg, SAFE_FILE, UNSAFE_FILE);
    return 2;
}

int banker_cmd(int argc, char **argv)
{
    const char *file = NULL;
    int opt;
    char msg[96];

    opterr = 0;
    optind = 1;
    while ((opt = getopt(argc, argv, ":f:")) != -1) {
        switch (opt) {
        case 'f':
            file = optarg;
            break;
        case ':':
            snprintf(msg, sizeof msg, "option -%c needs a value", optopt);
            return banker_usage_error(msg);
        default:
            snprintf(msg, sizeof msg, "unknown option -%c", optopt);
            return banker_usage_error(msg);
        }
    }
    if (optind < argc) {
        const char *which = argv[optind];
        if (file != NULL || optind + 1 < argc)
            return banker_usage_error("give either 'safe', 'unsafe' or -f <file>");
        if (strcmp(which, "safe") == 0)
            file = SAFE_FILE;
        else if (strcmp(which, "unsafe") == 0)
            file = UNSAFE_FILE;
        else {
            snprintf(msg, sizeof msg, "unknown banker scenario '%s'", which);
            return banker_usage_error(msg);
        }
    }
    if (file == NULL)
        file = SAFE_FILE;

    int safe;
    return banker_run_file(file, &safe, NULL, 0) == 0 ? 0 : 1;
}
