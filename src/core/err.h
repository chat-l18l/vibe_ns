#ifndef NS_ERR_H
#define NS_ERR_H

/**
 * @file err.h
 * @brief Error-code domain and propagation macros (ESP-IDF style).
 *
 * Every fallible public function in the server returns ::ns_err_t.
 * Codes are grouped per subsystem in disjoint hex ranges so a raw code
 * in a log immediately identifies the failing layer.
 *
 * Three propagation strategies are provided:
 *  - ::NS_ERROR_CHECK     — abort on failure (must-succeed init paths)
 *  - ::NS_RETURN_ON_ERROR — bubble the error up to the caller
 *  - ::NS_LOG_ON_ERROR    — log and continue (non-fatal)
 */

#include <stdio.h>
#include <stdlib.h>

/** Error/status code returned by all fallible netserver functions. */
typedef int ns_err_t;

#define NS_OK    0   /**< Success. */
#define NS_FAIL -1   /**< Generic, unspecified failure. */

/** @name Memory / argument errors (0x101–0x11F) */
/**@{*/
#define NS_ERR_NO_MEM           0x101  /**< Allocation failed. */
#define NS_ERR_INVALID_ARG      0x110  /**< Caller passed an invalid argument. */
#define NS_ERR_INVALID_STATE    0x111  /**< Operation not valid in current state. */
/**@}*/

/** @name Network / server errors (0x201–0x20F) */
/**@{*/
#define NS_ERR_BIND_FAILED      0x201  /**< socket()/bind() failed. */
#define NS_ERR_LISTEN_FAILED    0x202  /**< listen() failed. */
#define NS_ERR_CONN_FAILED      0x203  /**< Connection setup failed. */
#define NS_ERR_CONN_LOST        0x204  /**< Peer disconnected unexpectedly. */
#define NS_ERR_MAX_CONNECTIONS  0x205  /**< All connection slots occupied. */
/**@}*/

/** @name Protocol errors (0x301–0x30F) */
/**@{*/
#define NS_ERR_PROTO_INVALID    0x301  /**< Malformed protocol input. */
#define NS_ERR_PROTO_OVERFLOW   0x302  /**< Protocol buffer would overflow. */
#define NS_ERR_TIMEOUT          0x303  /**< Operation timed out. */
/**@}*/

/** @name System errors (0x401–0x40F) */
/**@{*/
#define NS_ERR_PRIV_DROP        0x401  /**< setuid/setgid privilege drop failed. */
#define NS_ERR_THREAD           0x402  /**< Thread or semaphore primitive failed. */
/**@}*/

/**
 * @brief Map an error code to its symbolic name for logging.
 * @param e  Error code.
 * @return Static string such as "NS_ERR_NO_MEM"; never NULL.
 */
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

/**
 * @brief Abort with full context — for must-succeed init calls.
 *
 * Evaluates @p x once; on any code other than ::NS_OK prints file, line,
 * function and the symbolic error name, then calls abort().
 */
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

/**
 * @brief Propagate an error to the caller.
 *
 * Evaluates @p x once; returns its value from the enclosing function if
 * it is not ::NS_OK. The enclosing function must return ::ns_err_t.
 */
#define NS_RETURN_ON_ERROR(x)                                           \
    do {                                                                \
        ns_err_t _rc = (x);                                             \
        if (_rc != NS_OK) return _rc;                                   \
    } while (0)

/**
 * @brief Log a warning and continue — for non-fatal situations.
 */
#define NS_LOG_ON_ERROR(x)                                              \
    do {                                                                \
        ns_err_t _rc = (x);                                             \
        if (_rc != NS_OK)                                               \
            fprintf (stderr, "\x1b[33m[WARN ]\x1b[0m %s:%d: %s\n",     \
                     __FILE__, __LINE__, ns_err_to_name (_rc));         \
    } while (0)

#endif /* NS_ERR_H */
