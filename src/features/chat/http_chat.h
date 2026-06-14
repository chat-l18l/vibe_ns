#ifndef HTTP_CHAT_H
#define HTTP_CHAT_H

/**
 * @file http_chat.h
 * @brief REST + SSE chat handlers behind the HTTP listener.
 *
 * Implements http_dispatch() (declared in http_session.h): it routes
 * HTTP requests onto the chat service (room.h) and serializes JSON. The
 * HTTP transport knows nothing about chat; this module is the only place
 * the two meet. There is no separate public API — the linker wires
 * http_dispatch into http_session.c.
 */

#endif /* HTTP_CHAT_H */
