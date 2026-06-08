#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <sys/socket.h>

typedef struct protocol_ops protocol_ops_t;

struct protocol_ops {
    const char  *name;
    const char  *description;
    void        *(*create_session)(int sockfd, const struct sockaddr_storage *peer);
    void         (*run_session)(void *session);
    void         (*request_shutdown)(void *session);
};

extern const protocol_ops_t *protocol_registry[];

#endif /* PROTOCOL_H */
