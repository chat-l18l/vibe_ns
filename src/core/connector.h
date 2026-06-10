#ifndef CONNECTOR_H
#define CONNECTOR_H

/**
 * @file connector.h
 * @brief Bounded thread spawner — one detached pthread per connection.
 *
 * A counting semaphore caps the number of simultaneous connection
 * threads at @c max_connections. connector_spawn() takes a slot
 * (non-blocking; rejects when full) and the thread returns the slot
 * when its work function finishes.
 */

#include <stdint.h>
#include <stddef.h>
#include <semaphore.h>
#include <stdatomic.h>
#include "err.h"

/** Thread spawner state. Embedded in ::server_t. */
typedef struct {
    uint32_t     max_connections;  /**< Slot count. */
    size_t       stack_size;       /**< Per-thread stack size in bytes. */
    sem_t        slots;            /**< Free-slot semaphore. */
    atomic_uint  active;           /**< Currently running threads. */
} connector_t;

/**
 * @brief Initialize the connector.
 * @param c          Caller-owned storage.
 * @param max_conn   Maximum simultaneous threads (> 0).
 * @param stack_size Per-thread stack size in bytes (≥ 64 KiB).
 * @return ::NS_OK or ::NS_ERR_THREAD.
 */
ns_err_t connector_init    (connector_t *c, uint32_t max_conn, size_t stack_size);

/**
 * @brief Run @p fn(arg) on a new detached thread if a slot is free.
 * @return ::NS_OK, ::NS_ERR_MAX_CONNECTIONS when all slots are taken,
 *         ::NS_ERR_NO_MEM or ::NS_ERR_THREAD on resource failure.
 *         On any error the caller keeps ownership of @p arg.
 */
ns_err_t connector_spawn   (connector_t *c, void *(*fn)(void *), void *arg);

/**
 * @brief Wait for all threads to finish, then destroy the semaphore.
 * @warning Blocks until every spawned thread has returned its slot —
 *          with long-lived sessions this can take minutes (idle timeout).
 */
void     connector_destroy (connector_t *c);

#endif /* CONNECTOR_H */
