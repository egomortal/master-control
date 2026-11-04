#include "log.h"
#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* The log is kept under LOG_MAX bytes: past it, it becomes path.1, the older
 * ones move down to path.2 and path.3 (the oldest is dropped), and a new one
 * starts: three rotations, as the Omnibloom standard asks. */
#define LOG_MAX (1L << 20)

static FILE *g_log;
static char g_path[256];

int log_open(const char *path)
{
    if (!path) return 1;
    snprintf(g_path, sizeof g_path, "%s", path);
    g_log = fopen(path, "a");
    return g_log != NULL;
}

void log_close(void)
{
    if (g_log) fclose(g_log);
    g_log = NULL;
}

#define LOG_KEEP 3

static void rotate(void)
{
    char from[sizeof g_path + 16], to[sizeof g_path + 16];
    int i;

    if (!g_log || ftell(g_log) < LOG_MAX) return;
    fclose(g_log);
    snprintf(to, sizeof to, "%s.%d", g_path, LOG_KEEP);
    remove(to);
    for (i = LOG_KEEP - 1; i >= 1; i--) {
        snprintf(from, sizeof from, "%s.%d", g_path, i);
        snprintf(to, sizeof to, "%s.%d", g_path, i + 1);
        rename(from, to);
    }
    snprintf(to, sizeof to, "%s.1", g_path);
    rename(g_path, to);
    g_log = fopen(g_path, "a");
}

const char *log_path(void)
{
    return g_log ? g_path : NULL;
}

void log_line(const char *fmt, ...)
{
    FILE *out;
    va_list ap;

    rotate();
    out = g_log ? g_log : stderr;
    fprintf(out, "[%8ld] ", now_ms());
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);
    fputc('\n', out);
    fflush(out);
}
