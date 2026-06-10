/**
 * @file connection.c
 * @brief Connection object lifecycle and the per-connection thread body.
 */

#include "connection.h"
#include "server.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdatomic.h>
#include <assert.h>

ns_err_t
connection_create (int sockfd,
                   const struct sockaddr_storage *peer,
                   const protocol_ops_t *proto,
                   server_t *srv,
                   connection_t **out)
{
    assert (sockfd >= 0);
    assert (peer  != NULL);
    assert (proto != NULL);
    assert (srv   != NULL);
    assert (out   != NULL);

    connection_t *conn = malloc (sizeof (*conn));
    if (!conn) {
        LOG_ERR ("connection_create: malloc failed");
        return NS_ERR_NO_MEM;
    }

    memset (conn, 0, sizeof (*conn));
    conn->sockfd       = sockfd;
    conn->peer_addr    = *peer;
    conn->proto        = proto;
    conn->server       = srv;
    conn->connect_time = time (NULL);

    if (peer->ss_family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in *) peer;
        inet_ntop (AF_INET, &sin->sin_addr,
                   conn->peer_str, sizeof (conn->peer_str) - 8);
        size_t n = strlen (conn->peer_str);
        snprintf (conn->peer_str + n, sizeof (conn->peer_str) - n,
                  ":%u", ntohs (sin->sin_port));
    } else {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *) peer;
        inet_ntop (AF_INET6, &sin6->sin6_addr,
                   conn->peer_str, sizeof (conn->peer_str) - 8);
        size_t n = strlen (conn->peer_str);
        snprintf (conn->peer_str + n, sizeof (conn->peer_str) - n,
                  ":%u", ntohs (sin6->sin6_port));
    }

    *out = conn;
    return NS_OK;
}

void *
connection_run (void *arg)
{
    assert (arg != NULL);
    connection_t *conn = (connection_t *) arg;

    atomic_fetch_add (&conn->server->active_connections, 1u);
    LOG_INFO ("connection from %s [%s]", conn->peer_str, conn->proto->name);

    conn->proto_session = conn->proto->create_session (conn->sockfd,
                                                       &conn->peer_addr);
    if (!conn->proto_session) {
        LOG_ERR ("create_session failed for %s", conn->peer_str);
        close (conn->sockfd);
    } else {
        conn->proto->run_session (conn->proto_session);
    }

    LOG_INFO ("connection closed: %s", conn->peer_str);
    atomic_fetch_sub (&conn->server->active_connections, 1u);
    free (conn);
    return NULL;
}
