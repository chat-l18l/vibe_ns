#ifndef PRIV_H
#define PRIV_H

#include "err.h"

/*  Drop process privileges to `username` after binding privileged ports.
 *  Call AFTER bind(), BEFORE server_run().
 *  Returns NS_OK on success, NS_ERR_PRIV_DROP on failure.               */
ns_err_t drop_privileges (const char *username);

#endif /* PRIV_H */
