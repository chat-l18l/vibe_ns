# Architecture

This document explains how `netserver` is put together and **why** the key
design decisions were made. For the public API of each module, read the
Doxygen comments in the headers (or build `cmake --build build --target docs`).

---

## 1. Process and threading model

**One process, thread-per-connection.** The server runs a single accept loop
(`core/server.c`) over one or more listening sockets. Each accepted connection
is handed to the connector (`core/connector.c`), which runs it on its own
detached pthread, bounded by a counting semaphore (`--max-conn`, default 2000).

Why thread-per-connection rather than an event loop:

- Session code reads sequentially (`recv → handle → send`) and may block. No
  callback inversion, no per-connection write queues.
- It gives the lowest coupling between modules — a game or chat feature is
  written as if it were the only user of the machine, with no awareness of I/O
  multiplexing.
- The realistic ceiling (5k–10k connections on Linux) is far beyond what a
  telnet BBS needs.

Each session thread multiplexes its socket **and** a private notify pipe with
`poll()` (not `select()` — with 3 fds per connection the fd numbers exceed
`FD_SETSIZE` well before `--max-conn`).

---

## 2. Protocol abstraction

`protocol_ops_t` (`protocol/protocol.h`) is a Strategy vtable:

```c
struct protocol_ops {
    const char *name, *description;
    void *(*create_session)(int sockfd, const struct sockaddr_storage *peer);
    void  (*run_session)(void *session);
    void  (*request_shutdown)(void *session);
    void  (*shutdown_all)(void);
};
```

The accept loop is protocol-agnostic: every listener carries a vtable and every
accepted connection runs through it. `telnet_protocol` and `http_protocol` are
two implementations bound to two ports in `main.c`. Adding a third protocol is a
new vtable and a `server_add_listener()` call — no core changes.

`shutdown_all()` lets each protocol wake all its live sessions at server
shutdown (see §5), so a `SIGTERM` completes in milliseconds instead of waiting
out idle timeouts.

---

## 3. Chat layering (low coupling by construction)

```
chat.c (telnet UI) ─┐
                    ├─► chat service (room.c) ─► chat_db ─► sqlite3
http_chat.c (REST) ─┘
```

- **`chat_db`** (`features/chat/chat_db.c`) is the *only* module that knows SQL.
  One process-wide connection behind a mutex, WAL mode, schema for
  `rooms` / `messages` / `presence`. It speaks in plain-data records
  (`chat_msg_t`), never in `sqlite3*` handles.
- **`chat service`** (`features/chat/room.c`) is protocol-agnostic. It owns the
  room registry and the in-process publish/subscribe bus, and delegates
  persistence to `chat_db`. It is also the single choke point where message
  bodies and usernames are sanitized to printable ASCII (see §6).
- **`chat.c`** renders the telnet ANSI UI; **`http_chat.c`** renders JSON and
  drives SSE. Neither references the other; both call only the service.

Telnet and HTTP therefore share rooms, history and presence with zero direct
coupling.

---

## 4. Storage: SQLite

Chat history moved from append-only log files to a vendored SQLite amalgamation
(`third_party/sqlite3.c`, built as its own target with relaxed warnings).

Why SQLite:

- Query-ability (filter by room / user / time) the log files never had.
- The monotonic `messages.id` is a clean paging cursor for both telnet history
  and the HTTP `since=` parameter — replacing a brittle byte-offset scan and the
  colon-splitting log parser it required.
- One schema ready to back game state later.
- WAL mode permits concurrent readers, so a future read-only consumer (an
  analytics tool, an nginx-served read API) could attach without code changes.

It is **vendored**, not an `apt` dependency: a single `.c` compiled into the
binary keeps the "few dependencies" property of the project.

Storage location: `$STATE_DIRECTORY/chat.db` under systemd
(`StateDirectory=netserver`), else `./data/chat.db` for dev runs.

Trade-off: a single shared connection + mutex is the simplest correct design and
fine at chat volume (SQLite serializes writers anyway). It can be upgraded to a
per-thread connection pool later without touching callers, since all SQL lives
in one module.

---

## 5. Notification model — why no message queue

When someone posts a message, every other participant must be told: telnet
clients redraw, web clients receive an SSE frame.

The decisive fact is the **process topology**. Because the HTTP API runs *in the
same process* as telnet, the notification never crosses a process boundary, so
the existing in-process pub/sub is the entire delivery mechanism:

1. Each session owns a non-blocking **self-pipe**, created at session start.
2. On entering a room it registers the pipe's write end with the room
   (`chat_room_subscribe`).
3. `chat_room_post()` persists the message, then writes one byte to every
   subscriber's pipe.
4. The woken session pulls messages newer than the last id it saw
   (`chat_room_since`) and renders them — telnet as an ANSI redraw, HTTP as
   `data: {json}\n\n` SSE frames.

A telnet self-pipe and an SSE connection both fit the same `sub_fds[]` list, so
one mechanism serves both. The same pipe is used by `shutdown_all()` to wake and
drop even idle SSE streams on `SIGTERM`.

**No ZeroMQ, no MQTT, no broker, no Redis.** Those solve cross-process /
cross-machine fan-out. Adding one here would only *increase* inter-module
coupling (every module would learn the MQ API and topic conventions) for a
problem the single-process design doesn't have. Note that SQLite's own
`update_hook` fires only in the writing process — so splitting into multiple
processes would not get notifications "for free" either; it would *add* the need
for a cross-process channel. That asymmetry is precisely why the API lives
in-process.

An MQ becomes worth revisiting only on horizontal scale across machines, or to
feed independent external consumers — neither of which this server has.

---

## 6. Security notes

- **Escape-sequence injection.** Message bodies and presence names are
  normalized to printable ASCII (`0x20–0x7E`) at the chat-service post/presence
  boundary, before storage. Both telnet and HTTP inputs pass through it, so a
  web client cannot store bytes that would execute as terminal escapes on a
  telnet screen. The web client additionally renders text via `textContent`
  (no HTML injection).
- **Privilege drop.** Binds privileged ports as root, then `initgroups` →
  `setgid` → `setuid` to `nobody` and verifies it is no longer root before
  serving any input (`core/priv.c`).
- **Transport.** The built-in HTTP server is plaintext HTTP/1.1 with no chunked
  request bodies — a deliberately small surface for an API we control both ends
  of. Put nginx in front for TLS.

---

## 7. Session FSM (telnet)

Telnet sessions use the generic table-driven FSM engine (`core/fsm.c`) with a
declarative transition table in `protocol/telnet/telnet_session.c`. The engine
is not re-entrant: an action must not dispatch into its own machine (the engine
writes `current_state` after the action returns). Where a follow-up event is
needed, the action raises a flag and the session loop dispatches it after the
outer call returns (`game_over_pending`, `quit_pending`). The chat feature,
being only four states, uses a plain enum + switch rather than the engine — a
deliberate "right-sized" choice.
