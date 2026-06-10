#ifndef SERVER_H
#define SERVER_H

/**
 * @file server.h
 * @brief Accept loop: listeners, signal handling and connection dispatch.
 *
 * The server owns one or more listening sockets, each bound to a
 * ::protocol_ops_t vtable. server_run() multiplexes the listeners with
 * select(), accepts incoming connections and hands each one to the
 * ::connector_t, which runs it on its own detached thread.
 *
 * Lifecycle: server_init() → server_add_listener()× → server_run()
 * (blocks until SIGINT/SIGTERM) → server_destroy().
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <signal.h>

#include "err.h"
#include "connector.h"
#include "../protocol/protocol.h"

#define SERVER_MAX_LISTENERS  8  /**< Max simultaneous listening ports. */

/** One listening socket bound to a protocol implementation. */
typedef struct {
    uint16_t              port;       /**< TCP port. */
    const protocol_ops_t *proto;      /**< Protocol vtable for accepted conns. */
    int                   listen_fd;  /**< Listening socket fd. */
} listener_t;

/** Server instance — typically one per process, stack-allocated in main(). */
struct server {
    listener_t      listeners[SERVER_MAX_LISTENERS];
    uint32_t        listener_count;
    connector_t     connector;           /**< Thread spawner / slot limiter. */
    uint32_t        max_connections;
    atomic_uint     active_connections;  /**< Live connection count (stats). */
};

typedef struct server server_t;

/**
 * @brief Initialize the server and its connector.
 * @param srv        Caller-owned storage.
 * @param max_conn   Maximum concurrent connections.
 * @param stack_size Per-connection thread stack size in bytes (≥ 64 KiB).
 */
ns_err_t server_init         (server_t *srv, uint32_t max_conn, size_t stack_size);

/**
 * @brief Bind and listen on @p port, serving protocol @p proto.
 * @return ::NS_OK, or ::NS_ERR_BIND_FAILED / ::NS_ERR_LISTEN_FAILED /
 *         ::NS_ERR_INVALID_STATE when the listener table is full.
 */
ns_err_t server_add_listener (server_t *srv, uint16_t port, const protocol_ops_t *proto);

/**
 * @brief Run the accept loop. Blocks until SIGINT/SIGTERM or fatal error.
 *
 * SIGHUP triggers server_reload() without interrupting service.
 */
void     server_run          (server_t *srv);

/** @brief Request the accept loop to stop (signal-safe flag set). */
void     server_shutdown     (server_t *srv);

/** @brief Configuration reload hook, invoked on SIGHUP. Currently a no-op. */
void     server_reload       (server_t *srv);

/**
 * @brief Close listeners and wait for all connection threads to finish.
 * @warning Blocks until every active session ends (see connector_destroy()).
 */
void     server_destroy      (server_t *srv);

/** @brief Install SIGINT/SIGTERM/SIGHUP handlers and ignore SIGPIPE. */
void     server_install_signals (void);

#endif /* SERVER_H */
