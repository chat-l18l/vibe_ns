/**
 * @file http_chat.c
 * @brief HTTP routing, JSON serialization and the SSE event stream.
 *
 * The application half of the HTTP listener: maps requests to chat
 * service calls (room.h) and renders JSON. Reached via http_dispatch(),
 * which http_session.c calls on the connection thread.
 *
 * Endpoints:
 *   GET  /api/rooms
 *   GET  /api/rooms/{id}/messages?since=&limit=
 *   POST /api/rooms/{id}/messages           body {"user":..,"body":..}
 *   GET  /api/rooms/{id}/users
 *   GET  /api/events/rooms/{id}?user=        Server-Sent Events stream
 *   GET  /  /index.html  /app.js             static front-end (dev convenience)
 *   GET  /api/health
 */

#include "../../protocol/http/http_session.h"
#include "room.h"
#include "../../core/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#define SSE_KEEPALIVE_MS  25000  /**< Heartbeat / presence-refresh interval. */

/* -------------------------------------------------------------------------
 * JSON helpers
 * ---------------------------------------------------------------------- */

/** @brief Escape a string into a JSON string body (no surrounding quotes). */
static void
json_escape (const char *s, char *out, size_t outsz)
{
    size_t o = 0;
    for (size_t i = 0; s[i] && o + 7 < outsz; i++) {
        unsigned char ch = (unsigned char) s[i];
        switch (ch) {
        case '"':  out[o++] = '\\'; out[o++] = '"';  break;
        case '\\': out[o++] = '\\'; out[o++] = '\\'; break;
        case '\n': out[o++] = '\\'; out[o++] = 'n';  break;
        case '\r': out[o++] = '\\'; out[o++] = 'r';  break;
        case '\t': out[o++] = '\\'; out[o++] = 't';  break;
        default:
            if (ch < 0x20)
                o += (size_t) snprintf (out + o, outsz - o, "\\u%04x", ch);
            else
                out[o++] = (char) ch;
        }
    }
    out[o] = '\0';
}

/** @brief Render one message as a JSON object. @return bytes written. */
static int
msg_to_json (const chat_msg_t *m, char *out, size_t outsz)
{
    char eu[CHAT_USER_MAX * 2], eb[CHAT_BODY_MAX * 2];
    json_escape (m->user, eu, sizeof (eu));
    json_escape (m->body, eb, sizeof (eb));
    return snprintf (out, outsz,
                     "{\"id\":%ld,\"user\":\"%s\",\"ts\":%ld,\"body\":\"%s\"}",
                     m->id, eu, m->ts, eb);
}

/**
 * @brief Extract the JSON string value of @p key from a request body.
 *
 * Minimal scanner for our own client's payloads: handles the common
 * backslash escapes, not \\uXXXX. The body need not be NUL-terminated.
 * @return 1 if found (value written to @p out), else 0.
 */
static int
json_get_string (const char *body, size_t len, const char *key,
                 char *out, size_t outsz)
{
    char pat[64];
    int  pl = snprintf (pat, sizeof (pat), "\"%s\"", key);
    const char *p = memmem (body, len, pat, (size_t) pl);
    if (!p)
        return 0;
    p += pl;
    const char *end = body + len;
    while (p < end && (*p == ' ' || *p == '\t' || *p == ':')) p++;
    if (p >= end || *p != '"')
        return 0;
    p++;
    size_t o = 0;
    while (p < end && *p != '"' && o + 1 < outsz) {
        char ch = *p++;
        if (ch == '\\' && p < end) {
            char e = *p++;
            switch (e) {
            case 'n': ch = '\n'; break;
            case 't': ch = '\t'; break;
            case 'r': ch = '\r'; break;
            default:  ch = e;    break;   /* \" \\ \/ and others: literal */
            }
        }
        out[o++] = ch;
    }
    out[o] = '\0';
    return 1;
}

/* -------------------------------------------------------------------------
 * REST handlers
 * ---------------------------------------------------------------------- */

/** @brief GET /api/rooms → JSON array of {id, name}. */
static void
handle_rooms (http_conn_t *c)
{
    int           count;
    chat_room_t **rooms = chat_rooms_list (&count);

    char buf[2048];
    size_t o = 0;
    o += (size_t) snprintf (buf + o, sizeof (buf) - o, "[");
    for (int i = 0; i < count; i++) {
        char en[CHAT_NAME_MAX * 2];
        json_escape (rooms[i]->name, en, sizeof (en));
        o += (size_t) snprintf (buf + o, sizeof (buf) - o,
                                "%s{\"id\":%ld,\"name\":\"%s\"}",
                                i ? "," : "", rooms[i]->id, en);
    }
    snprintf (buf + o, sizeof (buf) - o, "]");
    http_respond_json (c, 200, buf);
}

