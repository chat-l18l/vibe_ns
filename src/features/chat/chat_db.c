/**
 * @file chat_db.c
 * @brief SQLite storage layer — schema, queries and presence.
 *
 * One process-wide sqlite3 handle behind a single mutex. Statements are
 * prepared per call (chat volume is low; this avoids cached-statement
 * lifecycle complexity). All public functions lock, so callers need no
 * synchronization of their own.
 */

#include "chat_db.h"
#include "../../core/log.h"
#include "../../../third_party/sqlite3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>

static sqlite3        *s_db;
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;

static const char *const s_seed_rooms[] = { "general", "games", "random" };

static const char SCHEMA[] =
    "PRAGMA journal_mode=WAL;"
    "PRAGMA foreign_keys=ON;"
    "CREATE TABLE IF NOT EXISTS rooms ("
    "  id   INTEGER PRIMARY KEY,"
    "  name TEXT UNIQUE NOT NULL);"
    "CREATE TABLE IF NOT EXISTS messages ("
    "  id      INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  room_id INTEGER NOT NULL REFERENCES rooms(id),"
    "  user    TEXT NOT NULL,"
    "  ts      INTEGER NOT NULL,"
    "  body    TEXT NOT NULL);"
    "CREATE INDEX IF NOT EXISTS idx_messages_room ON messages(room_id, id);"
    "CREATE TABLE IF NOT EXISTS presence ("
    "  room_id   INTEGER NOT NULL REFERENCES rooms(id),"
    "  user      TEXT NOT NULL,"
    "  last_seen INTEGER NOT NULL,"
    "  PRIMARY KEY (room_id, user));";

/* -------------------------------------------------------------------------
 * Init / teardown
 * ---------------------------------------------------------------------- */

const char *
chat_db_default_path (char *buf, size_t bufsz)
{
    const char *state = getenv ("STATE_DIRECTORY");
    int n;
    if (state && state[0]) {
        size_t len = strcspn (state, ":");      /* may be colon-separated */
        n = snprintf (buf, bufsz, "%.*s/chat.db", (int) len, state);
    } else {
        mkdir ("data", 0755);
        n = snprintf (buf, bufsz, "data/chat.db");
    }
    if (n < 0 || (size_t) n >= bufsz)
        return NULL;
    return buf;
}

int
chat_db_init (const char *path)
{
    pthread_mutex_lock (&s_lock);
    if (s_db) {                                  /* already open — idempotent */
        pthread_mutex_unlock (&s_lock);
        return 0;
    }

    if (sqlite3_open (path, &s_db) != SQLITE_OK) {
        LOG_ERR ("chat_db: open %s failed: %s", path, sqlite3_errmsg (s_db));
        sqlite3_close (s_db);
        s_db = NULL;
        pthread_mutex_unlock (&s_lock);
        return -1;
    }

    sqlite3_busy_timeout (s_db, 5000);

    char *err = NULL;
    if (sqlite3_exec (s_db, SCHEMA, NULL, NULL, &err) != SQLITE_OK) {
        LOG_ERR ("chat_db: schema failed: %s", err ? err : "?");
        sqlite3_free (err);
        sqlite3_close (s_db);
        s_db = NULL;
        pthread_mutex_unlock (&s_lock);
        return -1;
    }

    /* Seed the default rooms (no-op if they already exist). */
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2 (s_db, "INSERT OR IGNORE INTO rooms(name) VALUES(?1);",
                            -1, &st, NULL) == SQLITE_OK) {
        for (size_t i = 0; i < sizeof (s_seed_rooms) / sizeof (s_seed_rooms[0]); i++) {
            sqlite3_bind_text (st, 1, s_seed_rooms[i], -1, SQLITE_STATIC);
            sqlite3_step (st);
            sqlite3_reset (st);
        }
        sqlite3_finalize (st);
    }

    LOG_INFO ("chat_db: ready at %s", path);
    pthread_mutex_unlock (&s_lock);
    return 0;
}

