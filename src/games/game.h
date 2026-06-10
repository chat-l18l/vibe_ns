#ifndef GAME_H
#define GAME_H

/**
 * @file game.h
 * @brief Game/feature plugin interface.
 *
 * Everything selectable from the main menu — games, chat, help —
 * implements this small Strategy vtable. The session FSM owns the
 * lifecycle: create() on menu selection, handle_key() per keypress
 * while TSTATE_IN_GAME, destroy() on exit or disconnect.
 *
 * Contract:
 *  - All callbacks run on the owning connection's thread; no locking
 *    is needed for session state.
 *  - Output goes exclusively through telnet_session_send().
 *  - create() may return a non-heap sentinel (e.g. @c (void*)1) when no
 *    state is needed, as long as destroy() tolerates it.
 */

#include <stdbool.h>

/* Forward declaration — game modules get the full type via telnet_session.h */
typedef struct telnet_session telnet_session_t;

typedef struct game_ops game_ops_t;

/** Game/feature vtable. Instances are static const, linked into menus. */
struct game_ops {
    const char *name;         /**< Display name. */
    const char *description;  /**< One-line description. */
    char        menu_key;     /**< Suggested menu hotkey. */

    /**
     * @brief Allocate and initialize state; draw the first screen.
     * @return Opaque state pointer, or NULL on failure (session returns
     *         to the main menu).
     */
    void *(*create)(telnet_session_t *session);

    /**
     * @brief Handle one keypress.
     * @return true to keep running, false to exit to the main menu
     *         (destroy() is then called by the session).
     */
    bool  (*handle_key)(void *state, char key);

    /** @brief Free state. Called on game-over or disconnect. */
    void  (*destroy)(void *state);
};

extern const game_ops_t game_chess;       /**< Chess (placeholder). */
extern const game_ops_t game_backgammon;  /**< Backgammon (placeholder). */

#endif /* GAME_H */
