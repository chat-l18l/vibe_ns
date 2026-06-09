#include "chat.h"
#include "room.h"
#include "../../protocol/telnet/telnet_session.h"
#include "../../core/log.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
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

#define CHAT_MSG_LINES  18

typedef struct {
    telnet_session_t *s;
    chat_room_t      *room;
    int               st;
    char              compose[512];
    int               compose_len;
    char              username_buf[32];
    int               username_len;
    off_t             view_tail;
    bool              at_live;
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

/* -------------------------------------------------------------------------
 * Username screen
 * ---------------------------------------------------------------------- */

static void
render_username (chat_state_t *c)
{
    csend (c,
        "\x1b[2J\x1b[H"
        "\x1b[1;36m  OETELX CHAT\x1b[0m\r\n"
        "\r\n"
        "  Kies een naam (letters, cijfers, _ — max 20).\r\n"
        "\r\n"
        "  Naam: ",
        strlen (
        "\x1b[2J\x1b[H"
        "\x1b[1;36m  OETELX CHAT\x1b[0m\r\n"
        "\r\n"
        "  Kies een naam (letters, cijfers, _ — max 20).\r\n"
        "\r\n"
        "  Naam: "));
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

    csend (c,
        "\x1b[2J\x1b[H"
        "\x1b[1;36m  OETELX CHAT\x1b[0m\r\n"
        "\x1b[2m  ─────────────────────────\x1b[0m\r\n"
        "\r\n",
        strlen (
        "\x1b[2J\x1b[H"
        "\x1b[1;36m  OETELX CHAT\x1b[0m\r\n"
        "\x1b[2m  ─────────────────────────\x1b[0m\r\n"
        "\r\n"));

    for (int i = 0; i < count; i++)
        csendf (c, "  \x1b[33m[%d]\x1b[0m  #%s\r\n", i + 1, rooms[i]->name);

    csend (c,
        "\r\n"
        "\x1b[2m  [1-3] kies room   [Q] terug naar menu\x1b[0m\r\n",
        strlen (
        "\r\n"
        "\x1b[2m  [1-3] kies room   [Q] terug naar menu\x1b[0m\r\n"));
}

/* -------------------------------------------------------------------------
 * Room screen — full redraw
 * ---------------------------------------------------------------------- */

static void
render_room (chat_state_t *c)
{
    uint16_t r = term_rows (c);

    /* header + separator stream from row 1; cursor ends at row 3 */
    csendf (c,
        "\x1b[2J\x1b[H"
        "\x1b[1;36m  #%s\x1b[0m\r\n"
        "\x1b[2m  ───────────────────────────────────────\x1b[0m\r\n",
        c->room->name);

    char  hist[8192];
    off_t new_tail;
    /* cap to available message rows so content never reaches footer */
    int   avail = (int) r - 5;
    int   want  = (avail > 0 && avail < CHAT_MSG_LINES) ? avail : CHAT_MSG_LINES;
    int   lines = chat_room_read_history (c->room, c->view_tail,
                                          want, hist, sizeof (hist),
                                          &new_tail);
    if (c->view_tail == 0)
        c->view_tail = new_tail;

    if (lines > 0)
        csend (c, hist, strlen (hist));

    /* footer: absolute positioning — no \r\n drift, no terminal scroll */
    csendf (c,
        "\x1b[%u;1H\x1b[2m  ─────────────────────────────────────────\x1b[0m",
        (unsigned)(r - 2));
    csendf (c,
        "\x1b[%u;1H  > ",
        (unsigned)(r - 1));
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
    c->room      = room;
    c->view_tail = 0;
    c->at_live   = true;
    c->st        = CHAT_ST_IN_ROOM;

    int fds[2];
    if (pipe (fds) == 0) {
        c->s->notify_rfd = fds[0];
        c->s->notify_wfd = fds[1];
        c->s->on_notify  = chat_on_notify;
        chat_room_subscribe (room, fds[1]);
    }

    render_room (c);
}

static void
leave_room (chat_state_t *c)
{
    if (c->room) {
        chat_room_unsubscribe (c->room, c->s->notify_wfd);
        c->room = NULL;
    }
    c->s->on_notify = NULL;
    if (c->s->notify_rfd >= 0) { close (c->s->notify_rfd); c->s->notify_rfd = -1; }
    if (c->s->notify_wfd >= 0) { close (c->s->notify_wfd); c->s->notify_wfd = -1; }

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
    if (!c->at_live) return;

    /* full redraw — simpler and correct; chat traffic is low enough */
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
        strncpy (c->s->username, c->username_buf, sizeof (c->s->username) - 1);
        c->s->username[sizeof (c->s->username) - 1] = '\0';
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
        /* scroll back — read older history */
        char  hist[8192];
        off_t new_tail;
        chat_room_read_history (c->room, c->view_tail,
                                CHAT_MSG_LINES, hist, sizeof (hist),
                                &new_tail);
        if (new_tail < c->view_tail) {
            c->view_tail = new_tail;
            c->at_live   = false;
            render_room (c);
        }
        return true;
    }
    if (key == 'n') {
        c->view_tail = 0;
        c->at_live   = true;
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
        }
        c->st          = CHAT_ST_IN_ROOM;
        c->compose_len = 0;
        c->compose[0]  = '\0';
        c->view_tail   = 0;
        c->at_live     = true;
        render_room (c);
        return true;
    }
    if ((key == 127 || key == '\b') && c->compose_len > 0) {
        c->compose_len--;
        render_compose (c);
        return true;
    }
    if (c->compose_len < (int) sizeof (c->compose) - 1 &&
        (unsigned char) key >= 32 && key != 127) {
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
    chat_rooms_ensure_init ();

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
    if (c->room)
        chat_room_unsubscribe (c->room, c->s->notify_wfd);
    c->s->on_notify = NULL;
    if (c->s->notify_rfd >= 0) { close (c->s->notify_rfd); c->s->notify_rfd = -1; }
    if (c->s->notify_wfd >= 0) { close (c->s->notify_wfd); c->s->notify_wfd = -1; }
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
