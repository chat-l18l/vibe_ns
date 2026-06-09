#ifndef CHAT_ROOM_H
#define CHAT_ROOM_H

#include <sys/types.h>

#define CHAT_MAX_ROOMS       8
#define CHAT_MAX_SUBSCRIBERS 256
#define CHAT_DATA_DIR        "data/chat"

typedef struct {
    char             name[32];
    char             log_path[512];
    int              sub_fds[CHAT_MAX_SUBSCRIBERS];
    int              sub_count;
    /* mutex is embedded; opaque to callers — use API only */
} chat_room_t;

void          chat_rooms_ensure_init(void);
chat_room_t **chat_rooms_list(int *count);

int     chat_room_post(chat_room_t *r, const char *username, const char *msg);
void    chat_room_subscribe(chat_room_t *r, int notify_wfd);
void    chat_room_unsubscribe(chat_room_t *r, int notify_wfd);

/* Read up to max_lines lines whose end is at or before before_off (0 = EOF).
   Fills buf with formatted display lines. Returns line count. Sets *new_off. */
int     chat_room_read_history(chat_room_t *r, off_t before_off,
                               int max_lines, char *buf, size_t bufsz,
                               off_t *new_off);

/* Read bytes appended since from_off. Returns bytes written into buf.
   Updates *new_off to new file size. */
ssize_t chat_room_read_since(chat_room_t *r, off_t from_off,
                             char *buf, size_t bufsz, off_t *new_off);

#endif /* CHAT_ROOM_H */
