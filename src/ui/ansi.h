#ifndef ANSI_H
#define ANSI_H

/**
 * @file ansi.h
 * @brief ANSI/VT100 escape sequence builders.
 *
 * Pure formatting helpers: each writes one escape sequence into a
 * caller buffer. No I/O, no state — composable into larger renders.
 */

#include <stddef.h>
#include <stdint.h>

/** SGR text attributes. Values match the ANSI codes. */
typedef enum {
    ANSI_RESET   = 0,
    ANSI_BOLD    = 1,
    ANSI_DIM     = 2,
    ANSI_ITALIC  = 3,
    ANSI_BLINK   = 5,
    ANSI_REVERSE = 7
} ansi_attr_t;

/** Standard 8 colors. Values match the ANSI codes (30+fg / 40+bg). */
typedef enum {
    ANSI_BLACK   = 0,
    ANSI_RED     = 1,
    ANSI_GREEN   = 2,
    ANSI_YELLOW  = 3,
    ANSI_BLUE    = 4,
    ANSI_MAGENTA = 5,
    ANSI_CYAN    = 6,
    ANSI_WHITE   = 7,
    ANSI_DEFAULT = 9
} ansi_color_t;

/** @name Escape sequence builders
 *  All write into @p buf and return bytes written, or -1 if it doesn't fit.
 */
/**@{*/
int ansi_cursor_pos(char *buf, size_t sz, uint16_t row, uint16_t col); /**< CUP — move cursor (1-based). */
int ansi_clear_screen(char *buf, size_t sz);  /**< ED 2 — erase display. */
int ansi_clear_line(char *buf, size_t sz);    /**< EL 2 — erase line. */
int ansi_hide_cursor(char *buf, size_t sz);   /**< DECTCEM off. */
int ansi_show_cursor(char *buf, size_t sz);   /**< DECTCEM on. */
int ansi_reset(char *buf, size_t sz);         /**< SGR 0 — reset attributes. */
/** SGR — set attribute + foreground + background in one sequence. */
int ansi_sgr(char *buf, size_t sz, ansi_attr_t attr, ansi_color_t fg, ansi_color_t bg);
/**@}*/

#endif /* ANSI_H */
