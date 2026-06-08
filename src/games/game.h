#ifndef GAME_H
#define GAME_H

#include <stdbool.h>

/* Forward declaration — game modules get the full type via telnet_session.h */
typedef struct telnet_session telnet_session_t;

typedef struct game_ops game_ops_t;

struct game_ops {
    const char *name;
    const char *description;
    char        menu_key;

    /* Allocate and initialize game state. Returns NULL on failure. */
    void *(*create)(telnet_session_t *session);

    /* Handle one keypress. Returns false when the game is over. */
    bool  (*handle_key)(void *state, char key);

    /* Free game state. Called on game-over or disconnect. */
    void  (*destroy)(void *state);
};

extern const game_ops_t game_chess;
extern const game_ops_t game_backgammon;

#endif /* GAME_H */
