#include "bbs.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>

/* ANSI escape shortcuts */
#define RST   "\x1b[0m"
#define BOLD  "\x1b[1m"
#define DIM   "\x1b[2m"
#define GRN   "\x1b[1;32m"
#define YLW   "\x1b[33m"
#define CYN   "\x1b[1;36m"
#define WHT   "\x1b[1;37m"

/* Append to buffer with overflow guard. Sets *ok=0 on first overflow. */
#define APPEND(ok, pos, bufsz, ...) \
    do { \
        if (*(ok)) { \
            int _n = snprintf ((buf) + (pos), (bufsz) - (pos), __VA_ARGS__); \
            if (_n < 0 || (size_t)_n >= (bufsz) - (pos)) *(ok) = 0; \
            else (pos) += (size_t) _n; \
        } \
    } while (0)

static const char LOGO[] =
    GRN
    "  ___  _____ _____ _____ _    _  __\r\n"
    " / _ \\| ____|_   _| ____| |   \\ \\/ /\r\n"
    "| | | |  _|   | | |  _| | |    \\  / \r\n"
    "| |_| | |___  | | | |___| |___ /  \\ \r\n"
    " \\___/|_____| |_| |_____|_____/_/\\_\\\r\n"
    RST;

int
bbs_render (const bbs_screen_t *screen, char *buf, size_t bufsz)
{
    assert (screen != NULL);
    assert (buf    != NULL);
    assert (bufsz  > 0);

    size_t pos = 0;
    int    ok  = 1;

    /* Clear screen, cursor home */
    APPEND (&ok, pos, bufsz, "\x1b[2J\x1b[H");

    /* ASCII art logo */
    APPEND (&ok, pos, bufsz, "%s", LOGO);

    /* Tagline + hostname */
    APPEND (&ok, pos, bufsz,
            YLW "  B·B·S" RST DIM "  —  %s" RST "\r\n", screen->hostname);

    /* Date/time + online count */
    {
        time_t    now = time (NULL);
        struct tm tm;
        localtime_r (&now, &tm);
        char datebuf[32];
        strftime (datebuf, sizeof (datebuf), "%a %e %b %Y  %H:%M", &tm);
        APPEND (&ok, pos, bufsz,
                DIM "  %s  ·  Users online: %u" RST "\r\n",
                datebuf, screen->online);
    }

    APPEND (&ok, pos, bufsz, "\r\n");

    /* Column headers */
    APPEND (&ok, pos, bufsz,
            CYN
            "  %-29s  %s\r\n"
            RST,
            "── GAMES ─────────────────",
            "── BBS ──────────────────");

    /* Two-column item list.
     * Left column : items[0 .. left_count-1]
     * Right column: items[left_count .. item_count-2]  (item_count-1 = quit)
     */
    uint8_t right_start = screen->left_count;
    uint8_t right_end   = (screen->item_count > 0)
                        ? screen->item_count - 1   /* exclude quit */
                        : 0;
    uint8_t rows = screen->left_count;
    {
        uint8_t right_rows = (right_end > right_start) ? right_end - right_start : 0;
        if (right_rows > rows) rows = right_rows;
    }

    for (uint8_t row = 0; row < rows; row++) {
        /* Left column */
        if (row < screen->left_count) {
            const menu_item_t *it = &screen->items[row];
            APPEND (&ok, pos, bufsz,
                    YLW "  [%c]" RST " %-24s",
                    it->key, it->label);
        } else {
            APPEND (&ok, pos, bufsz, "  %s  %-24s", "   ", "");
        }

        /* Right column */
        uint8_t ri = (uint8_t)(right_start + row);
        if (ri < right_end) {
            const menu_item_t *it = &screen->items[ri];
            APPEND (&ok, pos, bufsz,
                    YLW "  [%c]" RST " %s",
                    it->key, it->label);
        }

        APPEND (&ok, pos, bufsz, "\r\n");
    }

    APPEND (&ok, pos, bufsz, "\r\n");

    /* Footer separator + legend */
    APPEND (&ok, pos, bufsz,
            DIM
            "  ──────────────────────────────────────────────────────────\r\n"
            "  Druk een letter om te selecteren"
            RST
            "  "
            YLW "[Q]" RST DIM " Uitloggen" RST
            "\r\n");

    if (!ok)
        return -1;
    return (int) pos;
}
