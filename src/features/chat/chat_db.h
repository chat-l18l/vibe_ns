#ifndef CHAT_DB_H
#define CHAT_DB_H

/**
 * @file chat_db.h
 * @brief SQLite-backed storage for chat rooms, messages and presence.
 *
 * This is the only module in the project that knows SQL. Everything
 * above it (the chat service in room.c, the telnet UI, the HTTP API)
 * speaks in the plain-data records defined here — never in statements
 * or sqlite3 handles.
 *
 * Concurrency: one process-wide connection guarded by an internal
 * mutex. SQLite serializes writers anyway and chat volume is low, so a
 * single lock is the simplest correct choice; it can be upgraded to a
 * per-thread connection pool later without touching callers.
 *
 * Storage location is chosen by chat_db_default_path(): the database
 * lives at @c $STATE_DIRECTORY/chat.db under systemd, else ./data/chat.db.
 */

#include <stddef.h>

#define CHAT_NAME_MAX  32   /**< Room name buffer (incl. NUL). */
#define CHAT_USER_MAX  32   /**< Username buffer (incl. NUL). */
#define CHAT_BODY_MAX  512  /**< Message body buffer (incl. NUL). */

/** A room record. */
typedef struct {
    long id;
    char name[CHAT_NAME_MAX];
} chat_room_row_t;

/** A message record — the shared DTO between storage and all UIs. */
typedef struct {
    long id;                  /**< Monotonic message id (paging cursor). */
    char user[CHAT_USER_MAX];
    long ts;                  /**< Unix epoch seconds (UTC). */
    char body[CHAT_BODY_MAX];
} chat_msg_t;

/**
 * @brief Open/create the database, apply schema and seed the rooms.
 *
 * Sets WAL mode and a busy timeout. Idempotent schema (CREATE IF NOT
 * EXISTS); safe to call against an existing database.
 *
 * @param path  Database file path (see chat_db_default_path()).
 * @return 0 on success, -1 on failure (logged).
 */
int  chat_db_init (const char *path);

/** @brief Close the connection. Safe to call when never opened. */
void chat_db_close (void);

/**
 * @brief Fill @p out with the room list, ordered by id.
 * @return Number of rooms written (≤ @p max).
 */
int  chat_db_list_rooms (chat_room_row_t *out, int max);

/**
 * @brief Append a message to a room.
 * @return New message id (> 0) on success, -1 on failure.
 */
long chat_db_insert_message (long room_id, const char *user, const char *body);

/**
 * @brief Messages with id > @p after_id, ascending (oldest first).
 *
 * The live-tail / "what's new since I last looked" query, shared by the
 * telnet redraw and the HTTP @c since= parameter.
 * @return Number written (≤ @p max).
 */
int  chat_db_messages_since (long room_id, long after_id, int limit,
                             chat_msg_t *out, int max);

/**
 * @brief Up to @p limit newest messages with id < @p before_id,
 *        returned ascending. Pass @p before_id = 0 for the newest page.
 *
 * The history-paging query (telnet "older"). Returning ascending keeps
 * rendering uniform with chat_db_messages_since().
 * @return Number written (≤ @p max).
 */
int  chat_db_messages_before (long room_id, long before_id, int limit,
                              chat_msg_t *out, int max);

/** @brief Highest message id in a room, or 0 when empty. */
long chat_db_max_message_id (long room_id);

/** @brief Mark @p user present in @p room_id now (upsert last_seen). */
void chat_db_presence_set (long room_id, const char *user);

/** @brief Remove @p user from @p room_id's presence. */
void chat_db_presence_clear (long room_id, const char *user);

/**
 * @brief Distinct users seen in @p room_id within the last @p within_secs.
 * @param out  Array of name buffers to fill.
 * @return Number written (≤ @p max).
 */
int  chat_db_active_users (long room_id, int within_secs,
                           char out[][CHAT_USER_MAX], int max);

/**
 * @brief Compute the default database path.
 *
 * Writes @c $STATE_DIRECTORY/chat.db when that env var is set (systemd
 * StateDirectory), otherwise ./data/chat.db, creating the parent dir.
 * @return @p buf, or NULL if it did not fit.
 */
const char *chat_db_default_path (char *buf, size_t bufsz);

#endif /* CHAT_DB_H */
