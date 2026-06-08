#ifndef CONNECTION_H
#define CONNECTION_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>
#include <stdbool.h>

#include "err.h"
#include "../protocol/protocol.h"

typedef struct server server_t;

typedef struct {
    int                      sockfd;
    struct sockaddr_storage  peer_addr;
    char                     peer_str[64];
    const protocol_ops_t    *proto;
    void                    *proto_session;
    server_t                *server;
    time_t                   connect_time;
    volatile bool            shutdown_requested;
} connection_t;

ns_err_t connection_create (int sockfd,
                             const struct sockaddr_storage *peer,
                             const protocol_ops_t *proto,
                             server_t *srv,
                             connection_t **out);

void *connection_run (void *arg);

#endif /* CONNECTION_H */
