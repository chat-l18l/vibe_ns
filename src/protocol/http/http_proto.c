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

int
http_parse_request (const char *buf, size_t len, http_request_t *out)
{
    const char *hdr_end = memmem (buf, len, "\r\n\r\n", 4);
    if (!hdr_end)
        return 0;                                 /* headers incomplete */
    size_t header_len = (size_t) (hdr_end - buf) + 4;

    const char *line_end = memchr (buf, '\r', len);
    if (!line_end)
        return -1;

    /* method */
    const char *sp1 = memchr (buf, ' ', (size_t) (line_end - buf));
    if (!sp1)
        return -1;
    size_t mlen = (size_t) (sp1 - buf);
    if      (mlen == 3 && !memcmp (buf, "GET", 3))    out->method = HTTP_M_GET;
    else if (mlen == 4 && !memcmp (buf, "POST", 4))   out->method = HTTP_M_POST;
    else if (mlen == 3 && !memcmp (buf, "PUT", 3))    out->method = HTTP_M_PUT;
    else if (mlen == 6 && !memcmp (buf, "DELETE", 6)) out->method = HTTP_M_DELETE;
    else                                              out->method = HTTP_M_OTHER;

    /* request target: PATH[?QUERY] */
    const char *target = sp1 + 1;
    const char *sp2 = memchr (target, ' ', (size_t) (line_end - target));
    if (!sp2)
        return -1;
    size_t tlen = (size_t) (sp2 - target);
    const char *q = memchr (target, '?', tlen);
    size_t plen = q ? (size_t) (q - target) : tlen;

    url_decode (target, plen, out->path, sizeof (out->path), false);
    if (q) {
        size_t qlen = (size_t) (sp2 - (q + 1));
        size_t cp = qlen < sizeof (out->query) - 1 ? qlen : sizeof (out->query) - 1;
        memcpy (out->query, q + 1, cp);
        out->query[cp] = '\0';
    } else {
        out->query[0] = '\0';
    }

    /* headers */
    out->content_length = 0;
    out->keep_alive     = false;
    const char *p = line_end + 2;                 /* first header line */
    while (p < hdr_end) {
        const char *eol = memchr (p, '\r', (size_t) (hdr_end - p));
        if (!eol) break;
        size_t hlen = (size_t) (eol - p);
        if (hlen >= 15 && !strncasecmp (p, "Content-Length:", 15)) {
            out->content_length = strtol (p + 15, NULL, 10);
            if (out->content_length < 0) out->content_length = 0;
        } else if (hlen >= 11 && !strncasecmp (p, "Connection:", 11)) {
            if (memmem (p + 11, hlen - 11, "keep-alive", 10))
                out->keep_alive = true;
        }
        p = eol + 2;
    }

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
