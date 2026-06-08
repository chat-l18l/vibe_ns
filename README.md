# vibe_ns

A production-quality C network game server built as a **vibe coding experiment** by
[OETELX](https://oetelx.nl) — a developer from the Netherlands exploring how far
AI-assisted development can take a greenfield C project in a single session.

The server accepts Telnet connections and presents an ANSI menu: *"Shall we play a game?"*
Chess and backgammon are on the board (stubs for now).

---

## Features

- Multi-threaded: one detached pthread per connection, up to 2000 concurrent
- FSM-based session model with table-driven state transitions
- Telnet protocol with full IAC/NAWS negotiation (RFC 854)
- Protocol vtable abstraction — new protocols slot in without touching core
- ESP-IDF style error type (`ns_err_t`) with named hex codes and `NS_ERROR_CHECK`
- GNU `argp` CLI: `--port`, `--max-conn`, `--stack`, `--user`
- Privilege drop: bind port 23 as root, then drop to `nobody`
- SIGINT/SIGTERM for graceful shutdown, SIGHUP for config reload
- ANSI colors in terminal output (INFO=green, WARN=yellow, ERROR=red, DEBUG=cyan)
- Debug builds: AddressSanitizer + UBSan, `-Werror`

---

## Build

Requires: `gcc`, `cmake >= 3.16`, `libpthread` (standard on Linux).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Release build (no sanitizers, optimized):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

---

## Run

```bash
# High port, no privileges needed — for development
./build/netserver --port 2323 --max-conn 2000

# Port 23 — must start as root, drops to nobody after bind
sudo ./build/netserver --port 23 --user nobody

# All options
./build/netserver --help
```

Connect with any Telnet client:

```bash
telnet localhost 2323
```

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
    protocol.h     # protocol_ops_t vtable (create_session, run_session, destroy)
    telnet/        # IAC parser, NAWS, telnet FSM, game menu
  ui/
    ansi.h/.c      # ANSI escape helpers (cursor, color, clear)
    menu.h/.c      # Framed menu renderer and key handler
  games/
    game.h         # game_ops_t vtable
    chess/         # Stub
    backgammon/    # Stub
  main.c
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

### Code style

Inspired by Pieter Hintjens' *Code Connected* and the ZeroMQ codebase:

- No hidden flow control in macros (except `NS_RETURN_ON_ERROR` whose action is always `return _rc`)
- Explicit `if` checks, no clever shortcuts
- One responsibility per function
- `assert()` on every function parameter that must not be NULL
- No comments that explain *what* — only comments that explain *why* when the reason is non-obvious

### Commit messages

Follows [Conventional Commits](https://www.conventionalcommits.org/):

```
feat(core): short description
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
