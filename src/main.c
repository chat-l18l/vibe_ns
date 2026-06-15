/**
 * @file main.c
 * @brief Entry point: parse args, bind listeners, drop privileges, run.
 */

#include <stdlib.h>

#include "core/server.h"
#include "core/args.h"
#include "core/priv.h"
#include "core/log.h"
#include "core/err.h"
#include "protocol/telnet/telnet_session.h"
#include "protocol/http/http_session.h"

int
main (int argc, char **argv)
{
    server_args_t args;
    NS_ERROR_CHECK (args_parse (argc, argv, &args));

    LOG_INFO ("netserver starting (telnet=%u http=%u max-conn=%u stack=%uKB)",
              args.port, args.http_port, args.max_conn, args.stack_kb);

    server_install_signals ();

    server_t srv;
    NS_ERROR_CHECK (server_init (&srv, args.max_conn, (size_t) args.stack_kb * 1024));
    NS_ERROR_CHECK (server_add_listener (&srv, args.port, &telnet_protocol));
    if (args.http_port)
        NS_ERROR_CHECK (server_add_listener (&srv, args.http_port, &http_protocol));

    if (args.drop_user)
        NS_ERROR_CHECK (drop_privileges (args.drop_user));

    server_run (&srv);
    server_destroy (&srv);

    LOG_INFO ("netserver stopped");
    return EXIT_SUCCESS;
}
