/**
 * @file connector.c
 * @brief Bounded detached-thread spawner (semaphore slot accounting).
 */

#include "connector.h"
#include "log.h"

#include <pthread.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>

typedef struct {
    connector_t  *connector;
    void         *(*fn)(void *);
    void          *arg;
} conn_wrap_t;

static void *
thread_entry (void *raw)
{
    conn_wrap_t *w = (conn_wrap_t *) raw;
    connector_t *c = w->connector;
    void        *(*fn)(void *) = w->fn;
    void         *arg = w->arg;
    free (w);

    atomic_fetch_add (&c->active, 1u);
    fn (arg);
    atomic_fetch_sub (&c->active, 1u);
    sem_post (&c->slots);
    return NULL;
}

ns_err_t
connector_init (connector_t *c, uint32_t max_conn, size_t stack_size)
{
    assert (c != NULL);
    assert (max_conn > 0);
    assert (stack_size >= 65536);

    c->max_connections = max_conn;
    c->stack_size      = stack_size;
    atomic_init (&c->active, 0u);

    if (sem_init (&c->slots, 0, max_conn) != 0) {
        LOG_ERR ("connector_init: sem_init failed: %m");
        return NS_ERR_THREAD;
    }

    LOG_INFO ("connector: max %u connections, stack %zu KB",
              max_conn, stack_size / 1024);
    return NS_OK;
}

ns_err_t
connector_spawn (connector_t *c, void *(*fn)(void *), void *arg)
{
    assert (c != NULL);
    assert (fn != NULL);

    if (sem_trywait (&c->slots) != 0) {
        LOG_WARN ("connector: all %u slots occupied", c->max_connections);
        return NS_ERR_MAX_CONNECTIONS;
    }

    conn_wrap_t *w = malloc (sizeof (*w));
    if (!w) {
        LOG_ERR ("connector_spawn: malloc failed");
        sem_post (&c->slots);
        return NS_ERR_NO_MEM;
    }
    w->connector = c;
    w->fn        = fn;
    w->arg       = arg;

    pthread_attr_t attr;
    pthread_attr_init (&attr);
    pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize   (&attr, c->stack_size);

    pthread_t tid;
    int rc = pthread_create (&tid, &attr, thread_entry, w);
    pthread_attr_destroy (&attr);

    if (rc != 0) {
        LOG_ERR ("connector_spawn: pthread_create failed: %m");
        free (w);
        sem_post (&c->slots);
        return NS_ERR_THREAD;
    }
    return NS_OK;
}

void
connector_destroy (connector_t *c)
{
    assert (c != NULL);

    uint32_t remaining = atomic_load (&c->active);
    if (remaining > 0)
        LOG_INFO ("connector: waiting for %u active connections", remaining);

    for (uint32_t i = 0; i < c->max_connections; i++)
        sem_wait (&c->slots);

    sem_destroy (&c->slots);
    LOG_INFO ("connector: all connections closed");
}
