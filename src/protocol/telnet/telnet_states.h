#ifndef TELNET_STATES_H
#define TELNET_STATES_H

typedef enum {
    TSTATE_INIT        = 0,
    TSTATE_NEGOTIATING = 1,
    TSTATE_MAIN_MENU   = 2,
    TSTATE_IN_GAME     = 3,
    TSTATE_CLOSING     = 4,
    TSTATE_CLOSED      = 5,
    TSTATE__COUNT
} telnet_state_t;

typedef enum {
    TEV_CONNECTED   = 0,
    TEV_NEGO_DONE   = 1,
    TEV_KEY_INPUT   = 2,
    TEV_MENU_SELECT = 3,
    TEV_GAME_OVER   = 4,
    TEV_NAWS        = 5,
    TEV_DISCONNECT  = 6,
    TEV_TIMEOUT     = 7,
    TEV__COUNT
} telnet_event_t;

#endif /* TELNET_STATES_H */
