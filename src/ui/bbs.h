#ifndef BBS_H
#define BBS_H

/**
 * @file bbs.h
 * @brief BBS main screen renderer (logo, stats bar, two-column menu).
 *
 * Renders a complete telnet screen: ASCII art header, date/online
 * stats, a two-column menu and a key legend. The first @c left_count
 * items appear in the left column (games), the remainder in the right
 * column (BBS features) — except the last item, which is always the
 * quit entry and is rendered in the footer legend instead.
 */

#include <stdint.h>
#include <stddef.h>
#include "menu.h"

/** Everything bbs_render() needs; borrowed pointers, not owned. */
typedef struct {
    const char        *hostname;    /**< e.g. "oetelx.nl" */
    unsigned           online;      /**< Current online user count. */
    const menu_item_t *items;       /**< From menu_t.items. */
    uint8_t            item_count;  /**< Total items including quit. */
    uint8_t            left_count;  /**< items[0..left_count-1] = left column. */
} bbs_screen_t;

/**
 * @brief Render the BBS main screen into @p buf.
 * @return Bytes written, or -1 on buffer overflow.
 */
int bbs_render (const bbs_screen_t *screen, char *buf, size_t bufsz);

#endif /* BBS_H */
