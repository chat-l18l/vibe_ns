#ifndef FEATURES_STUBS_H
#define FEATURES_STUBS_H

#include "../games/game.h"

/*
 * BBS feature stubs — all return "coming soon" and immediately exit
 * back to the main menu. None of these crash; they are safe to select.
 */
extern const game_ops_t feature_text_adventure;  /* [A] */
extern const game_ops_t feature_trivia;          /* [T] */
extern const game_ops_t feature_reversi;         /* [R] */
extern const game_ops_t feature_whosonline;      /* [W] */
extern const game_ops_t feature_profile;         /* [P] */
extern const game_ops_t feature_alias;           /* [N] */
extern const game_ops_t feature_messageboard;    /* [M] */
extern const game_ops_t feature_help;            /* [H] */

#endif /* FEATURES_STUBS_H */
