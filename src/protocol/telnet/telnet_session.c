/**
 * @file telnet_session.c
 * @brief Telnet session: event loop, FSM actions/table and menu wiring.
 */

#include "telnet_session.h"
#include "../../core/log.h"
#include "../../protocol/protocol.h"
#include "../../ui/bbs.h"
#include "../../features/stubs.h"
#include "../../features/chat/chat.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <assert.h>

/* -------------------------------------------------------------------------
 * Online user counter — module-level, shared across all sessions
 * ---------------------------------------------------------------------- */

static atomic_uint s_online_count;

/* -------------------------------------------------------------------------
 * Session registry — lets shutdown_all reach and wake every live session.
 * Sessions register at loop start and unregister before closing their
 * notify pipe, so shutdown_all never writes to a closed fd.
 * ---------------------------------------------------------------------- */

static pthread_mutex_t   s_sessions_lock = PTHREAD_MUTEX_INITIALIZER;
static telnet_session_t *s_session_head;
static bool              s_shutting_down;   /* guarded by s_sessions_lock */

static void
session_register (telnet_session_t *s)
{
    pthread_mutex_lock (&s_sessions_lock);
    s->reg_next    = s_session_head;
    s_session_head = s;
    if (s_shutting_down)
        s->shutdown_requested = true;
    pthread_mutex_unlock (&s_sessions_lock);
}

static void
session_unregister (telnet_session_t *s)
{
    pthread_mutex_lock (&s_sessions_lock);
    for (telnet_session_t **pp = &s_session_head; *pp; pp = &(*pp)->reg_next) {
        if (*pp == s) {
            *pp = s->reg_next;
            break;
        }
    }
    pthread_mutex_unlock (&s_sessions_lock);
}

static void
telnet_shutdown_all (void)
{
    pthread_mutex_lock (&s_sessions_lock);
    s_shutting_down = true;
    for (telnet_session_t *s = s_session_head; s; s = s->reg_next) {
        s->shutdown_requested = true;
        if (s->notify_wfd >= 0) {
            uint8_t one = 1;
            (void) !write (s->notify_wfd, &one, 1);   /* wake poll() */
        }
    }
    pthread_mutex_unlock (&s_sessions_lock);
}

/* -------------------------------------------------------------------------
 * Send helpers
 * ---------------------------------------------------------------------- */

