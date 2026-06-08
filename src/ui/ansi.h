#ifndef ANSI_H
#define ANSI_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    ANSI_RESET   = 0,
    ANSI_BOLD    = 1,
    ANSI_DIM     = 2,
    ANSI_ITALIC  = 3,
    ANSI_BLINK   = 5,
    ANSI_REVERSE = 7
} ansi_attr_t;

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

/* All functions return bytes written, or -1 if buffer too small. */
int ansi_cursor_pos(char *buf, size_t sz, uint16_t row, uint16_t col);
int ansi_clear_screen(char *buf, size_t sz);
int ansi_clear_line(char *buf, size_t sz);
int ansi_hide_cursor(char *buf, size_t sz);
int ansi_show_cursor(char *buf, size_t sz);
int ansi_reset(char *buf, size_t sz);
int ansi_sgr(char *buf, size_t sz, ansi_attr_t attr, ansi_color_t fg, ansi_color_t bg);

#endif /* ANSI_H */
