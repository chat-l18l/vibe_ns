#ifndef BBS_H
#define BBS_H

#include <stdint.h>
#include <stddef.h>
#include "menu.h"

/*
 * BBS full-screen renderer.
 *
 * Renders a complete telnet screen: ASCII art header, stats bar,
 * two-column menu, and key legend. The first left_count items appear
 * in the left column (games), the remainder in the right column (BBS
 * features), except the last item which is always the quit entry and
 * appears in the footer legend.
 */
typedef struct {
    const char        *hostname;    /* e.g. "oetelx.nl" */
    unsigned           online;      /* current online user count */
    const menu_item_t *items;       /* from menu_t.items */
    uint8_t            item_count;  /* total items including quit */
    uint8_t            left_count;  /* items[0..left_count-1] = left column */
} bbs_screen_t;

/* Render BBS main screen into buf. Returns bytes written, -1 on overflow. */
int bbs_render (const bbs_screen_t *screen, char *buf, size_t bufsz);

#endif /* BBS_H */
