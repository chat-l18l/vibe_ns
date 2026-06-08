#include "room.h"
#include "../../core/log.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <pthread.h>
#include <time.h>
#include <assert.h>

/* -------------------------------------------------------------------------
 * Room storage
 * ---------------------------------------------------------------------- */

typedef struct {
    chat_room_t  pub;         /* must be first — callers cast pub* to room_t* */
    pthread_mutex_t lock;
} room_t;

static room_t       s_rooms[CHAT_MAX_ROOMS];
static int          s_room_count;
static chat_room_t *s_room_ptrs[CHAT_MAX_ROOMS];
static pthread_once_t s_once = PTHREAD_ONCE_INIT;

static const char *s_room_names[] = { "general", "games", "random" };

static void
rooms_init_once (void)
{
    /* create data directory if needed */
    mkdir (CHAT_DATA_DIR, 0755);

    s_room_count = (int) (sizeof (s_room_names) / sizeof (s_room_names[0]));
    for (int i = 0; i < s_room_count; i++) {
        room_t *r = &s_rooms[i];
        memset (r, 0, sizeof (*r));
        pthread_mutex_init (&r->lock, NULL);
        strncpy (r->pub.name, s_room_names[i], sizeof (r->pub.name) - 1);
        snprintf (r->pub.log_path, sizeof (r->pub.log_path),
                  CHAT_DATA_DIR "/%s.log", s_room_names[i]);
        s_room_ptrs[i] = &r->pub;
    }
}

void
chat_rooms_ensure_init (void)
{
    pthread_once (&s_once, rooms_init_once);
}

chat_room_t **
chat_rooms_list (int *count)
{
    chat_rooms_ensure_init ();
    if (count) *count = s_room_count;
    return s_room_ptrs;
}

/* -------------------------------------------------------------------------
 * Post a message
 * ---------------------------------------------------------------------- */

int
chat_room_post (chat_room_t *pub, const char *username, const char *msg)
{
    assert (pub != NULL);
    assert (username != NULL);
    assert (msg != NULL);

    room_t *r = (room_t *) pub;

    /* format ISO-8601 timestamp */
    time_t    now = time (NULL);
    struct tm tm;
    gmtime_r (&now, &tm);
    char ts[32];
    strftime (ts, sizeof (ts), "%Y-%m-%dT%H:%M:%S", &tm);

    /* write log line */
    int fd = open (pub->log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
        LOG_ERR ("chat_room_post: open %s: %s", pub->log_path, strerror (errno));
        return -1;
    }
    flock (fd, LOCK_EX);
    dprintf (fd, "%s:%s:%s:%s\n", pub->name, ts, username, msg);
    flock (fd, LOCK_UN);
    close (fd);

    /* notify subscribers */
    pthread_mutex_lock (&r->lock);
    for (int i = 0; i < pub->sub_count; i++) {
        uint8_t one = 1;
        write (pub->sub_fds[i], &one, 1);   /* best-effort; ignore error */
    }
    pthread_mutex_unlock (&r->lock);
    return 0;
}

/* -------------------------------------------------------------------------
 * Subscriber management
 * ---------------------------------------------------------------------- */

void
chat_room_subscribe (chat_room_t *pub, int notify_wfd)
{
    assert (pub != NULL);
    room_t *r = (room_t *) pub;
    pthread_mutex_lock (&r->lock);
    if (pub->sub_count < CHAT_MAX_SUBSCRIBERS)
        pub->sub_fds[pub->sub_count++] = notify_wfd;
    pthread_mutex_unlock (&r->lock);
}

void
chat_room_unsubscribe (chat_room_t *pub, int notify_wfd)
{
    assert (pub != NULL);
    room_t *r = (room_t *) pub;
    pthread_mutex_lock (&r->lock);
    for (int i = 0; i < pub->sub_count; i++) {
        if (pub->sub_fds[i] == notify_wfd) {
            pub->sub_fds[i] = pub->sub_fds[--pub->sub_count];
            break;
        }
    }
    pthread_mutex_unlock (&r->lock);
}

/* -------------------------------------------------------------------------
 * History read — scan backwards for max_lines lines before before_off
 * ---------------------------------------------------------------------- */

