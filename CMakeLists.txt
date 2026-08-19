cmake_minimum_required(VERSION 3.16)

project(remotifyd C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
find_package(Iconv REQUIRED)
pkg_check_modules(LIBUV REQUIRED IMPORTED_TARGET libuv)
pkg_check_modules(LIBCURL REQUIRED IMPORTED_TARGET libcurl)

add_executable(remotifyd
    src/main.c
    src/event_loop.c
    src/event_loop_executor_readloop.c
    src/event_loop_executor_handle.c
    src/ipc_process_callbacks.c
    src/executor.c
    src/executor_worker.c
    src/executor_worker_readloop.c
    src/executor_worker_handler.c
    src/executor_worker_exec.c
    src/executor_worker_exec_reader.c
    src/executor_worker_shell_locate.c
    src/executor_worker_output.c
    src/curl_muti.c
    src/logger.c
)

target_include_directories(remotifyd PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(remotifyd PRIVATE
    PkgConfig::LIBUV
    PkgConfig::LIBCURL
    Iconv::Iconv
)

if(WIN32)
    target_compile_definitions(remotifyd PRIVATE
        WINVER=0x0501
        _WIN32_WINNT=0x0501
    )
endif()
