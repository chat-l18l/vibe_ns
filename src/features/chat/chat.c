/**
 * @file chat.c
 * @brief Chat UI flow: username → lobby → room view → compose.
 *
 * Screen layout uses absolute cursor positioning for the footer rows
 * (separator, prompt, key legend) so message-count drift can never
 * scroll the terminal. Live updates arrive via the session notify pipe
 * and trigger a full room redraw — simple and always correct at chat
 * message rates.
 *
 * Paging uses message ids as the cursor (see room.h / chat_db): the live
 * view shows the newest page; 'p' walks older pages; 'n' returns to live.
 */

#include "chat.h"
#include "room.h"
#include "../../protocol/telnet/telnet_session.h"
#include "../../core/log.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <assert.h>

/* -------------------------------------------------------------------------
 * Internal states
 * ---------------------------------------------------------------------- */

enum {
    CHAT_ST_USERNAME = 0,
    CHAT_ST_LOBBY,
    CHAT_ST_IN_ROOM,
    CHAT_ST_COMPOSE,
};

#define CHAT_MSG_LINES  18   /**< Max message rows rendered per screen. */
#define CHAT_MAX_WHO    16   /**< Max active users shown in the header. */

typedef struct {
    telnet_session_t *s;
    chat_room_t      *room;
    int               st;
    char              compose[512];
    int               compose_len;
    char              username_buf[32];
    int               username_len;
    long              oldest_shown;  /* smallest message id on screen */
    bool              at_live;       /* true = following the newest page */
} chat_state_t;

/* -------------------------------------------------------------------------
 * Forward declarations
 * ---------------------------------------------------------------------- */

static void render_lobby    (chat_state_t *c);
static void render_room     (chat_state_t *c);
static void render_compose  (chat_state_t *c);
static void render_username (chat_state_t *c);
static void enter_room      (chat_state_t *c, chat_room_t *room);
static void leave_room      (chat_state_t *c);
static void chat_on_notify  (telnet_session_t *s);

/* -------------------------------------------------------------------------
 * Send helpers
 * ---------------------------------------------------------------------- */

static void
csend (chat_state_t *c, const char *buf, size_t len)
{
    telnet_session_send (c->s, buf, len);
}

static void
csendf (chat_state_t *c, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start (ap, fmt);
    int n = vsnprintf (buf, sizeof (buf), fmt, ap);
    va_end (ap);
    if (n > 0)
        telnet_session_send (c->s, buf, (size_t) n);
}

static uint16_t
term_rows (chat_state_t *c)
{
    return c->s->term_rows > 6 ? c->s->term_rows : 24;
}

/* Format one message as a telnet display line: "[HH:MM] user: body\r\n". */
static int
format_msg (const chat_msg_t *m, char *out, size_t outsz)
{
    struct tm tm;
    time_t    t = (time_t) m->ts;
    gmtime_r (&t, &tm);
    char hhmm[6];
    strftime (hhmm, sizeof (hhmm), "%H:%M", &tm);
    return snprintf (out, outsz,
                     "\x1b[2m[%s]\x1b[0m \x1b[1m%s\x1b[0m: %s\r\n",
                     hhmm, m->user, m->body);
}

/* -------------------------------------------------------------------------
 * Username screen
 * ---------------------------------------------------------------------- */

static void
render_username (chat_state_t *c)
{
    static const char screen[] =
        "\x1b[2J\x1b[H"
        "\x1b[1;36m  OETELX CHAT\x1b[0m\r\n"
        "\r\n"
        "  Kies een naam (letters, cijfers, _ — max 20).\r\n"
        "\r\n"
        "  Naam: ";
    csend (c, screen, sizeof (screen) - 1);
    if (c->username_len > 0)
        csend (c, c->username_buf, (size_t) c->username_len);
}

/* -------------------------------------------------------------------------
 * Lobby screen
 * ---------------------------------------------------------------------- */

static void
render_lobby (chat_state_t *c)
{
    int           count;
    chat_room_t **rooms = chat_rooms_list (&count);

    static const char head[] =
        "\x1b[2J\x1b[H"
        "\x1b[1;36m  OETELX CHAT\x1b[0m\r\n"
        "\x1b[2m  ─────────────────────────\x1b[0m\r\n"
        "\r\n";
    csend (c, head, sizeof (head) - 1);

    char who[CHAT_MAX_WHO][CHAT_USER_MAX];
    for (int i = 0; i < count; i++) {
        int un = chat_room_users (rooms[i], who, CHAT_MAX_WHO);
        csendf (c, "  \x1b[33m[%d]\x1b[0m  #%-10s \x1b[2m%d online\x1b[0m\r\n",
                i + 1, rooms[i]->name, un);
    }

    static const char foot[] =
        "\r\n"
        "\x1b[2m  [1-3] kies room   [Q] terug naar menu\x1b[0m\r\n";
    csend (c, foot, sizeof (foot) - 1);
}

