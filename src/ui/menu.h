#ifndef MENU_H
#define MENU_H

/**
 * @file menu.h
 * @brief Generic single-key menu: data model, renderer and key lookup.
 *
 * A menu is a flat list of (key, label, user_data) items. The renderer
 * draws a simple vertical list (the BBS main screen uses its own
 * two-column renderer in bbs.h but shares this data model). The
 * @c user_data pointer typically carries a ::game_ops_t.
 */

#include <stdint.h>
#include "ansi.h"

#define MENU_MAX_ITEMS   16  /**< Max items per menu. */
#define MENU_LABEL_MAX   48  /**< Max label length incl. NUL. */

/** One selectable entry. */
typedef struct {
    char  label[MENU_LABEL_MAX];  /**< Display text. */
    char  key;                    /**< Selection key (case-sensitive). */
    void *user_data;              /**< Payload, e.g. game_ops_t*. */
} menu_item_t;

/** Menu definition + styling. */
typedef struct {
    const char   *title;        /**< Optional; NULL to skip. */
    const char   *subtitle;     /**< Optional; NULL to skip. */
    menu_item_t   items[MENU_MAX_ITEMS];
    uint8_t       item_count;
    ansi_color_t  title_color;
    ansi_color_t  item_color;
    ansi_color_t  key_color;
} menu_t;

/**
 * @brief Render the full menu (clear screen + list) into @p buf.
 * @return Bytes written, or -1 if @p buf is too small.
 */
int menu_render(const menu_t *menu, char *buf, size_t bufsz);

/**
 * @brief Look up a keypress in the item list.
 * @return Item index (0-based) on match, -1 otherwise.
 */
int menu_handle_key(const menu_t *menu, char key);

#endif /* MENU_H */
