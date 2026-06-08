#ifndef SERVER_H
#define SERVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <signal.h>

#include "err.h"
#include "connector.h"
#include "../protocol/protocol.h"

#define SERVER_MAX_LISTENERS  8

typedef struct {
    uint16_t              port;
    const protocol_ops_t *proto;
    int                   listen_fd;
} listener_t;

struct server {
    listener_t      listeners[SERVER_MAX_LISTENERS];
    uint32_t        listener_count;
    connector_t     connector;
    uint32_t        max_connections;
    atomic_uint     active_connections;
};

typedef struct server server_t;

ns_err_t server_init         (server_t *srv, uint32_t max_conn, size_t stack_size);
ns_err_t server_add_listener (server_t *srv, uint16_t port, const protocol_ops_t *proto);
void     server_run          (server_t *srv);
void     server_shutdown     (server_t *srv);
void     server_reload       (server_t *srv);
void     server_destroy      (server_t *srv);
void     server_install_signals (void);

#endif /* SERVER_H */