/* -------------------------------------------------------------------------
 * Room screen — full redraw
 * ---------------------------------------------------------------------- */

static void
render_room (chat_state_t *c)
{
    uint16_t r = term_rows (c);

    /* how many message rows fit (leave room for 2 header + 3 footer) */
    int avail = (int) r - 5;
    int want  = (avail > 0 && avail < CHAT_MSG_LINES) ? avail : CHAT_MSG_LINES;

    chat_msg_t msgs[CHAT_MSG_LINES];
    long anchor = c->at_live ? 0 : c->oldest_shown;  /* 0 = newest page */
    int  n = chat_room_before (c->room, anchor, want, msgs, CHAT_MSG_LINES);
    if (n > 0)
        c->oldest_shown = msgs[0].id;

    /* active users for the header line */
    char who[CHAT_MAX_WHO][CHAT_USER_MAX];
    int  un = chat_room_users (c->room, who, CHAT_MAX_WHO);
    char wholist[256];
    size_t wl = 0;
    wholist[0] = '\0';
    for (int i = 0; i < un; i++) {
        int w = snprintf (wholist + wl, sizeof (wholist) - wl,
                          "%s%s", i ? ", " : "", who[i]);
        if (w < 0 || (size_t) w >= sizeof (wholist) - wl) break;
        wl += (size_t) w;
    }

    /* header (row 1) + separator (row 2) */
    csendf (c,
        "\x1b[2J\x1b[H"
        "\x1b[1;36m  #%s\x1b[0m  \x1b[2m(%d online: %s)\x1b[0m\r\n"
        "\x1b[2m  ───────────────────────────────────────\x1b[0m\r\n",
        c->room->name, un, wholist);

    /* message rows (stream from row 3) */
    char line[640];
    for (int i = 0; i < n; i++) {
        int w = format_msg (&msgs[i], line, sizeof (line));
        if (w > 0) csend (c, line, (size_t) w);
    }

    /* footer: absolute positioning — no \r\n drift, no terminal scroll */
    csendf (c,
        "\x1b[%u;1H\x1b[2m  ─────────────────────────────────────────\x1b[0m",
        (unsigned)(r - 2));
    csendf (c, "\x1b[%u;1H  > ", (unsigned)(r - 1));
    csendf (c,
        "\x1b[%u;1H\x1b[2m  [Enter]schrijf  [p]ouder  [n]nieuwer  [q]verlaat  [Q]quit\x1b[0m",
        (unsigned) r);
}

/* -------------------------------------------------------------------------
 * Compose line redraw
 * ---------------------------------------------------------------------- */

static void
render_compose (chat_state_t *c)
{
    uint16_t r = term_rows (c);
    csendf (c, "\x1b[%u;1H\x1b[2K  \x1b[0m> %.*s",
            (unsigned) (r - 1), c->compose_len, c->compose);
}

/* -------------------------------------------------------------------------
 * Enter / leave room
 * ---------------------------------------------------------------------- */

static void
enter_room (chat_state_t *c, chat_room_t *room)
{
    c->room         = room;
    c->oldest_shown = 0;
    c->at_live      = true;
    c->st           = CHAT_ST_IN_ROOM;

    chat_room_presence_set (room, c->s->username);

    /* The session owns its notify pipe; we only lend the write end to
     * the room and hook on_notify for the duration of the visit. */
    c->s->on_notify = chat_on_notify;
    if (c->s->notify_wfd >= 0)
        chat_room_subscribe (room, c->s->notify_wfd);

    render_room (c);
}

static void
leave_room (chat_state_t *c)
{
    if (c->room) {
        chat_room_unsubscribe (c->room, c->s->notify_wfd);
        chat_room_presence_clear (c->room, c->s->username);
        c->room = NULL;
    }
    c->s->on_notify = NULL;

    c->st = CHAT_ST_LOBBY;
    render_lobby (c);
}

/* -------------------------------------------------------------------------
 * on_notify — woken by pipe when another user posts to the room
 * ---------------------------------------------------------------------- */

static void
chat_on_notify (telnet_session_t *s)
{
    chat_state_t *c = (chat_state_t *) s->game_state;
    if (!c || c->st == CHAT_ST_LOBBY || c->st == CHAT_ST_USERNAME) return;
    if (!c->at_live) return;   /* user is reading history — don't yank them */

    render_room (c);
    if (c->st == CHAT_ST_COMPOSE)
        render_compose (c);
}

/* -------------------------------------------------------------------------
 * Key handlers per state
 * ---------------------------------------------------------------------- */

