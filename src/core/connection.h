#ifndef CONNECTION_H
#define CONNECTION_H

/**
 * @file connection.h
 * @brief Per-connection bookkeeping and thread entry point.
 *
 * A ::connection_t is the glue between the accept loop and a protocol
 * session: it owns the socket fd, remembers the peer address, and runs
 * the protocol vtable on the connection thread. The connection frees
 * itself at the end of connection_run() — the server must not touch it
 * after a successful connector_spawn().
 */

#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>
#include <stdbool.h>

#include "err.h"
#include "../protocol/protocol.h"

typedef struct server server_t;

/** One accepted connection (heap-allocated; self-freeing in connection_run). */
typedef struct {
    int                      sockfd;             /**< Accepted socket. */
    struct sockaddr_storage  peer_addr;          /**< Raw peer address (v4/v6). */
    char                     peer_str[64];       /**< "ip:port" for logging. */
    const protocol_ops_t    *proto;              /**< Protocol vtable. */
    void                    *proto_session;      /**< Session created by proto. */
    server_t                *server;             /**< Owning server (stats). */
    time_t                   connect_time;       /**< Accept timestamp. */
    volatile bool            shutdown_requested; /**< Reserved; not yet wired. */
} connection_t;

/**
 * @brief Allocate and initialize a connection object.
 * @param sockfd  Accepted socket (ownership transfers on success).
 * @param peer    Peer address from accept().
 * @param proto   Protocol vtable to run.
 * @param srv     Owning server.
 * @param out     Receives the new connection.
 * @return ::NS_OK or ::NS_ERR_NO_MEM.
 */
ns_err_t connection_create (int sockfd,
                             const struct sockaddr_storage *peer,
                             const protocol_ops_t *proto,
                             server_t *srv,
                             connection_t **out);

/**
 * @brief Thread entry point: runs the protocol session to completion,
 *        then frees the connection. Signature matches pthread start_routine.
 * @param arg  ::connection_t pointer.
 */
void *connection_run (void *arg);

#endif /* CONNECTION_H */
