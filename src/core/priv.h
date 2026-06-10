#ifndef PRIV_H
#define PRIV_H

/**
 * @file priv.h
 * @brief Privilege dropping for binding low ports as root.
 */

#include "err.h"

/**
 * @brief Drop process privileges to @p username.
 *
 * Order matters: call AFTER bind() (the privileged operation) and
 * BEFORE server_run() (untrusted input). Sets supplementary groups,
 * gid, then uid, and verifies the process is no longer root.
 *
 * @param username  Target user (e.g. "nobody").
 * @return ::NS_OK on success, ::NS_ERR_PRIV_DROP on failure — the caller
 *         must treat failure as fatal and refuse to serve.
 */
ns_err_t drop_privileges (const char *username);

#endif /* PRIV_H */