int
chat_room_read_history (chat_room_t *pub, off_t before_off,
                        int max_lines, char *buf, size_t bufsz,
                        off_t *new_off)
{
    assert (pub != NULL);
    assert (buf != NULL);
    assert (new_off != NULL);

    int fd = open (pub->log_path, O_RDONLY);
    if (fd < 0) {
        *new_off = 0;
        buf[0]   = '\0';
        return 0;
    }

    off_t file_size = lseek (fd, 0, SEEK_END);
    if (file_size <= 0) {
        close (fd);
        *new_off = 0;
        buf[0]   = '\0';
        return 0;
    }

    if (before_off == 0 || before_off > file_size)
        before_off = file_size;

    /* scan backwards byte by byte to find line boundaries */
    off_t scan = before_off;
    int   lines_found = 0;

    /* skip any trailing newline at scan position */
    if (scan > 0) scan--;

    while (scan > 0 && lines_found < max_lines) {
        lseek (fd, scan - 1, SEEK_SET);
        uint8_t c;
        if (read (fd, &c, 1) != 1) break;
        if (c == '\n') lines_found++;
        scan--;
    }
    /* scan now points to start of oldest line we want */
    off_t read_from = scan;

    /* read the lines into a temp buffer */
    off_t read_len = before_off - read_from;
    if (read_len <= 0) {
        close (fd);
        *new_off = before_off;
        buf[0]   = '\0';
        return 0;
    }

    char *tmp = malloc ((size_t) read_len + 1);
    if (!tmp) {
        close (fd);
        *new_off = before_off;
        buf[0]   = '\0';
        return 0;
    }

    lseek (fd, read_from, SEEK_SET);
    ssize_t got = read (fd, tmp, (size_t) read_len);
    close (fd);
    if (got <= 0) {
        free (tmp);
        *new_off = before_off;
        buf[0]   = '\0';
        return 0;
    }
    tmp[got] = '\0';

    /* format each line: "room:ts:user:msg\n" → "[HH:MM] user: msg\r\n" */
    size_t pos     = 0;
    int    count   = 0;
    char  *line    = tmp;
    char  *end     = tmp + got;

    while (line < end) {
        char *nl = memchr (line, '\n', (size_t) (end - line));
        size_t line_len = nl ? (size_t) (nl - line) : (size_t) (end - line);
        if (line_len == 0) { line++; continue; }

        /* parse room:ts:user:msg — split on ':' */
        char copy[768];
        size_t copy_len = line_len < sizeof (copy) - 1 ? line_len : sizeof (copy) - 1;
        memcpy (copy, line, copy_len);
        copy[copy_len] = '\0';

        char *f1 = copy;
        char *f2 = strchr (f1, ':');         /* after room */
        char *f3 = f2 ? strchr (f2 + 1, ':') : NULL;  /* after ts */
        char *f4 = f3 ? strchr (f3 + 1, ':') : NULL;  /* after user */

        if (f2 && f3 && f4) {
            *f2 = *f3 = *f4 = '\0';
            /* f1=room, f2+1=ts, f3+1=user, f4+1=msg */
            const char *ts   = f2 + 1;
            const char *user = f3 + 1;
            const char *msg  = f4 + 1;
            /* shorten ts to HH:MM (chars 11-15 of ISO-8601) */
            const char *hhmm = (strlen (ts) >= 16) ? ts + 11 : ts;
            char hhmm5[6];
            strncpy (hhmm5, hhmm, 5);
            hhmm5[5] = '\0';

            int n = snprintf (buf + pos, bufsz - pos,
                              "\x1b[2m[%s]\x1b[0m \x1b[1m%s\x1b[0m: %s\r\n",
                              hhmm5, user, msg);
            if (n > 0 && pos + (size_t) n < bufsz) {
                pos += (size_t) n;
                count++;
            }
        }

        line = nl ? nl + 1 : end;
    }

    free (tmp);
    *new_off = before_off;
    return count;
}

/* -------------------------------------------------------------------------
 * Read new lines appended since from_off
 * ---------------------------------------------------------------------- */

ssize_t
chat_room_read_since (chat_room_t *pub, off_t from_off,
                      char *buf, size_t bufsz, off_t *new_off)
{
    assert (pub != NULL);
    assert (buf != NULL);
    assert (new_off != NULL);

    int fd = open (pub->log_path, O_RDONLY);
    if (fd < 0) {
        *new_off = from_off;
        return 0;
    }

    lseek (fd, from_off, SEEK_SET);

    /* read raw new bytes */
    char *raw  = malloc (bufsz);
    if (!raw) { close (fd); *new_off = from_off; return 0; }

    ssize_t got = read (fd, raw, bufsz - 1);
    off_t   end_off = (got > 0) ? from_off + got : from_off;
    close (fd);

    if (got <= 0) {
        free (raw);
        *new_off = from_off;
        return 0;
    }
    raw[got] = '\0';

    /* reformat lines same as read_history */
    size_t pos  = 0;
    char  *line = raw;
    char  *end  = raw + got;

    while (line < end) {
        char *nl       = memchr (line, '\n', (size_t) (end - line));
        size_t line_len = nl ? (size_t) (nl - line) : (size_t) (end - line);
        if (line_len == 0) { line++; continue; }

        char copy[768];
        size_t copy_len = line_len < sizeof (copy) - 1 ? line_len : sizeof (copy) - 1;
        memcpy (copy, line, copy_len);
        copy[copy_len] = '\0';

        char *f1 = copy;
        char *f2 = strchr (f1, ':');
        char *f3 = f2 ? strchr (f2 + 1, ':') : NULL;
        char *f4 = f3 ? strchr (f3 + 1, ':') : NULL;

        if (f2 && f3 && f4) {
            *f2 = *f3 = *f4 = '\0';
            const char *ts   = f2 + 1;
            const char *user = f3 + 1;
            const char *msg  = f4 + 1;
            const char *hhmm = (strlen (ts) >= 16) ? ts + 11 : ts;
            char hhmm5[6];
            strncpy (hhmm5, hhmm, 5);
            hhmm5[5] = '\0';

            int n = snprintf (buf + pos, bufsz - pos,
                              "\x1b[2m[%s]\x1b[0m \x1b[1m%s\x1b[0m: %s\r\n",
                              hhmm5, user, msg);
            if (n > 0 && pos + (size_t) n < bufsz)
                pos += (size_t) n;
        }

        line = nl ? nl + 1 : end;
    }

    free (raw);
    *new_off = end_off;
    return (ssize_t) pos;
}
