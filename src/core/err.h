#ifndef NS_ERR_H
#define NS_ERR_H

#include <stdio.h>
#include <stdlib.h>

typedef int ns_err_t;

#define NS_OK    0
#define NS_FAIL -1

/* Memory / argument errors (0x101–0x11F) */
#define NS_ERR_NO_MEM           0x101
#define NS_ERR_INVALID_ARG      0x110
#define NS_ERR_INVALID_STATE    0x111

/* Network / server errors (0x201–0x20F) */
#define NS_ERR_BIND_FAILED      0x201
#define NS_ERR_LISTEN_FAILED    0x202
#define NS_ERR_CONN_FAILED      0x203
#define NS_ERR_CONN_LOST        0x204
#define NS_ERR_MAX_CONNECTIONS  0x205

/* Protocol errors (0x301–0x30F) */
#define NS_ERR_PROTO_INVALID    0x301
#define NS_ERR_PROTO_OVERFLOW   0x302
#define NS_ERR_TIMEOUT          0x303

/* System errors (0x401–0x40F) */
#define NS_ERR_PRIV_DROP        0x401
#define NS_ERR_THREAD           0x402

static inline const char *
ns_err_to_name (ns_err_t e)
{
    switch (e) {
        case NS_OK:                  return "NS_OK";
        case NS_FAIL:                return "NS_FAIL";
        case NS_ERR_NO_MEM:          return "NS_ERR_NO_MEM";
        case NS_ERR_INVALID_ARG:     return "NS_ERR_INVALID_ARG";
        case NS_ERR_INVALID_STATE:   return "NS_ERR_INVALID_STATE";
        case NS_ERR_BIND_FAILED:     return "NS_ERR_BIND_FAILED";
        case NS_ERR_LISTEN_FAILED:   return "NS_ERR_LISTEN_FAILED";
        case NS_ERR_CONN_FAILED:     return "NS_ERR_CONN_FAILED";
        case NS_ERR_CONN_LOST:       return "NS_ERR_CONN_LOST";
        case NS_ERR_MAX_CONNECTIONS: return "NS_ERR_MAX_CONNECTIONS";
        case NS_ERR_PROTO_INVALID:   return "NS_ERR_PROTO_INVALID";
        case NS_ERR_PROTO_OVERFLOW:  return "NS_ERR_PROTO_OVERFLOW";
        case NS_ERR_TIMEOUT:         return "NS_ERR_TIMEOUT";
        case NS_ERR_PRIV_DROP:       return "NS_ERR_PRIV_DROP";
        case NS_ERR_THREAD:          return "NS_ERR_THREAD";
        default:                     return "NS_ERR_UNKNOWN";
    }
}

/* Abort with full context — for must-succeed init calls. */
#define NS_ERROR_CHECK(x)                                               \
    do {                                                                \
        ns_err_t _rc = (x);                                             \
        if (_rc != NS_OK) {                                             \
            fprintf (stderr,                                            \
                     "\x1b[31m[ERROR]\x1b[0m %s:%d in %s()\n"          \
                     "  code: 0x%X  %s\n",                             \
                     __FILE__, __LINE__, __func__,                      \
                     (unsigned) _rc, ns_err_to_name (_rc));             \
            abort ();                                                   \
        }                                                               \
    } while (0)

/* Propagate error upward — always returns ns_err_t, nothing else. */
#define NS_RETURN_ON_ERROR(x)                                           \
    do {                                                                \
        ns_err_t _rc = (x);                                             \
        if (_rc != NS_OK) return _rc;                                   \
    } while (0)

/* Log and continue — for non-fatal situations. */
#define NS_LOG_ON_ERROR(x)                                              \
    do {                                                                \
        ns_err_t _rc = (x);                                             \
        if (_rc != NS_OK)                                               \
            fprintf (stderr, "\x1b[33m[WARN ]\x1b[0m %s:%d: %s\n",     \
                     __FILE__, __LINE__, ns_err_to_name (_rc));         \
    } while (0)

#endif /* NS_ERR_H */