void
chat_db_close (void)
{
    pthread_mutex_lock (&s_lock);
    if (s_db) {
        sqlite3_close (s_db);
        s_db = NULL;
    }
    pthread_mutex_unlock (&s_lock);
}

/* -------------------------------------------------------------------------
 * Rooms
 * ---------------------------------------------------------------------- */

int
chat_db_list_rooms (chat_room_row_t *out, int max)
{
    int count = 0;
    pthread_mutex_lock (&s_lock);
    sqlite3_stmt *st = NULL;
    if (s_db && sqlite3_prepare_v2 (s_db,
            "SELECT id, name FROM rooms ORDER BY id;", -1, &st, NULL) == SQLITE_OK) {
        while (count < max && sqlite3_step (st) == SQLITE_ROW) {
            out[count].id = (long) sqlite3_column_int64 (st, 0);
            snprintf (out[count].name, sizeof (out[count].name), "%s",
                      (const char *) sqlite3_column_text (st, 1));
            count++;
        }
        sqlite3_finalize (st);
    }
    pthread_mutex_unlock (&s_lock);
    return count;
}

/* -------------------------------------------------------------------------
 * Messages
 * ---------------------------------------------------------------------- */

long
chat_db_insert_message (long room_id, const char *user, const char *body)
{
    long id = -1;
    pthread_mutex_lock (&s_lock);
    sqlite3_stmt *st = NULL;
    if (s_db && sqlite3_prepare_v2 (s_db,
            "INSERT INTO messages(room_id, user, ts, body) VALUES(?1,?2,?3,?4);",
            -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int64 (st, 1, room_id);
        sqlite3_bind_text  (st, 2, user, -1, SQLITE_STATIC);
        sqlite3_bind_int64 (st, 3, (sqlite3_int64) time (NULL));
        sqlite3_bind_text  (st, 4, body, -1, SQLITE_STATIC);
        if (sqlite3_step (st) == SQLITE_DONE)
            id = (long) sqlite3_last_insert_rowid (s_db);
        else
            LOG_ERR ("chat_db: insert failed: %s", sqlite3_errmsg (s_db));
        sqlite3_finalize (st);
    }
    pthread_mutex_unlock (&s_lock);
    return id;
}

/** @brief Fill a ::chat_msg_t from a row of (id, user, ts, body). */
static void
row_to_msg (sqlite3_stmt *st, chat_msg_t *m)
{
    m->id = (long) sqlite3_column_int64 (st, 0);
    snprintf (m->user, sizeof (m->user), "%s",
              (const char *) sqlite3_column_text (st, 1));
    m->ts = (long) sqlite3_column_int64 (st, 2);
    snprintf (m->body, sizeof (m->body), "%s",
              (const char *) sqlite3_column_text (st, 3));
}

int
chat_db_messages_since (long room_id, long after_id, int limit,
                        chat_msg_t *out, int max)
{
    if (limit > max) limit = max;
    int count = 0;
    pthread_mutex_lock (&s_lock);
    sqlite3_stmt *st = NULL;
    if (s_db && sqlite3_prepare_v2 (s_db,
            "SELECT id, user, ts, body FROM messages "
            "WHERE room_id=?1 AND id>?2 ORDER BY id ASC LIMIT ?3;",
            -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int64 (st, 1, room_id);
        sqlite3_bind_int64 (st, 2, after_id);
        sqlite3_bind_int   (st, 3, limit);
        while (count < max && sqlite3_step (st) == SQLITE_ROW)
            row_to_msg (st, &out[count++]);
        sqlite3_finalize (st);
    }
    pthread_mutex_unlock (&s_lock);
    return count;
}

int
chat_db_messages_before (long room_id, long before_id, int limit,
                         chat_msg_t *out, int max)
{
    if (limit > max) limit = max;
    if (before_id <= 0) before_id = (long) 0x7fffffffffffffffLL;
    int count = 0;
    pthread_mutex_lock (&s_lock);
    sqlite3_stmt *st = NULL;
    /* Pick the newest `limit` rows below before_id (DESC), then flip to
     * ascending so callers always render oldest-first. */
    if (s_db && sqlite3_prepare_v2 (s_db,
            "SELECT id, user, ts, body FROM ("
            "  SELECT id, user, ts, body FROM messages "
            "  WHERE room_id=?1 AND id<?2 ORDER BY id DESC LIMIT ?3"
            ") ORDER BY id ASC;",
            -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int64 (st, 1, room_id);
        sqlite3_bind_int64 (st, 2, before_id);
        sqlite3_bind_int   (st, 3, limit);
        while (count < max && sqlite3_step (st) == SQLITE_ROW)
            row_to_msg (st, &out[count++]);
        sqlite3_finalize (st);
    }
    pthread_mutex_unlock (&s_lock);
    return count;
}

long
chat_db_max_message_id (long room_id)
{
    long id = 0;
    pthread_mutex_lock (&s_lock);
    sqlite3_stmt *st = NULL;
    if (s_db && sqlite3_prepare_v2 (s_db,
            "SELECT COALESCE(MAX(id),0) FROM messages WHERE room_id=?1;",
            -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int64 (st, 1, room_id);
        if (sqlite3_step (st) == SQLITE_ROW)
            id = (long) sqlite3_column_int64 (st, 0);
        sqlite3_finalize (st);
    }
    pthread_mutex_unlock (&s_lock);
    return id;
}

/* -------------------------------------------------------------------------
 * Presence
 * ---------------------------------------------------------------------- */

void
chat_db_presence_set (long room_id, const char *user)
{
    pthread_mutex_lock (&s_lock);
    sqlite3_stmt *st = NULL;
    if (s_db && sqlite3_prepare_v2 (s_db,
            "INSERT INTO presence(room_id, user, last_seen) VALUES(?1,?2,?3) "
            "ON CONFLICT(room_id, user) DO UPDATE SET last_seen=?3;",
            -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int64 (st, 1, room_id);
        sqlite3_bind_text  (st, 2, user, -1, SQLITE_STATIC);
        sqlite3_bind_int64 (st, 3, (sqlite3_int64) time (NULL));
        sqlite3_step (st);
        sqlite3_finalize (st);
    }
    pthread_mutex_unlock (&s_lock);
}

void
chat_db_presence_clear (long room_id, const char *user)
{
    pthread_mutex_lock (&s_lock);
    sqlite3_stmt *st = NULL;
    if (s_db && sqlite3_prepare_v2 (s_db,
            "DELETE FROM presence WHERE room_id=?1 AND user=?2;",
            -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int64 (st, 1, room_id);
        sqlite3_bind_text  (st, 2, user, -1, SQLITE_STATIC);
        sqlite3_step (st);
        sqlite3_finalize (st);
    }
    pthread_mutex_unlock (&s_lock);
}

int
chat_db_active_users (long room_id, int within_secs,
                      char out[][CHAT_USER_MAX], int max)
{
    int count = 0;
    pthread_mutex_lock (&s_lock);
    sqlite3_stmt *st = NULL;
    if (s_db && sqlite3_prepare_v2 (s_db,
            "SELECT user FROM presence "
            "WHERE room_id=?1 AND last_seen>=?2 ORDER BY user;",
            -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int64 (st, 1, room_id);
        sqlite3_bind_int64 (st, 2, (sqlite3_int64) time (NULL) - within_secs);
        while (count < max && sqlite3_step (st) == SQLITE_ROW) {
            snprintf (out[count], CHAT_USER_MAX, "%s",
                      (const char *) sqlite3_column_text (st, 0));
            count++;
        }
        sqlite3_finalize (st);
    }
    pthread_mutex_unlock (&s_lock);
    return count;
}
