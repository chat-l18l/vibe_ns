#ifndef TELNET_SESSION_H
#define TELNET_SESSION_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/socket.h>

#include "../../core/fsm.h"
#include "../../ui/menu.h"
#include "../../games/game.h"
#include "telnet_proto.h"
#include "telnet_states.h"

#define TELNET_RECV_BUFSZ    4096
#define TELNET_SEND_BUFSZ    8192
#define TELNET_IDLE_TIMEOUT  300
#define TELNET_NEGO_TIMEOUT  3

struct telnet_session {
    int                    sockfd;
    fsm_t                  fsm;
    iac_parser_t           iac;
    menu_t                 main_menu;
    const game_ops_t      *active_game;
    void                  *game_state;

    uint16_t               term_cols;
    uint16_t               term_rows;

    uint8_t                recv_buf[TELNET_RECV_BUFSZ];
    char                   send_buf[TELNET_SEND_BUFSZ];
    size_t                 send_len;

    bool                   echo_ok;
    bool                   sga_ok;
    bool                   naws_ok;
    uint8_t                nego_pending;

    int                    pending_selection;
    volatile bool          shutdown_requested;
};

typedef struct telnet_session telnet_session_t;

/* Called from game stubs to write output */
void telnet_session_send(telnet_session_t *session, const char *data, size_t len);

/* Protocol vtable entry point */
extern const struct protocol_ops telnet_protocol;

#endif /* TELNET_SESSION_H */
