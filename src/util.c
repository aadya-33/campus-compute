/*
 * util.c - small helpers shared by every CampusCompute module.
 */
#include "common.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int g_fd_baseline = -1;
int g_muted_fds = 0;

static char g_detail[256] = "";

int parse_long(const char *s, long lo, long hi, long *out)
{
    char *end = NULL;
    long v;

    if (s == NULL || *s == '\0')
        return -1;
    errno = 0;
    v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0')
        return -1;
    if (v < lo || v > hi)
        return -1;
    *out = v;
    return 0;
}

void detail_set(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(g_detail, sizeof g_detail, fmt, ap);
    va_end(ap);
}

const char *detail_get(void)
{
    return g_detail;
}

void print_banner(const char *title)
{
    printf("\n=== %s ===\n", title);
}

int ensure_parent_dir(const char *path)
{
    char dir[512];
    const char *slash = strrchr(path, '/');
    size_t len;

    if (slash == NULL)
        return 0;                       /* file lives in the current directory */
    len = (size_t)(slash - path);
    if (len == 0 || len >= sizeof dir)
        return 0;
    memcpy(dir, path, len);
    dir[len] = '\0';
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "Error: cannot create directory '%s': %s\n",
                dir, strerror(errno));
        return -1;
    }
    return 0;
}

int mute_fd(int fd)
{
    int saved, devnull;

    fflush(NULL);
    saved = dup(fd);
    devnull = open("/dev/null", O_WRONLY);
    if (saved >= 0 && devnull >= 0)
        dup2(devnull, fd);
    if (devnull >= 0)
        close(devnull);
    if (saved >= 0)
        g_muted_fds++;
    return saved;
}

void unmute_fd(int fd, int saved)
{
    fflush(NULL);
    if (saved >= 0) {
        dup2(saved, fd);
        close(saved);
        g_muted_fds--;
    }
}

/* Count the entries of a /proc directory, optionally only numeric names. */
static int count_proc_entries(const char *dirpath, int numeric_only)
{
    DIR *d = opendir(dirpath);
    struct dirent *e;
    int n = 0;

    if (d == NULL)
        return -1;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.')
            continue;
        if (numeric_only && !isdigit((unsigned char)e->d_name[0]))
            continue;
        n++;
    }
    closedir(d);
    return n;
}

int count_open_fds(void)
{
    /* opendir() itself holds one descriptor while we count; remove it. */
    int n = count_proc_entries("/proc/self/fd", 0);
    return n < 0 ? -1 : n - 1;
}

int count_threads(void)
{
    return count_proc_entries("/proc/self/task", 1);
}
