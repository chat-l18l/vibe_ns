#include "server.h"
#include "connection.h"
#include "log.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <errno.h>
#include <assert.h>

static volatile sig_atomic_t s_shutdown = 0;
static volatile sig_atomic_t s_reload   = 0;

static void
sig_handler (int sig)
{
    if (sig == SIGINT || sig == SIGTERM)
        s_shutdown = 1;
    else if (sig == SIGHUP)
        s_reload = 1;
}

void
server_install_signals (void)
{
    struct sigaction sa;
    memset (&sa, 0, sizeof (sa));
    sa.sa_handler = sig_handler;
    sigemptyset (&sa.sa_mask);

    sigaction (SIGINT,  &sa, NULL);
    sigaction (SIGTERM, &sa, NULL);
    sigaction (SIGHUP,  &sa, NULL);

    sa.sa_handler = SIG_IGN;
    sigaction (SIGPIPE, &sa, NULL);
}

ns_err_t
server_init (server_t *srv, uint32_t max_conn, size_t stack_size)
{
    assert (srv        != NULL);
    assert (max_conn    > 0);
    assert (stack_size >= 65536);

    memset (srv, 0, sizeof (*srv));
    srv->max_connections = max_conn;
    atomic_init (&srv->active_connections, 0u);

    NS_RETURN_ON_ERROR (connector_init (&srv->connector, max_conn, stack_size));
    return NS_OK;
}

ns_err_t
server_add_listener (server_t *srv, uint16_t port, const protocol_ops_t *proto)
{
    assert (srv   != NULL);
    assert (proto != NULL);
    assert (port   > 0);

    if (srv->listener_count >= SERVER_MAX_LISTENERS) {
        LOG_ERR ("server_add_listener: max listeners reached");
        return NS_ERR_INVALID_STATE;
    }

    int fd = socket (AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG_ERR ("socket: %m");
        return NS_ERR_BIND_FAILED;
    }

    int one = 1;
    setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof (one));

    struct sockaddr_in addr;
    memset (&addr, 0, sizeof (addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons (port);

    if (bind (fd, (struct sockaddr *) &addr, sizeof (addr)) != 0) {
        LOG_ERR ("bind port %u: %m", port);
        close (fd);
        return NS_ERR_BIND_FAILED;
    }

    if (listen (fd, 128) != 0) {
        LOG_ERR ("listen: %m");
        close (fd);
        return NS_ERR_LISTEN_FAILED;
    }

    listener_t *l = &srv->listeners[srv->listener_count++];
    l->port       = port;
    l->proto      = proto;
    l->listen_fd  = fd;

    LOG_INFO ("listening on port %u [%s]", port, proto->name);
    return NS_OK;
}

void
server_reload (server_t *srv)
{
    assert (srv != NULL);
    LOG_INFO ("reloading configuration (active connections: %u)",
              atomic_load (&srv->active_connections));
    /* Config reload hook — extend here when we add a config file. */
}

void
server_run (server_t *srv)
{
    assert (srv != NULL);
    assert (srv->listener_count > 0);

    LOG_INFO ("server running (%u listeners, max %u connections)",
              srv->listener_count, srv->max_connections);

    while (!s_shutdown) {
        fd_set rfds;
        FD_ZERO (&rfds);
        int maxfd = -1;
        for (uint32_t i = 0; i < srv->listener_count; i++) {
            FD_SET (srv->listeners[i].listen_fd, &rfds);
            if (srv->listeners[i].listen_fd > maxfd)
                maxfd = srv->listeners[i].listen_fd;
        }

        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        int nready = select (maxfd + 1, &rfds, NULL, NULL, &tv);

        if (nready < 0) {
            if (errno == EINTR) {
                if (s_reload) {
                    server_reload (srv);
                    s_reload = 0;
                }
                continue;
            }
            LOG_ERR ("select: %m");
            break;
        }

        for (uint32_t i = 0; i < srv->listener_count; i++) {
            listener_t *l = &srv->listeners[i];
            if (!FD_ISSET (l->listen_fd, &rfds))
                continue;

            struct sockaddr_storage peer;
            socklen_t peer_len = sizeof (peer);
            int cfd = accept (l->listen_fd, (struct sockaddr *) &peer, &peer_len);
            if (cfd < 0) {
                if (errno == EINTR) continue;
                LOG_WARN ("accept: %m");
                continue;
            }

            connection_t *conn = NULL;
            if (connection_create (cfd, &peer, l->proto, srv, &conn) != NS_OK) {
                close (cfd);
                continue;
            }

            if (connector_spawn (&srv->connector, connection_run, conn) != NS_OK) {
                LOG_WARN ("connection rejected — at capacity");
                close (cfd);
                free (conn);
            }
        }
    }

    LOG_INFO ("server shutting down");
}

void
server_shutdown (server_t *srv)
{
    (void) srv;
    s_shutdown = 1;
}

void
server_destroy (server_t *srv)
{
    assert (srv != NULL);
    connector_destroy (&srv->connector);
    for (uint32_t i = 0; i < srv->listener_count; i++)
        close (srv->listeners[i].listen_fd);
    LOG_INFO ("server destroyed");
}
