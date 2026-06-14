#ifndef HTTP_SESSION_H
#define HTTP_SESSION_H

/**
 * @file http_session.h
 * @brief HTTP listener: transport, response/SSE helpers and dispatch hook.
 *
 * A second ::protocol_ops_t (sibling of telnet_protocol) plugged into the
 * same accept loop. It owns the HTTP transport — reading a request,
 * writing responses, holding SSE streams open — and delegates routing to
 * the application via http_dispatch(), which http_chat.c implements. The
 * transport therefore knows nothing about chat; the chat API knows
 * nothing about sockets.
 *
 * Like telnet sessions, each connection owns a non-blocking self-pipe and
 * is tracked in a registry so server shutdown can wake and drop even
 * long-lived SSE streams promptly.
 */

#include <stddef.h>
#include "http_proto.h"
#include "../protocol.h"

/** Opaque per-request/-connection handle passed to handlers. */
typedef struct http_conn http_conn_t;

/** Outcome of waiting on an open SSE stream. */
typedef enum {
    HTTP_WAIT_NOTIFY,   /**< A subscribed publisher signalled new data. */
    HTTP_WAIT_TIMEOUT,  /**< Idle tick — a good moment for a heartbeat. */
    HTTP_WAIT_CLOSED    /**< Client disconnected or server shutting down. */
} http_wait_t;

/** @name One-shot responses (call exactly one per request, or begin SSE) */
/**@{*/
void http_respond (http_conn_t *c, int status, const char *content_type,
                   const char *body, size_t body_len);
void http_respond_json (http_conn_t *c, int status, const char *json);
void http_respond_text (http_conn_t *c, int status, const char *text);
/**@}*/

/** @name Server-Sent Events */
/**@{*/
/** @brief Send the SSE response headers; the connection then stays open. */
void http_sse_begin (http_conn_t *c);
/** @brief Send one `data: <data>\n\n` frame. @return 0 ok, -1 write error. */
int  http_sse_send (http_conn_t *c, const char *data, size_t len);
/** @brief Send a `: <text>\n\n` comment (heartbeat). @return 0 ok, -1 error. */
int  http_sse_comment (http_conn_t *c, const char *text);
/** @brief The self-pipe write end to hand to a publisher (chat_room_subscribe). */
int  http_conn_notify_wfd (http_conn_t *c);
/** @brief Block until a notify, an idle tick, or the connection closes. */
http_wait_t http_conn_wait (http_conn_t *c, int timeout_ms);
/**@}*/

/**
 * @brief Application route dispatch — implemented by http_chat.c.
 *
 * Runs on the connection thread. Must either send exactly one response
 * (http_respond*) or begin and drive an SSE stream before returning.
 */
void http_dispatch (http_conn_t *c, const http_request_t *req);

/** HTTP implementation of the protocol vtable. */
extern const struct protocol_ops http_protocol;

#endif /* HTTP_SESSION_H */
