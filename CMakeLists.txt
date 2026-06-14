cmake_minimum_required(VERSION 3.16)
project(netserver C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS OFF)

add_compile_definitions(_GNU_SOURCE)

# Vendored SQLite amalgamation — built as its own target WITHOUT the strict
# project warnings (it does not pass -Wpedantic/-Werror). Compile-time
# options trim features we never use and pin threading to serialized mode.
add_library(sqlite3 STATIC third_party/sqlite3.c)
target_include_directories(sqlite3 PUBLIC third_party)
target_compile_definitions(sqlite3 PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_DQS=0
    SQLITE_DEFAULT_MEMSTATUS=0
)
target_compile_options(sqlite3 PRIVATE -w)
target_link_libraries(sqlite3 PUBLIC pthread ${CMAKE_DL_LIBS} m)

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
    src/ui/bbs.c
    src/features/stubs.c
    src/features/chat/chat_db.c
    src/features/chat/room.c
    src/features/chat/chat.c
)
target_include_directories(netserver_core PUBLIC src)
target_link_libraries(netserver_core PUBLIC pthread sqlite3)

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

install(TARGETS netserver DESTINATION bin)

# API documentation: cmake --build build --target docs
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
    add_custom_target(docs
        COMMAND Doxygen::doxygen ${CMAKE_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating API documentation (docs/api/html)"
        VERBATIM
    )
endif()
