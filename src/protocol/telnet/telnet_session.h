#ifndef TELNET_SESSION_H
#define TELNET_SESSION_H

/**
 * @file telnet_session.h
 * @brief Telnet session: per-connection state, event loop and FSM.
 *
 * One ::telnet_session_t lives on each connection thread. The session
 * loop (telnet_run_session) selects on the socket plus an optional
 * notify pipe, feeds received bytes through the IAC parser, and drives
 * the session FSM with the decoded events.
 *
 * The notify pipe is the cross-thread wake-up mechanism: a feature
 * (currently chat) creates a pipe, registers the write end with a
 * publisher, and sets @c on_notify. When another thread writes a byte,
 * this session's select() wakes and @c on_notify runs *on the session
 * thread* — features never touch a session from foreign threads.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <sys/socket.h>

#include "../../core/fsm.h"
#include "../../ui/menu.h"
#include "../../games/game.h"
#include "telnet_proto.h"
#include "telnet_states.h"

#define TELNET_RECV_BUFSZ    4096  /**< recv() buffer size. */
#define TELNET_SEND_BUFSZ    8192  /**< Max bytes per send batch / screen. */
#define TELNET_IDLE_TIMEOUT  300   /**< Seconds of silence before disconnect. */
#define TELNET_NEGO_TIMEOUT  3     /**< Seconds to wait for IAC replies. */

/** Per-connection telnet session state. */
struct telnet_session {
    int                    sockfd;       /**< Client socket. */
    fsm_t                  fsm;          /**< Session lifecycle FSM. */
    iac_parser_t           iac;          /**< Telnet command decoder. */
    menu_t                 main_menu;    /**< BBS main menu definition. */
    const game_ops_t      *active_game;  /**< Running game/feature, or NULL. */
    void                  *game_state;   /**< State owned by active_game. */

    uint16_t               term_cols;    /**< From NAWS; default 80. */
    uint16_t               term_rows;    /**< From NAWS; default 24. */

    uint8_t                recv_buf[TELNET_RECV_BUFSZ];
    char                   send_buf[TELNET_SEND_BUFSZ];
    size_t                 send_len;

    bool                   echo_ok;      /**< Client accepted server echo. */
    bool                   sga_ok;       /**< Client accepted SGA (char mode). */
    bool                   naws_ok;      /**< Client will send window size. */
    uint8_t                nego_pending; /**< Option replies still expected. */

    /** @name Deferred-dispatch flags
     *  The FSM engine is not re-entrant, so actions raise these flags and
     *  on_data() dispatches the follow-up event after the outer
     *  fsm_dispatch() returns.
     */
    /**@{*/
    int                    pending_selection;   /**< Menu index, -1 = none. */
    bool                   game_over_pending;   /**< Set in act_game_key. */
    bool                   quit_pending;        /**< Set in act_handle_menu_key. */
    /**@}*/

    volatile bool          shutdown_requested;  /**< Async stop flag. */

    const atomic_uint     *online_count;        /**< Shared online counter. */

    /** @name Feature extensions */
    /**@{*/
    char                   username[32];  /**< Chosen in chat; persists for session. */
    int                    notify_rfd;    /**< Notify pipe read end; -1 unused. */
    int                    notify_wfd;    /**< Notify pipe write end (publisher side). */
    void                 (*on_notify)(struct telnet_session *s); /**< Wake handler. */
    /**@}*/
};

typedef struct telnet_session telnet_session_t;

/**
 * @brief Send bytes to the client (blocking, handles partial writes).
 *
 * The single output path for game/feature modules. Writes larger than
 * ::TELNET_SEND_BUFSZ are silently dropped — render one screen per call.
 */
void telnet_session_send(telnet_session_t *session, const char *data, size_t len);

/** Telnet implementation of the protocol vtable. */
extern const struct protocol_ops telnet_protocol;

#endif /* TELNET_SESSION_H */
