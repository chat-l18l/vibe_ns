#include "priv.h"
#include "log.h"

#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <assert.h>

ns_err_t
drop_privileges (const char *username)
{
    assert (username != NULL);

    struct passwd *pw = getpwnam (username);
    if (!pw) {
        LOG_ERR ("drop_privileges: user not found: %s", username);
        return NS_ERR_PRIV_DROP;
    }

    if (initgroups (username, pw->pw_gid) != 0 ||
        setgid (pw->pw_gid) != 0              ||
        setuid (pw->pw_uid) != 0) {
        LOG_ERR ("drop_privileges: setuid/setgid failed: %m");
        return NS_ERR_PRIV_DROP;
    }

    if (getuid () == 0) {
        LOG_ERR ("drop_privileges: still root after drop — refusing to continue");
        return NS_ERR_PRIV_DROP;
    }

    LOG_INFO ("privileges dropped to %s (uid=%d gid=%d)",
              username, (int) pw->pw_uid, (int) pw->pw_gid);
    return NS_OK;
}
