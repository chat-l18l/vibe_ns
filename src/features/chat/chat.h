#ifndef CHAT_H
#define CHAT_H

/**
 * @file chat.h
 * @brief Multi-room chat feature (menu entry point).
 *
 * Implements the ::game_ops_t interface. Internally a four-state flow:
 * username entry → lobby (room list) → in-room (history view) →
 * compose. Room storage and cross-session delivery live in room.h.
 */

#include "../../games/game.h"

/** Chat feature vtable; registered in the main menu under 'g'. */
extern const game_ops_t chat_feature;

#endif /* CHAT_H */
