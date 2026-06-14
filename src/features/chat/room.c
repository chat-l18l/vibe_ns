/**
 * @file room.c
 * @brief Chat service: registry, message API and subscriber wake-up.
 *
 * Persistence is delegated to chat_db; this layer adds the in-process
 * publish/subscribe bus (a per-room list of notify fds) and the room
 * handle registry loaded once at startup.
 */

#include "room.h"
#include "../../core/log.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>

/** Users counted as "active" if seen within this many seconds. Matches
 *  the telnet idle timeout, so disconnected users drop off naturally. */
#define CHAT_PRESENCE_TTL 300

/* Internal room: public handle + subscriber list, guarded by its lock. */
typedef struct {
    chat_room_t     pub;          /* must be first — we cast pub* to room_t* */
    pthread_mutex_t lock;
    int             sub_fds[CHAT_MAX_SUBSCRIBERS];
    int             sub_count;
} room_t;

static room_t         s_rooms[CHAT_MAX_ROOMS];
static int            s_room_count;
static chat_room_t   *s_room_ptrs[CHAT_MAX_ROOMS];
static pthread_once_t s_once = PTHREAD_ONCE_INIT;

static void
service_init_once (void)
{
    char path[512];
    if (!chat_db_default_path (path, sizeof (path)) ||
        chat_db_init (path) != 0) {
        LOG_ERR ("chat: storage init failed — chat will not work");
        return;
    }

    chat_room_row_t rows[CHAT_MAX_ROOMS];
    s_room_count = chat_db_list_rooms (rows, CHAT_MAX_ROOMS);
    for (int i = 0; i < s_room_count; i++) {
        room_t *r = &s_rooms[i];
        memset (r, 0, sizeof (*r));
        pthread_mutex_init (&r->lock, NULL);
        r->pub.id = rows[i].id;
        snprintf (r->pub.name, sizeof (r->pub.name), "%s", rows[i].name);
        s_room_ptrs[i] = &r->pub;
    }
}

void
chat_service_init (void)
{
    pthread_once (&s_once, service_init_once);
}

chat_room_t **
chat_rooms_list (int *count)
{
    chat_service_init ();
    if (count) *count = s_room_count;
    return s_room_ptrs;
}

chat_room_t *
chat_room_by_id (long id)
{
    chat_service_init ();
    for (int i = 0; i < s_room_count; i++)
        if (s_room_ptrs[i]->id == id)
            return s_room_ptrs[i];
    return NULL;
}

/* -------------------------------------------------------------------------
 * Input sanitization
 *
 * Bodies and usernames enter from two protocols (telnet and HTTP). Both
 * are stored once and later rendered onto *telnet* terminals, so anything
 * outside printable ASCII could carry an escape-sequence injection. We
 * normalize at this single choke point — the service post — rather than
 * trusting each protocol's input path.
 * ---------------------------------------------------------------------- */

/* Copy src→dst keeping only printable ASCII (0x20–0x7E); others become
 * '?'. Returns the number of kept characters (excluding NUL). */
static size_t
sanitize_text (const char *src, char *dst, size_t dstsz)
{
    size_t n = 0;
    for (; src[n] && n < dstsz - 1; n++) {
        unsigned char ch = (unsigned char) src[n];
        dst[n] = (ch >= 0x20 && ch <= 0x7E) ? (char) ch : '?';
    }
    dst[n] = '\0';
    return n;
}

/* Keep only [A-Za-z0-9_] from a username. Returns kept length. */
static size_t
sanitize_user (const char *src, char *dst, size_t dstsz)
{
    size_t n = 0;
    for (size_t i = 0; src[i] && n < dstsz - 1; i++) {
        char ch = src[i];
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '_')
            dst[n++] = ch;
    }
    dst[n] = '\0';
    return n;
}

/* -------------------------------------------------------------------------
 * Post + notify
 * ---------------------------------------------------------------------- */

long
chat_room_post (chat_room_t *pub, const char *user, const char *body)
{
    assert (pub != NULL);
    assert (user != NULL);
    assert (body != NULL);

    char clean_user[CHAT_USER_MAX];
    char clean_body[CHAT_BODY_MAX];
    if (sanitize_user (user, clean_user, sizeof (clean_user)) == 0)
        return -1;                               /* empty/invalid username */
    if (sanitize_text (body, clean_body, sizeof (clean_body)) == 0)
        return -1;                               /* empty message */

    long id = chat_db_insert_message (pub->id, clean_user, clean_body);
    if (id < 0)
        return -1;

    room_t *r = (room_t *) pub;
    pthread_mutex_lock (&r->lock);
    for (int i = 0; i < r->sub_count; i++) {
        uint8_t one = 1;
        if (write (r->sub_fds[i], &one, 1) < 0) {
            /* non-blocking pipe full: a wake-up is already pending, the
             * subscriber will pull everything new on its next redraw */
        }
    }
    pthread_mutex_unlock (&r->lock);
    return id;
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
    if (r->sub_count < CHAT_MAX_SUBSCRIBERS)
        r->sub_fds[r->sub_count++] = notify_wfd;
    pthread_mutex_unlock (&r->lock);
}

void
chat_room_unsubscribe (chat_room_t *pub, int notify_wfd)
{
    assert (pub != NULL);
    room_t *r = (room_t *) pub;
    pthread_mutex_lock (&r->lock);
    for (int i = 0; i < r->sub_count; i++) {
        if (r->sub_fds[i] == notify_wfd) {
            r->sub_fds[i] = r->sub_fds[--r->sub_count];
            break;
        }
    }
    pthread_mutex_unlock (&r->lock);
}

/* -------------------------------------------------------------------------
 * Reads + presence — thin pass-through to chat_db, keyed by room id
 * ---------------------------------------------------------------------- */

int
chat_room_since (chat_room_t *pub, long after_id, int limit,
                 chat_msg_t *out, int max)
{
    assert (pub != NULL);
    return chat_db_messages_since (pub->id, after_id, limit, out, max);
}

int
chat_room_before (chat_room_t *pub, long before_id, int limit,
                  chat_msg_t *out, int max)
{
    assert (pub != NULL);
    return chat_db_messages_before (pub->id, before_id, limit, out, max);
}

long
chat_room_tail (chat_room_t *pub)
{
    assert (pub != NULL);
    return chat_db_max_message_id (pub->id);
}

void
chat_room_presence_set (chat_room_t *pub, const char *user)
{
    assert (pub != NULL);
    char clean[CHAT_USER_MAX];
    if (user && sanitize_user (user, clean, sizeof (clean)) > 0)
        chat_db_presence_set (pub->id, clean);
}

void
chat_room_presence_clear (chat_room_t *pub, const char *user)
{
    assert (pub != NULL);
    char clean[CHAT_USER_MAX];
    if (user && sanitize_user (user, clean, sizeof (clean)) > 0)
        chat_db_presence_clear (pub->id, clean);
}

int
chat_room_users (chat_room_t *pub, char out[][CHAT_USER_MAX], int max)
{
    assert (pub != NULL);
    return chat_db_active_users (pub->id, CHAT_PRESENCE_TTL, out, max);
}