static int
session_flush (telnet_session_t *s)
{
    assert (s != NULL);
    size_t written = 0;
    while (written < s->send_len) {
        ssize_t n = send (s->sockfd, s->send_buf + written,
                          s->send_len - written, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        written += (size_t) n;
    }
    s->send_len = 0;
    return 0;
}

static int
session_append (telnet_session_t *s, const void *data, size_t len)
{
    assert (s != NULL);
    if (!data || s->send_len + len > TELNET_SEND_BUFSZ)
        return -1;
    memcpy (s->send_buf + s->send_len, data, len);
    s->send_len += len;
    return 0;
}

void
telnet_session_send (telnet_session_t *session, const char *data, size_t len)
{
    if (!session || !data || len == 0)
        return;
    session_append (session, data, len);
    session_flush  (session);
}

/* -------------------------------------------------------------------------
 * IAC callbacks
 * ---------------------------------------------------------------------- */

static void
on_data (void *ctx, const uint8_t *buf, size_t len)
{
    telnet_session_t *s = (telnet_session_t *) ctx;
    assert (s != NULL);

    for (size_t i = 0; i < len; i++) {
        char key = (char) buf[i];
        if (key == '\0') continue;
        if (fsm_is_terminal (&s->fsm)) return;

        fsm_dispatch (&s->fsm, TEV_KEY_INPUT, s, &key);

        /* Consume game-over flag raised by act_game_key — must be dispatched
         * here (outside the action) so the outer fsm_dispatch doesn't
         * overwrite the state transition. */
        if (s->game_over_pending && !fsm_is_terminal (&s->fsm)) {
            s->game_over_pending = false;
            fsm_dispatch (&s->fsm, TEV_GAME_OVER, s, NULL);
        }

        if (s->quit_pending && !fsm_is_terminal (&s->fsm)) {
            s->quit_pending = false;
            fsm_dispatch (&s->fsm, TEV_DISCONNECT, s, NULL);
        }

        if (s->pending_selection >= 0 && !fsm_is_terminal (&s->fsm)) {
            int sel = s->pending_selection;
            s->pending_selection = -1;   /* consume — once per selection */
            fsm_dispatch (&s->fsm, TEV_MENU_SELECT, s, &sel);
        }
    }
}

static void
on_option (void *ctx, const iac_option_event_t *ev)
{
    telnet_session_t *s = (telnet_session_t *) ctx;
    assert (s != NULL);
    assert (ev != NULL);

    if (ev->cmd == TELNET_WILL && ev->opt == TELOPT_NAWS) s->naws_ok = true;
    if (ev->cmd == TELNET_DO   && ev->opt == TELOPT_ECHO) s->echo_ok = true;
    if (ev->cmd == TELNET_DO   && ev->opt == TELOPT_SGA)  s->sga_ok  = true;
    if (ev->cmd == TELNET_DONT && ev->opt == TELOPT_ECHO) s->echo_ok = false;

    if (s->nego_pending > 0) {
        s->nego_pending--;
        if (s->nego_pending == 0 && !fsm_is_terminal (&s->fsm))
            fsm_dispatch (&s->fsm, TEV_NEGO_DONE, s, NULL);
    }
}

static void
on_subneg (void *ctx, const iac_sb_event_t *ev)
{
    telnet_session_t *s = (telnet_session_t *) ctx;
    assert (s != NULL);
    assert (ev != NULL);

    if (ev->opt == TELOPT_NAWS && ev->len >= 4) {
        s->term_cols = (uint16_t) ((ev->data[0] << 8) | ev->data[1]);
        s->term_rows = (uint16_t) ((ev->data[2] << 8) | ev->data[3]);
        LOG_DBG ("terminal size: %ux%u", s->term_cols, s->term_rows);
        fsm_dispatch (&s->fsm, TEV_NAWS, s, NULL);
    }
}

static const iac_callbacks_t iac_cbs = {
    .on_data   = on_data,
    .on_option = on_option,
    .on_subneg = on_subneg,
};

/* -------------------------------------------------------------------------
 * FSM actions
 * ---------------------------------------------------------------------- */

static fsm_action_result_t
act_send_negotiations (void *ctx, fsm_event_t ev, const void *data)
{
    (void) ev; (void) data;
    telnet_session_t *s = (telnet_session_t *) ctx;
    assert (s != NULL);

    uint8_t nego[16];
    size_t  pos = 0;
    int n;

    n = iac_write_will (nego + pos, sizeof (nego) - pos, TELOPT_ECHO);
    if (n > 0) pos += (size_t) n;
    n = iac_write_will (nego + pos, sizeof (nego) - pos, TELOPT_SGA);
    if (n > 0) pos += (size_t) n;
    n = iac_write_do   (nego + pos, sizeof (nego) - pos, TELOPT_NAWS);
    if (n > 0) pos += (size_t) n;

    s->nego_pending = 3;
    session_append (s, nego, pos);
    session_flush  (s);
    return FSM_ACTION_OK;
}

static fsm_action_result_t
act_show_main_menu (void *ctx, fsm_event_t ev, const void *data)
{
    (void) ev; (void) data;
    telnet_session_t *s = (telnet_session_t *) ctx;
    assert (s != NULL);

    s->pending_selection  = -1;
    s->game_over_pending  = false;

    bbs_screen_t screen = {
        .hostname   = "oetelx.nl",
        .online     = s->online_count ? (unsigned) atomic_load (s->online_count) : 0,
        .items      = s->main_menu.items,
        .item_count = s->main_menu.item_count,
        .left_count = 5,   /* games: c b a t r */
    };

    char buf[TELNET_SEND_BUFSZ];
    int n = bbs_render (&screen, buf, sizeof (buf));
    if (n > 0)
        telnet_session_send (s, buf, (size_t) n);
    return FSM_ACTION_OK;
}

static fsm_action_result_t
act_handle_menu_key (void *ctx, fsm_event_t ev, const void *data)
{
    (void) ev;
    telnet_session_t *s = (telnet_session_t *) ctx;
    assert (s != NULL);
    assert (data != NULL);

    char key = *(const char *) data;
    if (key == 'q' || key == 'Q' || key == 3) {
        s->quit_pending = true;
        return FSM_ACTION_OK;
    }

    int sel = menu_handle_key (&s->main_menu, key);
    if (sel >= 0)
        s->pending_selection = sel;

    return FSM_ACTION_OK;
}

static fsm_action_result_t
act_start_game (void *ctx, fsm_event_t ev, const void *data)
{
    (void) ev;
    telnet_session_t *s = (telnet_session_t *) ctx;
    assert (s != NULL);
    assert (data != NULL);

    int sel = *(const int *) data;
    assert (sel >= 0 && sel < (int) s->main_menu.item_count);

    const game_ops_t *game = (const game_ops_t *) s->main_menu.items[sel].user_data;
    if (!game) {
        /* Quit item selected via MENU_SELECT — treat as disconnect */
        return FSM_ACTION_ERROR;
    }

    s->active_game = game;
    s->game_state  = game->create (s);
    if (!s->game_state) {
        s->active_game = NULL;
        return FSM_ACTION_ERROR;
    }
    return FSM_ACTION_OK;
}

static fsm_action_result_t
act_game_key (void *ctx, fsm_event_t ev, const void *data)
{
    (void) ev;
    telnet_session_t *s = (telnet_session_t *) ctx;
    assert (s != NULL);

    /* Guard: active_game can be NULL if a prior nested dispatch left the FSM
     * in an inconsistent state. Raise game_over_pending so on_data can
     * cleanly transition back to the main menu. */
    if (!s->active_game) {
        LOG_ERR ("act_game_key: active_game is NULL — recovering to menu");
        s->game_over_pending = true;
        return FSM_ACTION_OK;
    }

    assert (data != NULL);
    char key    = *(const char *) data;
    bool running = s->active_game->handle_key (s->game_state, key);
    if (!running) {
        s->active_game->destroy (s->game_state);
        s->game_state        = NULL;
        s->active_game       = NULL;
        s->game_over_pending = true;  /* consumed by on_data, not dispatched here */
    }
    return FSM_ACTION_OK;
}

static fsm_action_result_t
act_close (void *ctx, fsm_event_t ev, const void *data)
{
    (void) ev; (void) data;
    telnet_session_t *s = (telnet_session_t *) ctx;
    assert (s != NULL);

    const char *bye = "\r\n\x1b[1;31mTot ziens!\x1b[0m\r\n";
    telnet_session_send (s, bye, strlen (bye));
    shutdown (s->sockfd, SHUT_RDWR);
    return FSM_ACTION_OK;
}

/* -------------------------------------------------------------------------
 * FSM transition table
 * ---------------------------------------------------------------------- */

static const fsm_transition_t telnet_fsm_table[] = {
    { TSTATE_INIT,        TEV_CONNECTED,   act_send_negotiations, TSTATE_NEGOTIATING, TSTATE_CLOSING   },
    { TSTATE_NEGOTIATING, TEV_NEGO_DONE,   act_show_main_menu,    TSTATE_MAIN_MENU,   TSTATE_CLOSING   },
    { TSTATE_NEGOTIATING, TEV_KEY_INPUT,   act_show_main_menu,    TSTATE_MAIN_MENU,   TSTATE_CLOSING   },
    { TSTATE_MAIN_MENU,   TEV_KEY_INPUT,   act_handle_menu_key,   TSTATE_MAIN_MENU,   TSTATE_CLOSING   },
    { TSTATE_MAIN_MENU,   TEV_MENU_SELECT, act_start_game,        TSTATE_IN_GAME,     TSTATE_MAIN_MENU },
    { TSTATE_IN_GAME,     TEV_KEY_INPUT,   act_game_key,          TSTATE_IN_GAME,     TSTATE_CLOSING   },
    { TSTATE_IN_GAME,     TEV_GAME_OVER,   act_show_main_menu,    TSTATE_MAIN_MENU,   TSTATE_CLOSING   },
    { TSTATE_INIT,        TEV_DISCONNECT,  act_close,             TSTATE_CLOSED,      TSTATE_CLOSED    },
    { TSTATE_NEGOTIATING, TEV_DISCONNECT,  act_close,             TSTATE_CLOSED,      TSTATE_CLOSED    },
    { TSTATE_MAIN_MENU,   TEV_DISCONNECT,  act_close,             TSTATE_CLOSED,      TSTATE_CLOSED    },
    { TSTATE_IN_GAME,     TEV_DISCONNECT,  act_close,             TSTATE_CLOSED,      TSTATE_CLOSED    },
    { TSTATE_CLOSING,     TEV_DISCONNECT,  act_close,             TSTATE_CLOSED,      TSTATE_CLOSED    },
    { TSTATE_MAIN_MENU,   TEV_TIMEOUT,     act_close,             TSTATE_CLOSED,      TSTATE_CLOSED    },
    { TSTATE_IN_GAME,     TEV_TIMEOUT,     act_close,             TSTATE_CLOSED,      TSTATE_CLOSED    },
};

static const fsm_def_t telnet_fsm_def = {
    .table          = telnet_fsm_table,
    .table_len      = sizeof (telnet_fsm_table) / sizeof (telnet_fsm_table[0]),
    .initial_state  = TSTATE_INIT,
    .terminal_state = TSTATE_CLOSED,
};

/* -------------------------------------------------------------------------
 * Menu setup
 * ---------------------------------------------------------------------- */

static void
menu_add (menu_t *m, char key, const char *label, const game_ops_t *ops)
{
    assert (m->item_count < MENU_MAX_ITEMS);
    menu_item_t *it = &m->items[m->item_count++];
    it->key       = key;
    it->user_data = (void *) ops;
    strncpy (it->label, label, MENU_LABEL_MAX - 1);
    it->label[MENU_LABEL_MAX - 1] = '\0';
}

static void
setup_main_menu (telnet_session_t *s)
{
    assert (s != NULL);
    menu_t *m  = &s->main_menu;
    m->item_count = 0;

    /* Left column — games (items 0–4, left_count=5) */
    menu_add (m, 'c', "Chess",          &game_chess);
    menu_add (m, 'b', "Backgammon",     &game_backgammon);
    menu_add (m, 'a', "Text Adventure", &feature_text_adventure);
    menu_add (m, 't', "Trivia",         &feature_trivia);
    menu_add (m, 'r', "Reversi",        &feature_reversi);

    /* Right column — BBS features (items 5–9) */
    menu_add (m, 'w', "Who's Online",   &feature_whosonline);
    menu_add (m, 'p', "Profiel",        &feature_profile);
    menu_add (m, 'n', "Alias instellen",&feature_alias);
    menu_add (m, 'g', "Chat rooms",      &chat_feature);
    menu_add (m, 'm', "Message board",  &feature_messageboard);
    menu_add (m, 'h', "Help",           &feature_help);

    /* Quit — rendered in the footer legend, not as a numbered item */
    menu_add (m, 'q', "Uitloggen",      NULL);
}

/* -------------------------------------------------------------------------
 * Protocol vtable
 * ---------------------------------------------------------------------- */

static void *
telnet_create_session (int sockfd, const struct sockaddr_storage *peer)
{
    assert (sockfd >= 0);
    if (!peer) return NULL;

    telnet_session_t *s = malloc (sizeof (*s));
    if (!s) {
        LOG_ERR ("telnet_create_session: malloc failed");
        return NULL;
    }
    memset (s, 0, sizeof (*s));

    s->sockfd            = sockfd;
    s->term_cols         = 80;
    s->term_rows         = 24;
    s->pending_selection = -1;
    s->online_count      = &s_online_count;
    s->on_notify         = NULL;

    /* Self-pipe: publishers (chat) and shutdown_all write here to wake the
     * session loop. Non-blocking on both ends, so a full pipe can never
     * stall a publisher — a lost wake-up is fine, redraws read full state. */
    int fds[2];
    if (pipe2 (fds, O_NONBLOCK) == 0) {
        s->notify_rfd = fds[0];
        s->notify_wfd = fds[1];
    } else {
        LOG_WARN ("telnet_create_session: pipe2 failed: %m — no live notify");
        s->notify_rfd = -1;
        s->notify_wfd = -1;
    }

    fsm_init        (&s->fsm, &telnet_fsm_def);
    iac_parser_init (&s->iac);
    setup_main_menu (s);
    return s;
}

static void
telnet_run_session (void *session)
{
    assert (session != NULL);
    telnet_session_t *s = (telnet_session_t *) session;

    atomic_fetch_add (&s_online_count, 1u);
    session_register (s);

    fsm_dispatch (&s->fsm, TEV_CONNECTED, s, NULL);

    time_t nego_deadline = time (NULL) + TELNET_NEGO_TIMEOUT;

    /* poll(), not select(): connection fds can exceed FD_SETSIZE (1024)
     * under load, and FD_SET past that limit corrupts memory. */
    while (!fsm_is_terminal (&s->fsm) && !s->shutdown_requested) {
        struct pollfd pfds[2];
        nfds_t        npfd = 0;

        pfds[npfd].fd     = s->sockfd;
        pfds[npfd].events = POLLIN;
        npfd++;
        if (s->notify_rfd >= 0) {
            pfds[npfd].fd     = s->notify_rfd;
            pfds[npfd].events = POLLIN;
            npfd++;
        }

        time_t now  = time (NULL);
        long   wait = (long) (nego_deadline - now);
        int    timeout_ms = (s->nego_pending > 0 && wait > 0)
                          ? (int) wait * 1000
                          : TELNET_IDLE_TIMEOUT * 1000;

        int nready = poll (pfds, npfd, timeout_ms);

        if (nready < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (nready == 0) {
            if (s->nego_pending > 0) {
                s->nego_pending = 0;
                fsm_dispatch (&s->fsm, TEV_NEGO_DONE, s, NULL);
                nego_deadline = 0;
            } else {
                fsm_dispatch (&s->fsm, TEV_TIMEOUT, s, NULL);
            }
            continue;
        }

        if (npfd > 1 && (pfds[1].revents & POLLIN)) {
            /* Drain all pending wake-ups, then notify once — coalesces a
             * burst of posts into a single redraw. */
            uint8_t drain[64];
            while (read (s->notify_rfd, drain, sizeof (drain)) > 0)
                ;
            if (s->shutdown_requested)
                break;
            if (s->on_notify) s->on_notify (s);
        }

        if (!(pfds[0].revents & (POLLIN | POLLHUP | POLLERR)))
            continue;

        ssize_t n = recv (s->sockfd, s->recv_buf, sizeof (s->recv_buf), 0);
        if (n <= 0) {
            fsm_dispatch (&s->fsm, TEV_DISCONNECT, s, NULL);
            break;
        }

        iac_parser_feed (&s->iac, s->recv_buf, (size_t) n, s, &iac_cbs);
    }

    if (s->active_game && s->game_state) {
        s->active_game->destroy (s->game_state);
        s->game_state  = NULL;
        s->active_game = NULL;
    }

    /* Unregister before closing the pipe: after this returns,
     * telnet_shutdown_all can no longer see this session. */
    session_unregister (s);
    if (s->notify_rfd >= 0) close (s->notify_rfd);
    if (s->notify_wfd >= 0) close (s->notify_wfd);

    atomic_fetch_sub (&s_online_count, 1u);
    close (s->sockfd);
    free (s);
}

static void
telnet_request_shutdown (void *session)
{
    if (!session) return;
    ((telnet_session_t *) session)->shutdown_requested = true;
}

const protocol_ops_t telnet_protocol = {
    .name             = "telnet",
    .description      = "Telnet game server",
    .create_session   = telnet_create_session,
    .run_session      = telnet_run_session,
    .request_shutdown = telnet_request_shutdown,
    .shutdown_all     = telnet_shutdown_all,
};
