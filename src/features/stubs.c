/**
 * @file stubs.c
 * @brief "Coming soon" placeholder implementations of game_ops_t.
 */

#include "stubs.h"
#include "../protocol/telnet/telnet_session.h"

#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Shared helpers
 * ---------------------------------------------------------------------- */

static void *
coming_soon (telnet_session_t *s, const char *feature_name, const char *description)
{
    char buf[512];
    int n = snprintf (buf, sizeof (buf),
        "\x1b[2J\x1b[H"
        "\x1b[1;33m\r\n"
        "  %s\r\n"
        "\x1b[0m"
        "  \x1b[2m%s\x1b[0m\r\n"
        "\r\n"
        "  \x1b[2mKomt binnenkort — druk op een toets om terug te gaan\x1b[0m\r\n",
        feature_name, description);
    if (n > 0)
        telnet_session_send (s, buf, (size_t) n);
    return (void *) 1;  /* non-NULL = success, no heap allocation */
}

static bool
stub_handle_key (void *state, char key)
{
    (void) state;
    (void) key;
    return false;   /* immediately return to main menu */
}

static void
stub_destroy (void *state)
{
    (void) state;
}

/* -------------------------------------------------------------------------
 * Text Adventure
 * ---------------------------------------------------------------------- */

static void *
adventure_create (telnet_session_t *s)
{
    return coming_soon (s, "Text Adventure",
                        "Verken een tekstuele wereld vol gevaar en mysterie");
}

const game_ops_t feature_text_adventure = {
    .name        = "Text Adventure",
    .description = "Verken een tekstuele wereld vol gevaar en mysterie",
    .menu_key    = 'a',
    .create      = adventure_create,
    .handle_key  = stub_handle_key,
    .destroy     = stub_destroy,
};

/* -------------------------------------------------------------------------
 * Trivia
 * ---------------------------------------------------------------------- */

static void *
trivia_create (telnet_session_t *s)
{
    return coming_soon (s, "Trivia",
                        "Test je kennis met meerdere keuze vragen");
}

const game_ops_t feature_trivia = {
    .name        = "Trivia",
    .description = "Test je kennis met meerdere keuze vragen",
    .menu_key    = 't',
    .create      = trivia_create,
    .handle_key  = stub_handle_key,
    .destroy     = stub_destroy,
};

/* -------------------------------------------------------------------------
 * Reversi
 * ---------------------------------------------------------------------- */

static void *
reversi_create (telnet_session_t *s)
{
    return coming_soon (s, "Reversi",
                        "Klassiek strategiespel voor twee spelers");
}

const game_ops_t feature_reversi = {
    .name        = "Reversi",
    .description = "Klassiek strategiespel voor twee spelers",
    .menu_key    = 'r',
    .create      = reversi_create,
    .handle_key  = stub_handle_key,
    .destroy     = stub_destroy,
};

/* -------------------------------------------------------------------------
 * Who's Online
 * ---------------------------------------------------------------------- */

static void *
whosonline_create (telnet_session_t *s)
{
    unsigned online = s->online_count ? (unsigned) atomic_load (s->online_count) : 0;
    char buf[512];
    int n = snprintf (buf, sizeof (buf),
        "\x1b[2J\x1b[H"
        "\x1b[1;36m\r\n"
        "  WHO'S ONLINE\r\n"
        "\x1b[0m"
        "  \x1b[1m%u\x1b[0m gebruiker%s verbonden op dit moment.\r\n"
        "\r\n"
        "  \x1b[2m(gebruikerslijst volgt binnenkort)\x1b[0m\r\n"
        "\r\n"
        "  \x1b[2mDruk op een toets om terug te gaan\x1b[0m\r\n",
        online, online == 1 ? "" : "s");
    if (n > 0)
        telnet_session_send (s, buf, (size_t) n);
    return (void *) 1;
}

const game_ops_t feature_whosonline = {
    .name        = "Who's Online",
    .description = "Bekijk wie er nu verbonden is",
    .menu_key    = 'w',
    .create      = whosonline_create,
    .handle_key  = stub_handle_key,
    .destroy     = stub_destroy,
};

/* -------------------------------------------------------------------------
 * Profile
 * ---------------------------------------------------------------------- */

static void *
profile_create (telnet_session_t *s)
{
    return coming_soon (s, "Profiel",
                        "Bekijk en bewerk je gebruikersprofiel");
}

const game_ops_t feature_profile = {
    .name        = "Profiel",
    .description = "Bekijk en bewerk je gebruikersprofiel",
    .menu_key    = 'p',
    .create      = profile_create,
    .handle_key  = stub_handle_key,
    .destroy     = stub_destroy,
};

/* -------------------------------------------------------------------------
 * Alias
 * ---------------------------------------------------------------------- */

static void *
alias_create (telnet_session_t *s)
{
    return coming_soon (s, "Alias instellen",
                        "Kies een bijnaam waarmee andere gebruikers je zien");
}

const game_ops_t feature_alias = {
    .name        = "Alias instellen",
    .description = "Kies een bijnaam waarmee andere gebruikers je zien",
    .menu_key    = 'n',
    .create      = alias_create,
    .handle_key  = stub_handle_key,
    .destroy     = stub_destroy,
};

/* -------------------------------------------------------------------------
 * Message board
 * ---------------------------------------------------------------------- */

static void *
messageboard_create (telnet_session_t *s)
{
    return coming_soon (s, "Message Board",
                        "Lees en schrijf berichten voor alle gebruikers");
}

const game_ops_t feature_messageboard = {
    .name        = "Message Board",
    .description = "Lees en schrijf berichten voor alle gebruikers",
    .menu_key    = 'm',
    .create      = messageboard_create,
    .handle_key  = stub_handle_key,
    .destroy     = stub_destroy,
};

/* -------------------------------------------------------------------------
 * Help
 * ---------------------------------------------------------------------- */

static void *
help_create (telnet_session_t *s)
{
    const char *text =
        "\x1b[2J\x1b[H"
        "\x1b[1;37m\r\n"
        "  HELP\r\n"
        "\x1b[0m"
        "  Druk een letter in het hoofdmenu om een optie te selecteren.\r\n"
        "\r\n"
        "  \x1b[33m[C]\x1b[0m Chess           \x1b[33m[B]\x1b[0m Backgammon\r\n"
        "  \x1b[33m[A]\x1b[0m Text Adventure   \x1b[33m[T]\x1b[0m Trivia\r\n"
        "  \x1b[33m[R]\x1b[0m Reversi          \x1b[33m[W]\x1b[0m Who's Online\r\n"
        "  \x1b[33m[P]\x1b[0m Profiel          \x1b[33m[N]\x1b[0m Alias instellen\r\n"
        "  \x1b[33m[M]\x1b[0m Message board    \x1b[33m[Q]\x1b[0m Uitloggen\r\n"
        "\r\n"
        "  \x1b[2mDruk op een toets om terug te gaan\x1b[0m\r\n";
    telnet_session_send (s, text, strlen (text));
    return (void *) 1;
}

const game_ops_t feature_help = {
    .name        = "Help",
    .description = "Uitleg over alle beschikbare functies",
    .menu_key    = 'h',
    .create      = help_create,
    .handle_key  = stub_handle_key,
    .destroy     = stub_destroy,
};
