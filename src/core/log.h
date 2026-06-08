#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <stdarg.h>

static inline void
log_write (const char *level, const char *fmt, ...)
{
    va_list ap;
    fprintf (stderr, "[%s] ", level);
    va_start (ap, fmt);
    vfprintf (stderr, fmt, ap);
    va_end (ap);
    fprintf (stderr, "\n");
}

#define LOG_INFO(...)  log_write ("\x1b[32mINFO \x1b[0m", __VA_ARGS__)
#define LOG_WARN(...)  log_write ("\x1b[33mWARN \x1b[0m", __VA_ARGS__)
#define LOG_ERR(...)   log_write ("\x1b[31mERROR\x1b[0m", __VA_ARGS__)
#define LOG_DBG(...)   log_write ("\x1b[36mDEBUG\x1b[0m", __VA_ARGS__)

#endif /* LOG_H */
