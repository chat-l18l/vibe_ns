#ifndef HTTP_PROTO_H
#define HTTP_PROTO_H

/**
 * @file http_proto.h
 * @brief Minimal HTTP/1.1 request parser.
 *
 * Pure decoding, no I/O — the analog of telnet_proto.c's IAC parser for
 * the HTTP listener. Handles the request line, headers we care about
 * (Content-Length, Connection) and an optional body. The API subset is
 * deliberate: we control both ends, so there is no chunked-request
 * support and no multi-request pipelining.
 */

#include <stddef.h>
#include <stdbool.h>

#define HTTP_PATH_MAX   1024  /**< Decoded request path buffer. */
#define HTTP_QUERY_MAX  1024  /**< Raw query-string buffer. */

/** Request method. Anything we don't route maps to HTTP_M_OTHER. */
typedef enum {
    HTTP_M_GET,
    HTTP_M_POST,
    HTTP_M_PUT,
    HTTP_M_DELETE,
    HTTP_M_OTHER
} http_method_t;

/** A parsed request. @c body points into the caller's buffer (not owned). */
typedef struct {
    http_method_t method;
    char          path[HTTP_PATH_MAX];    /**< Percent-decoded, no query. */
    char          query[HTTP_QUERY_MAX];  /**< Raw query string (no '?'). */
    long          content_length;
    const char   *body;
    size_t        body_len;
    bool          keep_alive;             /**< Connection: keep-alive seen. */
} http_request_t;

/**
 * @brief Try to parse one request from an accumulating buffer.
 * @return Total request length (headers + body) on success — the number
 *         of bytes consumed; 0 if more data is needed; -1 if malformed.
 */
int http_parse_request (const char *buf, size_t len, http_request_t *out);

/**
 * @brief Extract and percent-decode a query parameter.
 * @return 1 if @p key was found (value written to @p out), else 0.
 */
int http_query_get (const char *query, const char *key, char *out, size_t outsz);

/** @brief Method enum → "GET"/"POST"/... for logging. */
const char *http_method_name (http_method_t m);

#endif /* HTTP_PROTO_H */
