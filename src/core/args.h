#ifndef ARGS_H
#define ARGS_H

/**
 * @file args.h
 * @brief Command-line argument parsing (GNU argp).
 */

#include <stdint.h>
#include <stdbool.h>
#include "err.h"

/** Parsed command-line options with their defaults. */
typedef struct {
    uint16_t    port;        /**< -p / --port       default: 23    */
    uint32_t    max_conn;    /**< -n / --max-conn   default: 2000  */
    uint32_t    stack_kb;    /**< -s / --stack      default: 256   */
    const char *drop_user;   /**< -u / --user       default: NULL (no drop) */
    bool        foreground;  /**< -f / --foreground default: true  */
} server_args_t;

/**
 * @brief Parse argv into @p out, applying defaults first.
 *
 * Invalid values terminate the process via argp_error() with a usage
 * message (standard argp behavior).
 *
 * @return ::NS_OK or ::NS_ERR_INVALID_ARG.
 */
ns_err_t args_parse (int argc, char **argv, server_args_t *out);

#endif /* ARGS_H */
