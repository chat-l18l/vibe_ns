#ifndef CHAT_ROOM_H
#define CHAT_ROOM_H

/**
 * @file room.h
 * @brief Chat service: room registry, message API and live notification.
 *
 * The protocol-agnostic layer that both the telnet UI (chat.c) and the
 * HTTP API (http_chat.c) sit on. It owns the in-process publish/subscribe
 * bus and delegates all persistence to chat_db (the only SQL module).
 *
 * Delivery (publish/subscribe): every session in a room registers the
 * write end of its notify pipe via chat_room_subscribe(). chat_room_post()
 * persists the message and writes one byte to each subscriber, waking
 * their event loops. A woken subscriber then pulls new messages with
 * chat_room_since() using the last message id it saw as a cursor — so the
 * same mechanism serves telnet redraws and HTTP/SSE streams identically.
 *
 * Message records (::chat_msg_t) and field limits come from chat_db.h;
 * callers use them as plain data and never call chat_db directly.
 *
 * Thread-safety: all functions are safe to call from any session thread.
 */

#include "chat_db.h"

#define CHAT_MAX_ROOMS       8    /**< Registry capacity. */
#define CHAT_MAX_SUBSCRIBERS 256  /**< Max live subscribers per room. */

/**
 * Public handle for a room. @c id and @c name are readable; the
 * subscriber list is internal (the struct is embedded in a larger one).
 */
typedef struct {
    long id;
    char name[CHAT_NAME_MAX];
} chat_room_t;

/** @brief Open the database and load the room registry (idempotent). */
void          chat_service_init (void);

/**
 * @brief Get the room list.
 * @param count  Receives the room count (may be NULL).
 * @return Static array of room pointers; never NULL after init.
 */
chat_room_t **chat_rooms_list (int *count);

/** @brief Look up a room by id, or NULL if unknown. */
chat_room_t  *chat_room_by_id (long id);

/**
 * @brief Persist a message and wake every subscriber of the room.
 * @return New message id (> 0) on success, -1 on storage failure.
 */
long chat_room_post (chat_room_t *room, const char *user, const char *body);

/** @brief Register a notify fd (write end); see file overview. */
void chat_room_subscribe (chat_room_t *room, int notify_wfd);

/** @brief Remove a previously registered notify fd. */
void chat_room_unsubscribe (chat_room_t *room, int notify_wfd);

/**
 * @brief Messages newer than @p after_id (ascending). Live-tail cursor.
 * @return Number written (≤ @p max).
 */
int  chat_room_since (chat_room_t *room, long after_id, int limit,
                      chat_msg_t *out, int max);

/**
 * @brief Up to @p limit newest messages older than @p before_id
 *        (ascending; @p before_id = 0 means the newest page). History paging.
 * @return Number written (≤ @p max).
 */
int  chat_room_before (chat_room_t *room, long before_id, int limit,
                       chat_msg_t *out, int max);

/** @brief Highest message id in the room, or 0 when empty. */
long chat_room_tail (chat_room_t *room);

/** @brief Mark / unmark a user as present in the room. */
void chat_room_presence_set (chat_room_t *room, const char *user);
void chat_room_presence_clear (chat_room_t *room, const char *user);

/**
 * @brief Active users in the room (present within the idle window).
 * @return Number written (≤ @p max).
 */
int  chat_room_users (chat_room_t *room, char out[][CHAT_USER_MAX], int max);

#endif /* CHAT_ROOM_H */
