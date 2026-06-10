#ifndef LOG_H
#define LOG_H

/**
 * @file log.h
 * @brief Minimal colored stderr logger.
 *
 * Header-only on purpose: no init, no global state, usable from any
 * translation unit. Output goes to stderr, which systemd captures into
 * the journal (see deploy/netserver.service).
 *
 * @warning Each log call performs three separate fprintf calls, so lines
 *          from concurrent connection threads can interleave. Acceptable
 *          for the current log volume; switch to a single buffered write
 *          (or flockfile) if logs become load-bearing.
 */

#include <stdio.h>
#include <stdarg.h>

/**
 * @brief Write one log line: "[LEVEL] message\n" to stderr.
 * @param level  Pre-colored level tag (use the LOG_* macros instead).
 * @param fmt    printf-style format string.
 */
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

#define LOG_INFO(...)  log_write ("\x1b[32mINFO \x1b[0m", __VA_ARGS__)  /**< Informational. */
#define LOG_WARN(...)  log_write ("\x1b[33mWARN \x1b[0m", __VA_ARGS__)  /**< Recoverable problem. */
#define LOG_ERR(...)   log_write ("\x1b[31mERROR\x1b[0m", __VA_ARGS__)  /**< Operation failed. */
#define LOG_DBG(...)   log_write ("\x1b[36mDEBUG\x1b[0m", __VA_ARGS__)  /**< Development detail. */

#endif /* LOG_H */
