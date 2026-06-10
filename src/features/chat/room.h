#ifndef CHAT_ROOM_H
#define CHAT_ROOM_H

/**
 * @file room.h
 * @brief Chat room registry: persistent history + cross-session delivery.
 *
 * Each room is backed by an append-only log file, one line per message:
 * @code
 *   room:ISO-8601-timestamp:username:message\n
 * @endcode
 * The file is the single source of truth — there is no in-memory message
 * buffer. Readers re-read the file; this keeps history, live view and
 * multi-process access (flock) consistent by construction.
 *
 * Delivery uses the publish/subscribe pattern: every session in a room
 * registers the write end of its notify pipe. chat_room_post() appends
 * to the log and writes one byte to each subscriber, waking their
 * select() loops (see telnet_session.h, @c on_notify).
 *
 * Storage location: @c $STATE_DIRECTORY/chat when run under systemd
 * (StateDirectory=netserver), otherwise ./data/chat for dev runs.
 *
 * Thread-safety: all functions are safe to call from any session thread.
 */

#include <sys/types.h>

#define CHAT_MAX_ROOMS       8        /**< Registry capacity. */
#define CHAT_MAX_SUBSCRIBERS 256      /**< Max sessions per room. */
#define CHAT_DATA_DIR        "data/chat"  /**< Dev fallback log directory. */

/**
 * Public view of a room. Embedded as first member of an internal struct
 * that adds a mutex — treat instances as opaque and use the API only.
 */
typedef struct {
    char             name[32];                      /**< Room name, no '#'. */
    char             log_path[512];                 /**< Log file path. */
    int              sub_fds[CHAT_MAX_SUBSCRIBERS]; /**< Subscriber notify fds. */
    int              sub_count;
    /* mutex is embedded; opaque to callers — use API only */
} chat_room_t;

/** @brief Initialize the registry (idempotent, thread-safe via pthread_once). */
void          chat_rooms_ensure_init(void);

/**
 * @brief Get the room list.
 * @param count  Receives the number of rooms (may be NULL).
 * @return Static array of room pointers; never NULL after init.
 */
chat_room_t **chat_rooms_list(int *count);

/**
 * @brief Append a message to the room log and wake all subscribers.
 * @param r         Target room.
 * @param username  Sender (validated charset: [A-Za-z0-9_]).
 * @param msg       Message text (single line, no '\\n').
 * @return 0 on success, -1 if the log file could not be written.
 */
int     chat_room_post(chat_room_t *r, const char *username, const char *msg);

/**
 * @brief Register a notify fd; chat_room_post() writes 1 byte to it per message.
 */
void    chat_room_subscribe(chat_room_t *r, int notify_wfd);

/** @brief Remove a previously subscribed notify fd. */
void    chat_room_unsubscribe(chat_room_t *r, int notify_wfd);

/**
 * @brief Read up to @p max_lines history lines ending at @p before_off.
 *
 * Lines are returned formatted for display ("[HH:MM] user: msg\\r\\n",
 * with ANSI styling). Pass @c before_off = 0 for "newest messages".
 *
 * @param r           Room.
 * @param before_off  File offset to read up to (0 = end of file).
 * @param max_lines   Max lines to return.
 * @param buf         Output buffer (NUL-terminated formatted text).
 * @param bufsz       Output buffer size.
 * @param new_off     Receives the effective end offset — pass back in as
 *                    @p before_off to page further into the past.
 * @return Number of lines written into @p buf.
 */
int     chat_room_read_history(chat_room_t *r, off_t before_off,
                               int max_lines, char *buf, size_t bufsz,
                               off_t *new_off);

/**
 * @brief Read messages appended since @p from_off, formatted for display.
 * @param new_off  Receives the new end-of-file offset.
 * @return Bytes written into @p buf (0 when nothing new).
 */
ssize_t chat_room_read_since(chat_room_t *r, off_t from_off,
                             char *buf, size_t bufsz, off_t *new_off);

#endif /* CHAT_ROOM_H */
