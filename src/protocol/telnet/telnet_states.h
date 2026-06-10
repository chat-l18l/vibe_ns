#ifndef TELNET_STATES_H
#define TELNET_STATES_H

/**
 * @file telnet_states.h
 * @brief States and events of the telnet session FSM.
 *
 * The transition table binding these together lives in
 * telnet_session.c (@c telnet_fsm_table).
 */

/** Session lifecycle states. */
typedef enum {
    TSTATE_INIT        = 0,  /**< Just accepted; nothing sent yet. */
    TSTATE_NEGOTIATING = 1,  /**< IAC options sent; awaiting replies/timeout. */
    TSTATE_MAIN_MENU   = 2,  /**< BBS main menu shown. */
    TSTATE_IN_GAME     = 3,  /**< A game/feature owns the keyboard. */
    TSTATE_CLOSING     = 4,  /**< Error landing state; only disconnect leaves it. */
    TSTATE_CLOSED      = 5,  /**< Terminal state — session loop exits. */
    TSTATE__COUNT
} telnet_state_t;

/** Session events. */
typedef enum {
    TEV_CONNECTED   = 0,  /**< Connection thread started. */
    TEV_NEGO_DONE   = 1,  /**< All option replies received, or timeout. */
    TEV_KEY_INPUT   = 2,  /**< One decoded keyboard byte (payload: char*). */
    TEV_MENU_SELECT = 3,  /**< Menu item chosen (payload: int* index). */
    TEV_GAME_OVER   = 4,  /**< Game/feature returned to menu. */
    TEV_NAWS        = 5,  /**< Terminal size received/changed. */
    TEV_DISCONNECT  = 6,  /**< Peer gone or quit requested. */
    TEV_TIMEOUT     = 7,  /**< Idle timeout expired. */
    TEV__COUNT
} telnet_event_t;

#endif /* TELNET_STATES_H */
