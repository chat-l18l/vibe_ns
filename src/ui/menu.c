/**
 * @file menu.c
 * @brief Generic menu renderer and key lookup.
 */

#include "menu.h"
#include "../core/log.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

int
menu_render (const menu_t *menu, char *buf, size_t bufsz)
{
    assert (menu != NULL);
    assert (buf  != NULL);

    if (bufsz == 0)
        return -1;

    char   tmp[64];
    size_t pos = 0;
    int    n;

#define APPEND(s, bytes) \
    do { size_t _n = (size_t)(bytes); \
         if (pos + _n >= bufsz) return -1; \
         memcpy (buf + pos, (s), _n); pos += _n; } while (0)
#define APPEND_STR(s) \
    do { size_t _n = strlen (s); \
         if (pos + _n >= bufsz) return -1; \
         memcpy (buf + pos, (s), _n); pos += _n; } while (0)

    n = ansi_clear_screen (tmp, sizeof (tmp)); if (n > 0) APPEND (tmp, n);
    n = ansi_cursor_pos   (tmp, sizeof (tmp), 1, 1); if (n > 0) APPEND (tmp, n);

    n = ansi_sgr (tmp, sizeof (tmp), ANSI_BOLD, menu->title_color, ANSI_DEFAULT);
    if (n > 0) APPEND (tmp, n);
    if (menu->title) APPEND_STR (menu->title);
    n = ansi_reset (tmp, sizeof (tmp)); if (n > 0) APPEND (tmp, n);
    APPEND_STR ("\r\n");

    if (menu->subtitle) {
        n = ansi_sgr (tmp, sizeof (tmp), ANSI_DIM, ANSI_WHITE, ANSI_DEFAULT);
        if (n > 0) APPEND (tmp, n);
        APPEND_STR (menu->subtitle);
        n = ansi_reset (tmp, sizeof (tmp)); if (n > 0) APPEND (tmp, n);
        APPEND_STR ("\r\n");
    }
    APPEND_STR ("\r\n");

    for (uint8_t i = 0; i < menu->item_count; i++) {
        const menu_item_t *item = &menu->items[i];

        n = ansi_sgr (tmp, sizeof (tmp), ANSI_BOLD, menu->key_color, ANSI_DEFAULT);
        if (n > 0) APPEND (tmp, n);
        n = snprintf (tmp, sizeof (tmp), "  [%c] ", item->key);
        if (n > 0) APPEND (tmp, n);

        n = ansi_sgr (tmp, sizeof (tmp), ANSI_RESET, menu->item_color, ANSI_DEFAULT);
        if (n > 0) APPEND (tmp, n);
        APPEND_STR (item->label);
        n = ansi_reset (tmp, sizeof (tmp)); if (n > 0) APPEND (tmp, n);
        APPEND_STR ("\r\n");
    }

    APPEND_STR ("\r\n");
    n = ansi_sgr (tmp, sizeof (tmp), ANSI_DIM, ANSI_WHITE, ANSI_DEFAULT);
    if (n > 0) APPEND (tmp, n);
    APPEND_STR ("Press a key to select: ");
    n = ansi_reset (tmp, sizeof (tmp)); if (n > 0) APPEND (tmp, n);

    buf[pos] = '\0';
    return (int) pos;

#undef APPEND
#undef APPEND_STR
}

int
menu_handle_key (const menu_t *menu, char key)
{
    assert (menu != NULL);
    for (uint8_t i = 0; i < menu->item_count; i++) {
        if (menu->items[i].key == key)
            return (int) i;
    }
    return -1;
}
