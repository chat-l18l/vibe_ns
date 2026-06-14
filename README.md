# vibe_ns

A production-quality C network game server built as a **vibe coding experiment** by
[OETELX](https://oetelx.nl) — a developer from the Netherlands exploring how far
AI-assisted development can take a greenfield C project in a single session.

The server accepts **Telnet** connections and presents an ANSI menu: *"Shall we play a game?"*
Chess and backgammon are on the board (stubs for now). It also speaks **HTTP** — the
same chat rooms are reachable over a REST + Server-Sent-Events API and a small web
front-end, served by a second listener inside the same process.

---

## Features

- Multi-threaded: one detached pthread per connection, up to 2000 concurrent
- FSM-based session model with table-driven state transitions
- Telnet protocol with full IAC/NAWS negotiation (RFC 854)
- **Multi-room chat**, reachable over both telnet and HTTP, with shared history
- **HTTP REST + SSE API** as a second listener — no message queue, no broker
- **SQLite storage** (vendored amalgamation, no system dependency) for chat
  history and presence — ready to back game state too
- Protocol vtable abstraction — new protocols slot in without touching core
- ESP-IDF style error type (`ns_err_t`) with named hex codes and `NS_ERROR_CHECK`
- GNU `argp` CLI: `--port`, `--http-port`, `--max-conn`, `--stack`, `--user`
- Privilege drop: bind port 23 as root, then drop to `nobody`
- SIGINT/SIGTERM for graceful shutdown (wakes even idle SSE streams), SIGHUP reload
- ANSI colors in terminal output (INFO=green, WARN=yellow, ERROR=red, DEBUG=cyan)
- Debug builds: AddressSanitizer + UBSan, `-Werror`

---

## Architecture at a glance

One process, two listeners on the shared accept loop. Both protocols sit on the
same protocol-agnostic chat service, so a message posted from the web appears
live on a telnet screen and vice versa — delivered by an in-process
publish/subscribe bus, with **no external message queue**.

```
  telnet :23  ─► telnet_protocol ─┐
                                  ├─► chat service ─► chat_db ─► SQLite (WAL)
  http   :8080 ─► http_protocol  ─┘        │
  (REST + SSE)                             └─► in-process pub/sub (notify pipes)
        ▲
        │ fetch() + EventSource
   web front-end (static files; served here or by nginx)
```

Layering keeps coupling low — telnet and HTTP never reference each other, both
see only the chat service, and only `chat_db` touches SQL:

```
chat.c (telnet UI) ─┐
                    ├─► chat service (room.c) ─► chat_db ─► third_party/sqlite3
http_chat.c (REST) ─┘
```

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the design rationale
(why one process, why SSE, why no message queue) and the full API reference.

---

## Build

Requires: `gcc`, `cmake >= 3.16`, `libpthread` (standard on Linux). SQLite is
**vendored** (`third_party/sqlite3.c`) — no `apt install` needed.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Release build (no sanitizers, optimized):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

API docs (Doxygen, optional — `apt install doxygen graphviz`):

```bash
cmake --build build --target docs   # → docs/api/html/index.html
```

---

## Run

```bash
# High ports, no privileges needed — for development
./build/netserver --port 2323 --http-port 8080 --max-conn 2000

# Standard telnet port 23 — must start as root, drops to nobody after bind
sudo ./build/netserver --port 23 --http-port 8080 --user nobody

# Disable the HTTP listener (telnet only)
./build/netserver --port 2323 --http-port 0

# All options
./build/netserver --help
```

Connect with a Telnet client, or open the web client in a browser:

```bash
telnet localhost 2323
xdg-open http://localhost:8080/
```

Both land in the same rooms with the same history.

---

## HTTP API

Base URL: `http://<host>:<http-port>`. All responses are JSON except SSE and
static files. CORS is open (`Access-Control-Allow-Origin: *`).

| Method | Path | Description |
|---|---|---|
| `GET`  | `/api/rooms` | List rooms: `[{ "id", "name" }]` |
| `GET`  | `/api/rooms/{id}/messages?since={id}&limit={n}` | Messages with id > `since` (ascending), newest `limit` if `since=0` |
| `POST` | `/api/rooms/{id}/messages` | Post a message; body `{ "user", "body" }` → `{ "id" }` |
| `GET`  | `/api/rooms/{id}/users` | Active users: `{ "count", "users": [...] }` |
| `GET`  | `/api/events/rooms/{id}?user={name}` | **SSE** stream; one `data: {message-json}` per new post |
| `GET`  | `/` · `/index.html` · `/app.js` | Static web front-end |
| `GET`  | `/api/health` | `{ "status": "ok" }` |

Quick tour with `curl`:

```bash
curl localhost:8080/api/rooms
curl -X POST localhost:8080/api/rooms/1/messages \
     -H 'Content-Type: application/json' \
     -d '{"user":"alice","body":"hello over http"}'
curl 'localhost:8080/api/rooms/1/messages?limit=20'
curl -N 'localhost:8080/api/events/rooms/1?user=alice'   # live stream
```

Message bodies and usernames are normalized to printable ASCII at the storage
boundary, so an HTTP client cannot inject terminal escape sequences onto telnet
screens.

---

## Install & deploy (systemd)

Build a release binary and install it system-wide (also installs the web
front-end to `share/netserver/web`):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build --prefix /usr/local
```

Copy the included service file and enable it:

```bash
sudo cp deploy/netserver.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now netserver
```

The service starts as root to bind port 23, then drops privileges to `nobody`.
It uses systemd's `StateDirectory=netserver`, so the chat database lives at
`/var/lib/netserver/chat.db` (with WAL sidecars). The HTTP API listens on 8080
and the web root is `NETSERVER_WEBROOT=/usr/local/share/netserver/web`. It
restarts on crash after 5 seconds and logs to journald.

```bash
journalctl -u netserver -f      # follow logs
```

**After a code update:**

```bash
cmake --build build
sudo cmake --install build --prefix /usr/local
sudo systemctl restart netserver
```

### Putting nginx / HTTPS in front

The built-in HTTP server is plaintext HTTP/1.1. For TLS or to serve the front-end
separately, front the HTTP port with nginx and proxy `/api/` (including SSE — set
`proxy_buffering off;` for the events endpoint) to `127.0.0.1:8080`.

---

## Project layout

```
src/
  core/
    err.h          # ns_err_t type, error codes, NS_ERROR_CHECK / NS_RETURN_ON_ERROR
    log.h          # LOG_INFO / LOG_WARN / LOG_ERR / LOG_DBG with ANSI colors
    fsm.h/.c       # Generic table-driven FSM engine
    connector.h/.c # Per-connection detached pthreads + semaphore slot limiter
    connection.h/.c# Connection struct and thread entry point
    server.h/.c    # Listener loop (select), signal handling, privilege drop hook
    args.h/.c      # GNU argp CLI parser
    priv.h/.c      # Privilege drop (initgroups + setgid + setuid)
  protocol/
    protocol.h     # protocol_ops_t vtable (create_session, run_session, shutdown_all)
    telnet/        # IAC parser, NAWS, telnet session FSM, game menu
    http/          # HTTP/1.1 parser, transport + SSE, session registry
  features/
    chat/
      chat_db.h/.c   # SQLite storage — the only module that knows SQL
      room.h/.c      # Chat service: registry, message API, pub/sub bus
      chat.h/.c      # Telnet chat UI (username → lobby → room → compose)
      http_chat.h/.c # REST + SSE handlers (implements http_dispatch)
    stubs.h/.c     # "Coming soon" BBS feature placeholders
  ui/
    ansi.h/.c      # ANSI escape helpers (cursor, color, clear)
    menu.h/.c      # Framed menu renderer and key handler
    bbs.h/.c       # BBS main-screen renderer (logo, two-column menu)
  games/
    game.h         # game_ops_t vtable
    chess/         # Stub
    backgammon/    # Stub
  main.c
third_party/
  sqlite3.{c,h}    # Vendored SQLite amalgamation (built with relaxed warnings)
web/
  index.html app.js# Minimal web chat client (fetch + EventSource)
deploy/
  netserver.service# systemd unit (StateDirectory, privilege drop, web root)
```

---

## Conventions

### Error handling — `ns_err_t`

Inspired by ESP-IDF. Every function that can fail returns `ns_err_t`.

```c
// Must not fail — aborts with file/line/code on error
NS_ERROR_CHECK(server_init(&srv, max_conn, stack_size));

// Propagate error up the call stack
ns_err_t my_fn(void) {
    NS_RETURN_ON_ERROR(some_other_fn());
    return NS_OK;
}

// Log and continue
NS_LOG_ON_ERROR(optional_thing());
```

Error codes are grouped by domain:

| Range | Domain |
|---|---|
| `0x101–0x11F` | Memory / arguments |
| `0x201–0x20F` | Network / server |
| `0x301–0x30F` | Protocol |
| `0x401–0x40F` | System (privileges, threads) |

### Programming errors vs. runtime errors

- **Programming errors** (wrong arguments, violated invariants): `assert()` — abort immediately, never silently continue.
- **Runtime errors** (network, malloc, user input): explicit `if` check, return `ns_err_t`, never crash.

### Concurrency model

Thread-per-connection. Each session runs its protocol on its own detached
thread and may use blocking calls freely. Cross-thread wake-ups (a chat post
reaching other sessions, or shutdown reaching idle SSE streams) go through a
per-session non-blocking **self-pipe**: a publisher writes one byte, the
session's `poll()` loop wakes and pulls fresh state. Features therefore never
touch a foreign session directly. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

### Code style

Inspired by Pieter Hintjens' *Code Connected* and the ZeroMQ codebase:

- No hidden flow control in macros (except `NS_RETURN_ON_ERROR` whose action is always `return _rc`)
- Explicit `if` checks, no clever shortcuts
- One responsibility per function
- `assert()` on every function parameter that must not be NULL
- Comments explain *why*, not *what*; public APIs carry Doxygen contracts in the headers

### Commit messages

Follows [Conventional Commits](https://www.conventionalcommits.org/):

```
feat(http): short description
fix(telnet): short description
build: short description
chore: short description
```

---

## About

**OETELX** — [oetelx.nl](https://oetelx.nl) — Netherlands

This project is a vibe coding test: build a real, production-quality network server in C
using AI assistance only, no copy-paste from existing projects, starting from zero.
The goal is to see what emerges when you describe architecture in plain language and let
the code write itself — while keeping full control over design decisions, conventions, and
quality bar.

> "Shall we play a game?"