/** @brief GET /api/rooms/{id}/messages?since=&limit= → JSON array. */
static void
handle_messages_get (http_conn_t *c, chat_room_t *room, const http_request_t *req)
{
    char tmp[32];
    long since = 0;
    int  limit = 50;
    if (http_query_get (req->query, "since", tmp, sizeof (tmp)))
        since = strtol (tmp, NULL, 10);
    if (http_query_get (req->query, "limit", tmp, sizeof (tmp)))
        limit = atoi (tmp);
    if (limit < 1)   limit = 1;
    if (limit > 100) limit = 100;

    chat_msg_t msgs[100];
    int n = chat_room_since (room, since, limit, msgs, 100);

    size_t cap = 64 + (size_t) n * (CHAT_BODY_MAX * 2 + CHAT_USER_MAX * 2 + 64);
    char  *buf = malloc (cap);
    if (!buf) { http_respond_json (c, 500, "{\"error\":\"oom\"}"); return; }

    size_t o = 0;
    o += (size_t) snprintf (buf + o, cap - o, "[");
    for (int i = 0; i < n; i++) {
        if (i) o += (size_t) snprintf (buf + o, cap - o, ",");
        o += (size_t) msg_to_json (&msgs[i], buf + o, cap - o);
    }
    snprintf (buf + o, cap - o, "]");
    http_respond_json (c, 200, buf);
    free (buf);
}

/** @brief POST /api/rooms/{id}/messages — body {user, body} → {id}. */
static void
handle_messages_post (http_conn_t *c, chat_room_t *room, const http_request_t *req)
{
    char user[CHAT_USER_MAX] = { 0 };
    char body[CHAT_BODY_MAX] = { 0 };
    json_get_string (req->body, req->body_len, "user", user, sizeof (user));
    json_get_string (req->body, req->body_len, "body", body, sizeof (body));

    if (!user[0] || !body[0]) {
        http_respond_json (c, 400, "{\"error\":\"user and body required\"}");
        return;
    }
    long id = chat_room_post (room, user, body);
    if (id < 0) {
        http_respond_json (c, 400, "{\"error\":\"invalid message\"}");
        return;
    }
    char out[64];
    snprintf (out, sizeof (out), "{\"id\":%ld}", id);
    http_respond_json (c, 201, out);
}

/** @brief GET /api/rooms/{id}/users → {count, users[]}. */
static void
handle_users (http_conn_t *c, chat_room_t *room)
{
    char users[64][CHAT_USER_MAX];
    int  n = chat_room_users (room, users, 64);

    char buf[64 * (CHAT_USER_MAX * 2 + 4) + 64];
    size_t o = 0;
    o += (size_t) snprintf (buf + o, sizeof (buf) - o, "{\"count\":%d,\"users\":[", n);
    for (int i = 0; i < n; i++) {
        char e[CHAT_USER_MAX * 2];
        json_escape (users[i], e, sizeof (e));
        o += (size_t) snprintf (buf + o, sizeof (buf) - o,
                                "%s\"%s\"", i ? "," : "", e);
    }
    snprintf (buf + o, sizeof (buf) - o, "]}");
    http_respond_json (c, 200, buf);
}

/* -------------------------------------------------------------------------
 * SSE event stream
 * ---------------------------------------------------------------------- */

/**
 * @brief GET /api/events/rooms/{id} — hold the connection open as an SSE
 *        stream, emitting one frame per new message until the client or the
 *        server closes it. Optional @c ?user= registers presence.
 */
static void
handle_sse (http_conn_t *c, chat_room_t *room, const http_request_t *req)
{
    char user[CHAT_USER_MAX] = { 0 };
    int  have_user = http_query_get (req->query, "user", user, sizeof (user))
                  && user[0];

    http_sse_begin (c);
    long cursor = chat_room_tail (room);          /* only stream new messages */
    int  wfd    = http_conn_notify_wfd (c);
    chat_room_subscribe (room, wfd);
    if (have_user)
        chat_room_presence_set (room, user);
    http_sse_comment (c, "connected");

    for (;;) {
        http_wait_t w = http_conn_wait (c, SSE_KEEPALIVE_MS);
        if (w == HTTP_WAIT_CLOSED)
            break;
        if (w == HTTP_WAIT_TIMEOUT) {
            if (have_user) chat_room_presence_set (room, user);
            if (http_sse_comment (c, "keepalive") < 0) break;
            continue;
        }
        /* HTTP_WAIT_NOTIFY: flush everything newer than our cursor */
        chat_msg_t msgs[64];
        int n = chat_room_since (room, cursor, 64, msgs, 64);
        int broke = 0;
        for (int i = 0; i < n; i++) {
            cursor = msgs[i].id;
            char j[CHAT_BODY_MAX * 2 + 128];
            msg_to_json (&msgs[i], j, sizeof (j));
            if (http_sse_send (c, j, strlen (j)) < 0) { broke = 1; break; }
        }
        if (broke)
            break;
    }

    chat_room_unsubscribe (room, wfd);
    if (have_user)
        chat_room_presence_clear (room, user);
}