static bool
handle_username (chat_state_t *c, char key)
{
    if (key == '\r' || key == '\n') {
        if (c->username_len == 0) return true;
        c->username_buf[c->username_len] = '\0';
        snprintf (c->s->username, sizeof (c->s->username), "%s", c->username_buf);
        c->st = CHAT_ST_LOBBY;
        render_lobby (c);
        return true;
    }
    if ((key == 127 || key == '\b') && c->username_len > 0) {
        c->username_len--;
        csend (c, "\b \b", 3);
        return true;
    }
    if (c->username_len < 20 &&
        ((key >= 'a' && key <= 'z') || (key >= 'A' && key <= 'Z') ||
         (key >= '0' && key <= '9') || key == '_')) {
        c->username_buf[c->username_len++] = key;
        telnet_session_send (c->s, &key, 1);
    }
    return true;
}

static bool
handle_lobby (chat_state_t *c, char key)
{
    if (key == 'Q' || key == 'q') return false;

    int count;
    chat_room_t **rooms = chat_rooms_list (&count);

    if (key >= '1' && key < '1' + count) {
        enter_room (c, rooms[key - '1']);
        return true;
    }
    return true;
}

static bool
handle_in_room (chat_state_t *c, char key)
{
    if (key == 'Q') { leave_room (c); return false; }
    if (key == 'q') { leave_room (c); return true;  }

    if (key == '\r' || key == 'i') {
        c->st          = CHAT_ST_COMPOSE;
        c->compose_len = 0;
        c->compose[0]  = '\0';
        render_compose (c);
        return true;
    }
    if (key == 'p') {
        /* page older: messages before the oldest currently shown */
        chat_msg_t msgs[CHAT_MSG_LINES];
        int n = chat_room_before (c->room, c->oldest_shown, CHAT_MSG_LINES,
                                  msgs, CHAT_MSG_LINES);
        if (n > 0) {
            c->at_live = false;
            render_room (c);
        }
        return true;
    }
    if (key == 'n') {
        c->at_live = true;
        render_room (c);
        return true;
    }
    return true;
}

static bool
handle_compose (chat_state_t *c, char key)
{
    if (key == 27 || key == 3) {
        c->st = CHAT_ST_IN_ROOM;
        render_room (c);
        return true;
    }
    if (key == '\r' || key == '\n') {
        if (c->compose_len > 0) {
            c->compose[c->compose_len] = '\0';
            chat_room_post (c->room, c->s->username, c->compose);
            chat_room_presence_set (c->room, c->s->username);
        }
        c->st          = CHAT_ST_IN_ROOM;
        c->compose_len = 0;
        c->compose[0]  = '\0';
        c->at_live     = true;
        render_room (c);
        return true;
    }
    if ((key == 127 || key == '\b') && c->compose_len > 0) {
        c->compose_len--;
        render_compose (c);
        return true;
    }
    /* Printable ASCII only. Bytes 128–159 (C1, incl. 0x9B = single-byte
     * CSI) would be replayed on every subscriber's terminal — a remote
     * escape-sequence injection. Costs us accented chars; safe trade. */
    if (c->compose_len < (int) sizeof (c->compose) - 1 &&
        (unsigned char) key >= 32 && (unsigned char) key <= 126) {
        c->compose[c->compose_len++] = key;
        render_compose (c);
    }
    return true;
}

/* -------------------------------------------------------------------------
 * game_ops_t
 * ---------------------------------------------------------------------- */

static void *
chat_create (telnet_session_t *s)
{
    chat_service_init ();

    chat_state_t *c = calloc (1, sizeof (*c));
    if (!c) return NULL;
    c->s = s;

    if (s->username[0] != '\0') {
        c->st = CHAT_ST_LOBBY;
        render_lobby (c);
    } else {
        c->st = CHAT_ST_USERNAME;
        render_username (c);
    }
    return c;
}

static bool
chat_handle_key (void *state, char key)
{
    chat_state_t *c = (chat_state_t *) state;
    assert (c != NULL);
    switch (c->st) {
    case CHAT_ST_USERNAME: return handle_username (c, key);
    case CHAT_ST_LOBBY:    return handle_lobby    (c, key);
    case CHAT_ST_IN_ROOM:  return handle_in_room  (c, key);
    case CHAT_ST_COMPOSE:  return handle_compose  (c, key);
    default:               return false;
    }
}

static void
chat_destroy (void *state)
{
    chat_state_t *c = (chat_state_t *) state;
    if (!c) return;
    if (c->room) {
        chat_room_unsubscribe (c->room, c->s->notify_wfd);
        chat_room_presence_clear (c->room, c->s->username);
    }
    c->s->on_notify = NULL;   /* pipe stays open — owned by the session */
    free (c);
}

const game_ops_t chat_feature = {
    .name        = "Chat rooms",
    .description = "Chat in real-time met andere gebruikers",
    .menu_key    = 'g',
    .create      = chat_create,
    .handle_key  = chat_handle_key,
    .destroy     = chat_destroy,
};
