/**
 * @file backgammon.c
 * @brief Backgammon placeholder — shows "not yet implemented" and returns.
 */

#include "../game.h"
#include "../../core/log.h"
#include "../../protocol/telnet/telnet_session.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

/** @brief Placeholder: show a "not implemented" notice, then return to menu. */
static void *
backgammon_create (telnet_session_t *session)
{
    assert (session != NULL);
    const char *msg =
        "\r\n\x1b[1;35m  Backgammon\x1b[0m\r\n"
        "  Not yet implemented. Returning to menu...\r\n\r\n";
    telnet_session_send (session, msg, strlen (msg));
    return (void *) 1;
}

static bool
backgammon_handle_key (void *state, char key)
{
    (void) state; (void) key;
    return false;
}

static void
backgammon_destroy (void *state)
{
    (void) state;
}

const game_ops_t game_backgammon = {
    .name        = "Backgammon",
    .description = "Play a game of backgammon",
    .menu_key    = 'b',
    .create      = backgammon_create,
    .handle_key  = backgammon_handle_key,
    .destroy     = backgammon_destroy,
};
