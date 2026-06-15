/**
 * @file http_session.c
 * @brief HTTP transport: request read, response/SSE writers, shutdown.
 *
 * Mirrors telnet_session.c's connection lifecycle (self-pipe, session
 * registry, poll-based wait, shutdown_all) but speaks HTTP. Routing and
 * payloads live in http_chat.c, reached through http_dispatch().
 */

#include "http_session.h"
#include "../../core/log.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/socket.h>
#include <assert.h>

#define HTTP_RECV_MAX      16384  /**< Max request size (headers + body). */
#define HTTP_READ_TIMEOUT  10000  /**< ms to wait for a complete request. */

struct http_conn {
    int               sockfd;
    int               notify_rfd;   /* self-pipe read end (O_NONBLOCK) */
    int               notify_wfd;   /* self-pipe write end (for publishers) */
    bool              responded;    /* a response/SSE header was sent */
    volatile bool     shutdown_requested;
    struct http_conn *reg_next;
};

/* -------------------------------------------------------------------------
 * Session registry — wake every connection on server shutdown.
 * ---------------------------------------------------------------------- */

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static http_conn_t    *s_head;
static bool            s_shutting_down;

/** @brief Add a connection to the registry; flag it if shutdown is underway. */
static void
session_register (http_conn_t *c)
{
    pthread_mutex_lock (&s_lock);
    c->reg_next = s_head;
    s_head      = c;
    if (s_shutting_down)
        c->shutdown_requested = true;
    pthread_mutex_unlock (&s_lock);
}

/** @brief Remove a connection from the registry before its fds close. */
static void
session_unregister (http_conn_t *c)
{
    pthread_mutex_lock (&s_lock);
    for (http_conn_t **pp = &s_head; *pp; pp = &(*pp)->reg_next) {
        if (*pp == c) { *pp = c->reg_next; break; }
    }
    pthread_mutex_unlock (&s_lock);
}

/** @brief Flag every live connection and wake its poll() via the self-pipe. */
static void
http_shutdown_all (void)
{
    pthread_mutex_lock (&s_lock);
    s_shutting_down = true;
    for (http_conn_t *c = s_head; c; c = c->reg_next) {
        c->shutdown_requested = true;
        if (c->notify_wfd >= 0) {
            uint8_t one = 1;
            (void) !write (c->notify_wfd, &one, 1);
        }
    }
    pthread_mutex_unlock (&s_lock);
}

/* -------------------------------------------------------------------------
 * Low-level writes
 * ---------------------------------------------------------------------- */

/**
 * @brief Write the whole buffer, retrying short writes.
 * @return 0 on success, -1 on error (e.g. the client went away).
 */
static int
send_all (int fd, const char *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t n = send (fd, buf + off, len - off, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t) n;
    }
    return 0;
}

/** @brief HTTP status code → reason phrase. */
static const char *
status_reason (int status)
{
    switch (status) {
    case 200: return "OK";
    case 201: return "Created";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 408: return "Request Timeout";
    case 413: return "Payload Too Large";
    case 500: return "Internal Server Error";
    default:  return "OK";
    }
}

/* -------------------------------------------------------------------------
 * Response helpers
 * ---------------------------------------------------------------------- */

void
http_respond (http_conn_t *c, int status, const char *content_type,
              const char *body, size_t body_len)
{
    char hdr[512];
    int n = snprintf (hdr, sizeof (hdr),
                      "HTTP/1.1 %d %s\r\n"
                      "Content-Type: %s\r\n"
                      "Content-Length: %zu\r\n"
                      "Access-Control-Allow-Origin: *\r\n"
                      "Connection: close\r\n"
                      "\r\n",
                      status, status_reason (status), content_type, body_len);
    if (n > 0)
        send_all (c->sockfd, hdr, (size_t) n);
    if (body && body_len)
        send_all (c->sockfd, body, body_len);
    c->responded = true;
}

void
http_respond_json (http_conn_t *c, int status, const char *json)
{
    http_respond (c, status, "application/json", json, strlen (json));
}

void
http_respond_text (http_conn_t *c, int status, const char *text)
{
    http_respond (c, status, "text/plain; charset=utf-8", text, strlen (text));
}

/* -------------------------------------------------------------------------
 * Server-Sent Events
 * ---------------------------------------------------------------------- */

