if(NOT WIN32)
    message(FATAL_ERROR "libuv 1.13.1 wrapper is only used by old Windows targets")
endif()

add_library(uv_a STATIC
    ${libuv_SOURCE_DIR}/src/fs-poll.c
    ${libuv_SOURCE_DIR}/src/inet.c
    ${libuv_SOURCE_DIR}/src/threadpool.c
    ${libuv_SOURCE_DIR}/src/uv-common.c
    ${libuv_SOURCE_DIR}/src/version.c
    ${libuv_SOURCE_DIR}/src/win/async.c
    ${libuv_SOURCE_DIR}/src/win/core.c
    ${libuv_SOURCE_DIR}/src/win/detect-wakeup.c
    ${libuv_SOURCE_DIR}/src/win/dl.c
    ${libuv_SOURCE_DIR}/src/win/error.c
    ${libuv_SOURCE_DIR}/src/win/fs.c
    ${libuv_SOURCE_DIR}/src/win/fs-event.c
    ${libuv_SOURCE_DIR}/src/win/getaddrinfo.c
    ${libuv_SOURCE_DIR}/src/win/getnameinfo.c
    ${libuv_SOURCE_DIR}/src/win/handle.c
    ${libuv_SOURCE_DIR}/src/win/loop-watcher.c
    ${libuv_SOURCE_DIR}/src/win/pipe.c
    ${libuv_SOURCE_DIR}/src/win/thread.c
    ${libuv_SOURCE_DIR}/src/win/poll.c
    ${libuv_SOURCE_DIR}/src/win/process.c
    ${libuv_SOURCE_DIR}/src/win/process-stdio.c
    ${libuv_SOURCE_DIR}/src/win/req.c
    ${libuv_SOURCE_DIR}/src/win/signal.c
    ${libuv_SOURCE_DIR}/src/win/snprintf.c
    ${libuv_SOURCE_DIR}/src/win/stream.c
    ${libuv_SOURCE_DIR}/src/win/tcp.c
    ${libuv_SOURCE_DIR}/src/win/tty.c
    ${libuv_SOURCE_DIR}/src/win/timer.c
    ${libuv_SOURCE_DIR}/src/win/udp.c
    ${libuv_SOURCE_DIR}/src/win/util.c
    ${libuv_SOURCE_DIR}/src/win/winapi.c
    ${libuv_SOURCE_DIR}/src/win/winsock.c
)

target_include_directories(uv_a
    PUBLIC ${libuv_SOURCE_DIR}/include
    PRIVATE ${libuv_SOURCE_DIR}/src
)

target_compile_definitions(uv_a PRIVATE
    _GNU_SOURCE
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(uv_a PUBLIC
    advapi32
    iphlpapi
    psapi
    shell32
    user32
    userenv
    ws2_32
)
