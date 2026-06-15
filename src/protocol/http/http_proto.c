/**
 * @file http_proto.c
 * @brief HTTP/1.1 request-line, header and query parsing.
 */

#include "http_proto.h"

#include <string.h>
#include <strings.h>
#include <stdlib.h>

const char *
http_method_name (http_method_t m)
{
    switch (m) {
    case HTTP_M_GET:    return "GET";
    case HTTP_M_POST:   return "POST";
    case HTTP_M_PUT:    return "PUT";
    case HTTP_M_DELETE: return "DELETE";
    default:            return "OTHER";
    }
}

/** @brief Hex digit → value 0–15, or -1 if not a hex digit. */
static int
hexval (char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/**
 * @brief Percent-decode @p src[0..slen) into @p dst (NUL-terminated).
 * @param plus_space  Decode '+' as space — true for query strings, not paths.
 */
static void
url_decode (const char *src, size_t slen, char *dst, size_t dsz, bool plus_space)
{
    size_t di = 0;
    for (size_t i = 0; i < slen && di + 1 < dsz; i++) {
        char ch = src[i];
        int  h, l;
        if (ch == '%' && i + 2 < slen &&
            (h = hexval (src[i + 1])) >= 0 && (l = hexval (src[i + 2])) >= 0) {
            dst[di++] = (char) ((h << 4) | l);
            i += 2;
        } else if (ch == '+' && plus_space) {
            dst[di++] = ' ';
        } else {
            dst[di++] = ch;
        }
    }
    dst[di] = '\0';
}

/**
 * @brief Return the next line as [start, start+*len), line-ending agnostic.
 *
 * Treats '\n' as the separator and strips a single trailing '\r', so CRLF,
 * bare LF and mixed endings all parse (RFC 7230 §3.5 robustness). A blank
 * line yields @c *len == 0.
 *
 * @param p     Cursor into the buffer.
 * @param end   One past the last valid byte.
 * @param len   Receives the line length excluding the ending.
 * @param next  Receives the cursor just past the '\n'.
 * @return Line start, or NULL if no complete line is buffered yet.
 */
static const char *
get_line (const char *p, const char *end, size_t *len, const char **next)
{
    const char *nl = memchr (p, '\n', (size_t) (end - p));
    if (!nl)
        return NULL;                              /* line not fully received */
    size_t l = (size_t) (nl - p);
    if (l > 0 && p[l - 1] == '\r')
        l--;                                      /* strip trailing CR */
    *len  = l;
    *next = nl + 1;
    return p;
}

/** @brief Parse the request line [line, line+llen): method, target, version. */
static int
parse_request_line (const char *line, size_t llen, http_request_t *out)
{
    const char *line_end = line + llen;

    const char *sp1 = memchr (line, ' ', llen);
    if (!sp1)
        return -1;                                /* no method/target split */

    size_t mlen = (size_t) (sp1 - line);
    if      (mlen == 3 && !memcmp (line, "GET", 3))    out->method = HTTP_M_GET;
    else if (mlen == 4 && !memcmp (line, "POST", 4))   out->method = HTTP_M_POST;
    else if (mlen == 3 && !memcmp (line, "PUT", 3))    out->method = HTTP_M_PUT;
    else if (mlen == 6 && !memcmp (line, "DELETE", 6)) out->method = HTTP_M_DELETE;
    else                                               out->method = HTTP_M_OTHER;

    /* target = up to the next space, or the rest of the line when no version
     * is given (HTTP/0.9-style "GET /") */
    const char *target = sp1 + 1;
    const char *sp2 = memchr (target, ' ', (size_t) (line_end - target));
    const char *target_end = sp2 ? sp2 : line_end;

    size_t tlen = (size_t) (target_end - target);
    const char *q = memchr (target, '?', tlen);
    size_t plen = q ? (size_t) (q - target) : tlen;

    url_decode (target, plen, out->path, sizeof (out->path), false);
    if (q) {
        size_t qlen = (size_t) (target_end - (q + 1));
        size_t cp = qlen < sizeof (out->query) - 1 ? qlen : sizeof (out->query) - 1;
        memcpy (out->query, q + 1, cp);
        out->query[cp] = '\0';
    } else {
        out->query[0] = '\0';
    }
    return 0;
}

int
http_parse_request (const char *buf, size_t len, http_request_t *out)
{
    const char *end = buf + len;
    const char *p   = buf;
    const char *next;
    size_t      llen;

    /* request line */
    const char *line = get_line (p, end, &llen, &next);
    if (!line)
        return 0;                                 /* need more */
    if (parse_request_line (line, llen, out) != 0)
        return -1;                                /* malformed */
    p = next;

    /* headers until a blank line */
    out->content_length = 0;
    out->keep_alive     = false;
    for (;;) {
        line = get_line (p, end, &llen, &next);
        if (!line)
            return 0;                             /* no blank line yet */
        if (llen == 0) {                          /* end of headers */
            p = next;
            break;
        }
        if (llen >= 15 && !strncasecmp (line, "Content-Length:", 15)) {
            out->content_length = strtol (line + 15, NULL, 10);
            if (out->content_length < 0) out->content_length = 0;
        } else if (llen >= 11 && !strncasecmp (line, "Connection:", 11)) {
            if (memmem (line + 11, llen - 11, "keep-alive", 10))
                out->keep_alive = true;
        }
        p = next;
    }

    /* body */
    size_t header_len = (size_t) (p - buf);
    size_t total = header_len + (size_t) out->content_length;
    if (len < total)
        return 0;                                 /* body incomplete */

    out->body     = buf + header_len;
    out->body_len = (size_t) out->content_length;
    return (int) total;
}

int
http_query_get (const char *query, const char *key, char *out, size_t outsz)
{
    size_t klen = strlen (key);
    const char *p = query;
    while (p && *p) {
        const char *amp = strchr (p, '&');
        size_t seg = amp ? (size_t) (amp - p) : strlen (p);
        if (seg > klen && p[klen] == '=' && !strncmp (p, key, klen)) {
            url_decode (p + klen + 1, seg - klen - 1, out, outsz, true);
            return 1;
        }
        p = amp ? amp + 1 : NULL;
    }
    return 0;
}