void
http_sse_begin (http_conn_t *c)
{
    static const char hdr[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";
    send_all (c->sockfd, hdr, sizeof (hdr) - 1);
    c->responded = true;
}

int
http_sse_send (http_conn_t *c, const char *data, size_t len)
{
    if (send_all (c->sockfd, "data: ", 6) < 0) return -1;
    if (send_all (c->sockfd, data, len)    < 0) return -1;
    if (send_all (c->sockfd, "\n\n", 2)    < 0) return -1;
    return 0;
}

int
http_sse_comment (http_conn_t *c, const char *text)
{
    if (send_all (c->sockfd, ": ", 2)            < 0) return -1;
    if (send_all (c->sockfd, text, strlen (text)) < 0) return -1;
    if (send_all (c->sockfd, "\n\n", 2)          < 0) return -1;
    return 0;
}

int
http_conn_notify_wfd (http_conn_t *c)
{
    return c->notify_wfd;
}

http_wait_t
http_conn_wait (http_conn_t *c, int timeout_ms)
{
    if (c->shutdown_requested)
        return HTTP_WAIT_CLOSED;

    struct pollfd p[2];
    p[0].fd = c->sockfd;     p[0].events = POLLIN;
    p[1].fd = c->notify_rfd; p[1].events = POLLIN;

    int n = poll (p, 2, timeout_ms);
    if (n < 0)
        return (errno == EINTR) ? HTTP_WAIT_TIMEOUT : HTTP_WAIT_CLOSED;
    if (n == 0)
        return HTTP_WAIT_TIMEOUT;

    if (p[1].revents & POLLIN) {
        uint8_t drain[64];
        while (read (c->notify_rfd, drain, sizeof (drain)) > 0)
            ;
        return c->shutdown_requested ? HTTP_WAIT_CLOSED : HTTP_WAIT_NOTIFY;
    }
    if (p[0].revents & (POLLIN | POLLHUP | POLLERR)) {
        /* We don't expect data on an SSE stream; any read of 0 means the
         * client closed. Stray data is ignored as a keep-alive tick. */
        char tmp[256];
        ssize_t r = recv (c->sockfd, tmp, sizeof (tmp), MSG_DONTWAIT);
        if (r <= 0)
            return HTTP_WAIT_CLOSED;
        return HTTP_WAIT_TIMEOUT;
    }
    return HTTP_WAIT_TIMEOUT;
}

/* -------------------------------------------------------------------------
 * Protocol vtable
 * ---------------------------------------------------------------------- */

/** @brief Allocate a connection and its non-blocking self-pipe. */
static void *
http_create_session (int sockfd, const struct sockaddr_storage *peer)
{
    assert (sockfd >= 0);
    (void) peer;

    http_conn_t *c = calloc (1, sizeof (*c));
    if (!c) {
        LOG_ERR ("http_create_session: calloc failed");
        return NULL;
    }
    c->sockfd = sockfd;

    int fds[2];
    if (pipe2 (fds, O_NONBLOCK) == 0) {
        c->notify_rfd = fds[0];
        c->notify_wfd = fds[1];
    } else {
        c->notify_rfd = -1;
        c->notify_wfd = -1;
    }
    return c;
}

/**
 * @brief Accumulate bytes until a complete request is parsed.
 *
 * Sends a 400/413 error response itself on malformed or oversized input.
 * @return Request length on success, or -1 to abort the connection.
 */
static int
read_request (http_conn_t *c, char *buf, size_t bufsz, http_request_t *req)
{
    size_t have = 0;
    for (;;) {
        struct pollfd pf = { .fd = c->sockfd, .events = POLLIN };
        int pr = poll (&pf, 1, HTTP_READ_TIMEOUT);
        if (pr < 0) {
            if (errno == EINTR) continue;         /* interrupted — retry */
            return -1;
        }
        if (pr == 0) {                            /* client went quiet */
            http_respond_text (c, 408, "Request Timeout\n");
            return -1;
        }

        ssize_t n = recv (c->sockfd, buf + have, bufsz - have, 0);
        if (n < 0) {
            if (errno == EINTR) continue;         /* interrupted — retry */
            return -1;
        }
        if (n == 0)                               /* peer closed */
            return -1;
        have += (size_t) n;

        int total = http_parse_request (buf, have, req);
        if (total > 0)
            return total;
        if (total < 0) {
            http_respond_text (c, 400, "Bad Request\n");
            return -1;
        }
        if (have >= bufsz) {                       /* never completed */
            http_respond_text (c, 413, "Payload Too Large\n");
            return -1;
        }
    }
}

/**
 * @brief Close a socket without discarding the response we just sent.
 *
 * If the client still has unread request bytes queued (e.g. we rejected an
 * oversized request before reading it all), a plain close() makes the
 * kernel send RST, which can drop the in-flight response. Sending our FIN
 * first (shutdown) and draining the remainder makes it a clean close so the
 * client reliably receives the response. Bounded so a stuck peer can't hang
 * the thread.
 */
static void
close_graceful (int fd)
{
    shutdown (fd, SHUT_WR);
    char tmp[512];
    for (int i = 0; i < 20; i++) {                /* ~200ms budget */
        struct pollfd pf = { .fd = fd, .events = POLLIN };
        if (poll (&pf, 1, 10) <= 0) break;
        ssize_t n = recv (fd, tmp, sizeof (tmp), 0);
        if (n <= 0) break;                        /* EOF or error → done */
    }
    close (fd);
}

/** @brief Connection body: read one request, dispatch it, then close. */
static void
http_run_session (void *session)
{
    http_conn_t *c = (http_conn_t *) session;
    session_register (c);

    char           buf[HTTP_RECV_MAX];
    http_request_t req;
    memset (&req, 0, sizeof (req));

    int total = read_request (c, buf, sizeof (buf), &req);
    if (total > 0) {
        http_dispatch (c, &req);
        if (!c->responded)
            http_respond_text (c, 404, "Not Found\n");
    }

    session_unregister (c);
    if (c->notify_rfd >= 0) close (c->notify_rfd);
    if (c->notify_wfd >= 0) close (c->notify_wfd);
    close_graceful (c->sockfd);
    free (c);
}

/** @brief Flag one connection to stop (async; the poll loop notices). */
static void
http_request_shutdown (void *session)
{
    if (session)
        ((http_conn_t *) session)->shutdown_requested = true;
}

const protocol_ops_t http_protocol = {
    .name             = "http",
    .description      = "HTTP REST + SSE chat API",
    .create_session   = http_create_session,
    .run_session      = http_run_session,
    .request_shutdown = http_request_shutdown,
    .shutdown_all     = http_shutdown_all,
};
