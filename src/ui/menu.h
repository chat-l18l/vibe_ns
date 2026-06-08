#ifndef MENU_H
#define MENU_H

#include <stdint.h>
#include "ansi.h"

#define MENU_MAX_ITEMS   16
#define MENU_LABEL_MAX   48

typedef struct {
    char  label[MENU_LABEL_MAX];
    char  key;
    void *user_data;
} menu_item_t;

typedef struct {
    const char   *title;
    const char   *subtitle;
    menu_item_t   items[MENU_MAX_ITEMS];
    uint8_t       item_count;
    ansi_color_t  title_color;
    ansi_color_t  item_color;
    ansi_color_t  key_color;
} menu_t;

/* Render full menu into buf. Returns bytes written, or -1 if buf too small. */
int menu_render(const menu_t *menu, char *buf, size_t bufsz);

/* Process one keypress. Returns item index (0-based) on match, -1 otherwise. */
int menu_handle_key(const menu_t *menu, char key);

#endif /* MENU_H */
