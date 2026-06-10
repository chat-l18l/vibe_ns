#ifndef FEATURES_STUBS_H
#define FEATURES_STUBS_H

/**
 * @file stubs.h
 * @brief Placeholder BBS features — "coming soon" screens.
 *
 * Each stub shows a description and returns to the main menu on any
 * key. They keep the menu honest while features are built one by one;
 * replace the extern here and the implementation in stubs.c when a
 * feature graduates to its own module (as chat/ did).
 */

#include "../games/game.h"

extern const game_ops_t feature_text_adventure;  /**< [A] */
extern const game_ops_t feature_trivia;          /**< [T] */
extern const game_ops_t feature_reversi;         /**< [R] */
extern const game_ops_t feature_whosonline;      /**< [W] — shows live count. */
extern const game_ops_t feature_profile;         /**< [P] */
extern const game_ops_t feature_alias;           /**< [N] */
extern const game_ops_t feature_messageboard;    /**< [M] */
extern const game_ops_t feature_help;            /**< [H] — key overview. */

#endif /* FEATURES_STUBS_H */
