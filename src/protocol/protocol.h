#ifndef PROTOCOL_H
#define PROTOCOL_H

/**
 * @file protocol.h
 * @brief Protocol abstraction — Strategy vtable per wire protocol.
 *
 * The accept loop is protocol-agnostic: each listener carries a
 * ::protocol_ops_t and every accepted connection runs through it.
 * Adding a second protocol (SSH, raw TCP, WebSocket bridge, …) means
 * implementing this vtable and registering a listener — no changes to
 * the core server.
 */

#include <sys/socket.h>

typedef struct protocol_ops protocol_ops_t;

/** Protocol implementation vtable. All instances are static/const. */
struct protocol_ops {
    const char  *name;         /**< Short identifier for logs ("telnet"). */
    const char  *description;  /**< Human-readable description. */

    /**
     * @brief Allocate session state for one accepted connection.
     * @return Opaque session pointer, or NULL on failure (the caller
     *         then closes the socket).
     */
    void        *(*create_session)(int sockfd, const struct sockaddr_storage *peer);

    /**
     * @brief Run the session to completion on the connection thread.
     *        Must close the socket and free the session before returning.
     */
    void         (*run_session)(void *session);

    /** @brief Ask a running session to terminate (async, thread-safe). */
    void         (*request_shutdown)(void *session);

    /**
     * @brief Ask all running sessions of this protocol to terminate and
     *        wake their event loops. Called once at server shutdown,
     *        before waiting for connection threads. May be NULL.
     */
    void         (*shutdown_all)(void);
};

/** NULL-terminated list of all linked protocols. */
extern const protocol_ops_t *protocol_registry[];

#endif /* PROTOCOL_H */
