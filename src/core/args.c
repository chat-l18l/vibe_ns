#include "args.h"
#include "log.h"

#include <argp.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

const char *argp_program_version     = "netserver 0.1";
const char *argp_program_bug_address = "<chat@l18l.nl>";

static const char doc[] =
    "netserver -- multi-protocol network game server\v"
    "Start without --user to run unprivileged on a high port.\n"
    "Start as root with --user=nobody to bind port 23 and drop privileges.";

static const struct argp_option options[] = {
    { "port",       'p', "PORT", 0, "Listen port (default: 23)",                    0 },
    { "max-conn",   'n', "N",    0, "Max concurrent connections (default: 2000)",   0 },
    { "stack",      's', "KB",   0, "Per-connection stack size in KB (default: 256)", 0 },
    { "user",       'u', "USER", 0, "Drop privileges to USER after bind",           0 },
    { "foreground", 'f', NULL,   0, "Run in foreground (default: yes)",             0 },
    { 0, 0, 0, 0, 0, 0 }
};

static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
    server_args_t *a = (server_args_t *) state->input;

    switch (key) {
    case 'p': {
        long v = strtol (arg, NULL, 10);
        if (v <= 0 || v > 65535) {
            argp_error (state, "port must be 1-65535, got: %s", arg);
            return ARGP_ERR_UNKNOWN;
        }
        a->port = (uint16_t) v;
        break;
    }
    case 'n': {
        long v = strtol (arg, NULL, 10);
        if (v <= 0 || v > 100000) {
            argp_error (state, "max-conn must be 1-100000, got: %s", arg);
            return ARGP_ERR_UNKNOWN;
        }
        a->max_conn = (uint32_t) v;
        break;
    }
    case 's': {
        long v = strtol (arg, NULL, 10);
        if (v < 64 || v > 8192) {
            argp_error (state, "stack must be 64-8192 KB, got: %s", arg);
            return ARGP_ERR_UNKNOWN;
        }
        a->stack_kb = (uint32_t) v;
        break;
    }
    case 'u':
        a->drop_user = arg;
        break;
    case 'f':
        a->foreground = true;
        break;
    case ARGP_KEY_END:
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static const struct argp argp_parser = {
    options, parse_opt, NULL, doc, NULL, NULL, NULL
};

ns_err_t
args_parse (int argc, char **argv, server_args_t *out)
{
    assert (out != NULL);

    out->port       = 23;
    out->max_conn   = 2000;
    out->stack_kb   = 256;
    out->drop_user  = NULL;
    out->foreground = true;

    if (argp_parse (&argp_parser, argc, argv, 0, NULL, out) != 0) {
        LOG_ERR ("argument parsing failed");
        return NS_ERR_INVALID_ARG;
    }
    return NS_OK;
}
