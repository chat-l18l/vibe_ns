#ifndef CONNECTOR_H
#define CONNECTOR_H

#include <stdint.h>
#include <stddef.h>
#include <semaphore.h>
#include <stdatomic.h>
#include "err.h"

typedef struct {
    uint32_t     max_connections;
    size_t       stack_size;
    sem_t        slots;
    atomic_uint  active;
} connector_t;

ns_err_t connector_init    (connector_t *c, uint32_t max_conn, size_t stack_size);
ns_err_t connector_spawn   (connector_t *c, void *(*fn)(void *), void *arg);
void     connector_destroy (connector_t *c);

#endif /* CONNECTOR_H */
