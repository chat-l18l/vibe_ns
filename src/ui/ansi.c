#include "ansi.h"
#include "../core/log.h"

#include <stdio.h>
#include <assert.h>

int ansi_cursor_pos   (char *buf, size_t sz, uint16_t row, uint16_t col) {
    assert (buf != NULL);
    return snprintf (buf, sz, "\x1b[%u;%uH", row, col);
}
int ansi_clear_screen (char *buf, size_t sz) {
    assert (buf != NULL);
    return snprintf (buf, sz, "\x1b[2J");
}
int ansi_clear_line   (char *buf, size_t sz) {
    assert (buf != NULL);
    return snprintf (buf, sz, "\x1b[2K");
}
int ansi_hide_cursor  (char *buf, size_t sz) {
    assert (buf != NULL);
    return snprintf (buf, sz, "\x1b[?25l");
}
int ansi_show_cursor  (char *buf, size_t sz) {
    assert (buf != NULL);
    return snprintf (buf, sz, "\x1b[?25h");
}
int ansi_reset        (char *buf, size_t sz) {
    assert (buf != NULL);
    return snprintf (buf, sz, "\x1b[0m");
}
int ansi_sgr (char *buf, size_t sz, ansi_attr_t attr, ansi_color_t fg, ansi_color_t bg) {
    assert (buf != NULL);
    return snprintf (buf, sz, "\x1b[%d;3%d;4%dm", (int)attr, (int)fg, (int)bg);
}