/* -------------------------------------------------------------------------
 * Static files (dev convenience; production can front with nginx)
 * ---------------------------------------------------------------------- */

/** @brief Serve a fixed file from the web root (no path traversal possible). */
static void
serve_static (http_conn_t *c, const char *name, const char *ctype)
{
    const char *root = getenv ("NETSERVER_WEBROOT");
    char path[512];
    snprintf (path, sizeof (path), "%s/%s", root && root[0] ? root : "web", name);

    int fd = open (path, O_RDONLY);
    if (fd < 0) { http_respond_text (c, 404, "Not Found\n"); return; }

    struct stat st;
    if (fstat (fd, &st) != 0 || st.st_size <= 0 || st.st_size > 262144) {
        close (fd);
        http_respond_text (c, 404, "Not Found\n");
        return;
    }
    char *body = malloc ((size_t) st.st_size);
    if (!body) { close (fd); http_respond_text (c, 500, "oom\n"); return; }

    ssize_t got = read (fd, body, (size_t) st.st_size);
    close (fd);
    if (got > 0)
        http_respond (c, 200, ctype, body, (size_t) got);
    else
        http_respond_text (c, 404, "Not Found\n");
    free (body);
}

/* -------------------------------------------------------------------------
 * Router — the http_dispatch hook called by http_session.c
 * ---------------------------------------------------------------------- */

/**
 * @brief Match "/api/rooms/<id>/<tail>".
 * @return Pointer to <tail> with @p id set, or NULL if the path doesn't match.
 */
static const char *
match_room_sub (const char *path, long *id)
{
    static const char pfx[] = "/api/rooms/";
    if (strncmp (path, pfx, sizeof (pfx) - 1) != 0)
        return NULL;
    const char *p = path + sizeof (pfx) - 1;
    char *e;
    long  v = strtol (p, &e, 10);
    if (e == p || *e != '/')
        return NULL;
    *id = v;
    return e + 1;
}

/** @brief Route one request to the matching handler (the http_dispatch hook). */
void
http_dispatch (http_conn_t *c, const http_request_t *req)
{
    const char *path = req->path;

    if (req->method == HTTP_M_GET &&
        (!strcmp (path, "/") || !strcmp (path, "/index.html"))) {
        serve_static (c, "index.html", "text/html; charset=utf-8");
        return;
    }
    if (req->method == HTTP_M_GET && !strcmp (path, "/app.js")) {
        serve_static (c, "app.js", "application/javascript; charset=utf-8");
        return;
    }
    if (req->method == HTTP_M_GET && !strcmp (path, "/api/health")) {
        http_respond_json (c, 200, "{\"status\":\"ok\"}");
        return;
    }
    if (req->method == HTTP_M_GET && !strcmp (path, "/api/rooms")) {
        handle_rooms (c);
        return;
    }

    long id;
    const char *sub = match_room_sub (path, &id);
    if (sub) {
        chat_room_t *room = chat_room_by_id (id);
        if (!room) { http_respond_json (c, 404, "{\"error\":\"no such room\"}"); return; }

        if (!strcmp (sub, "messages")) {
            if (req->method == HTTP_M_GET)  { handle_messages_get  (c, room, req); return; }
            if (req->method == HTTP_M_POST) { handle_messages_post (c, room, req); return; }
            http_respond_text (c, 405, "Method Not Allowed\n");
            return;
        }
        if (!strcmp (sub, "users") && req->method == HTTP_M_GET) {
            handle_users (c, room);
            return;
        }
    }

    if (req->method == HTTP_M_GET &&
        !strncmp (path, "/api/events/rooms/", 18)) {
        long eid = strtol (path + 18, NULL, 10);
        chat_room_t *room = chat_room_by_id (eid);
        if (!room) { http_respond_json (c, 404, "{\"error\":\"no such room\"}"); return; }
        handle_sse (c, room, req);
        return;
    }

    http_respond_text (c, 404, "Not Found\n");
}
