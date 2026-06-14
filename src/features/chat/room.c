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
 * Post + notify
 * ---------------------------------------------------------------------- */

long
chat_room_post (chat_room_t *pub, const char *user, const char *body)
{
    assert (pub != NULL);
    assert (user != NULL);
    assert (body != NULL);

    long id = chat_db_insert_message (pub->id, user, body);
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
    if (user && user[0])
        chat_db_presence_set (pub->id, user);
}

void
chat_room_presence_clear (chat_room_t *pub, const char *user)
{
    assert (pub != NULL);
    if (user && user[0])
        chat_db_presence_clear (pub->id, user);
}

int
chat_room_users (chat_room_t *pub, char out[][CHAT_USER_MAX], int max)
{
    assert (pub != NULL);
    return chat_db_active_users (pub->id, CHAT_PRESENCE_TTL, out, max);
}
