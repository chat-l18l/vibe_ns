cmake_minimum_required(VERSION 3.16)
project(netserver C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS OFF)

add_compile_definitions(_GNU_SOURCE)

add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wstrict-prototypes -Wmissing-prototypes
    -Wshadow
    $<$<CONFIG:Debug>:-Werror>
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)
add_link_options(
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)

add_library(netserver_core STATIC
    src/core/fsm.c
    src/core/connector.c
    src/core/server.c
    src/core/connection.c
    src/core/args.c
    src/core/priv.c
    src/protocol/protocol.c
    src/protocol/telnet/telnet_proto.c
    src/protocol/telnet/telnet_session.c
    src/ui/ansi.c
    src/ui/menu.c
)
target_include_directories(netserver_core PUBLIC src)
target_link_libraries(netserver_core PUBLIC pthread)

add_library(game_chess      STATIC src/games/chess/chess.c)
add_library(game_backgammon STATIC src/games/backgammon/backgammon.c)
target_include_directories(game_chess      PUBLIC src)
target_include_directories(game_backgammon PUBLIC src)

add_executable(netserver src/main.c)
target_link_libraries(netserver
    netserver_core
    game_chess
    game_backgammon
)
