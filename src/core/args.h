#ifndef ARGS_H
#define ARGS_H

#include <stdint.h>
#include <stdbool.h>
#include "err.h"

typedef struct {
    uint16_t    port;        /*  -p / --port       default: 23    */
    uint32_t    max_conn;    /*  -n / --max-conn   default: 2000  */
    uint32_t    stack_kb;    /*  -s / --stack      default: 256   */
    const char *drop_user;   /*  -u / --user       default: NULL  */
    bool        foreground;  /*  -f / --foreground default: true  */
} server_args_t;

ns_err_t args_parse (int argc, char **argv, server_args_t *out);

#endif /* ARGS_H */
