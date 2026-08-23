cmake_minimum_required(VERSION 3.16)

project(remotifyd C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
include(ExternalProject)

find_package(Iconv REQUIRED)

# libuv is part of remotifyd's runtime, not a build-machine prerequisite.
# Pinning its static target keeps local builds and GitHub Actions identical.
set(LIBUV_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(LIBUV_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LIBUV_BUILD_BENCH OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    libuv
    URL https://github.com/libuv/libuv/archive/refs/tags/v1.52.0.tar.gz
    URL_HASH SHA256=eee139c05f7c868f5ae7a54b1e155fd5b6ed13a22329958d2ba711faad016353
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libuv
)
FetchContent_MakeAvailable(libuv)

# libidn2 does not provide a CMake build. Download its fixed source release to
# vendor, build its static library with the bundled libunistring sources, and
# expose the resulting paths before configuring libcurl.
if(NOT WIN32)
    find_program(REMOTIFY_MAKE_EXECUTABLE NAMES gmake make REQUIRED)

    FetchContent_Declare(
        libidn2
        URL https://ftp.gnu.org/gnu/libidn/libidn2-2.3.7.tar.gz
        URL_HASH SHA256=4c21a791b610b9519b9d0e12b8097bf2f359b12f8dd92647611a929e6bfd7d64
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libidn2
    )
    FetchContent_GetProperties(libidn2)
    if(NOT libidn2_POPULATED)
        FetchContent_Populate(libidn2)
    endif()

    set(REMOTIFY_LIBIDN2_INSTALL ${CMAKE_CURRENT_BINARY_DIR}/libidn2)
    set(LIBIDN2_INCLUDE_DIR ${REMOTIFY_LIBIDN2_INSTALL}/include CACHE PATH "" FORCE)
    set(LIBIDN2_LIBRARY ${REMOTIFY_LIBIDN2_INSTALL}/lib/libidn2.a CACHE FILEPATH "" FORCE)

    # FindLibidn2 reads the public header while configuring libcurl, before the
    # external build runs. Generate the same release header that `make install`
    # will later place at this path.
    file(MAKE_DIRECTORY ${LIBIDN2_INCLUDE_DIR})
    set(VERSION 2.3.7)
    set(NUMBER_VERSION 0x02030007)
    set(MAJOR_VERSION 2)
    set(MINOR_VERSION 3)
    set(PATCH_VERSION 7)
    configure_file(
        ${libidn2_SOURCE_DIR}/lib/idn2.h.in
        ${LIBIDN2_INCLUDE_DIR}/idn2.h
        @ONLY
    )

    ExternalProject_Add(
        remotify_libidn2
        SOURCE_DIR ${libidn2_SOURCE_DIR}
        BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/libidn2-build
        INSTALL_DIR ${REMOTIFY_LIBIDN2_INSTALL}
        CONFIGURE_COMMAND
            <SOURCE_DIR>/configure
            --prefix=<INSTALL_DIR>
            --disable-doc
            --disable-nls
            --disable-shared
            --enable-static
            --with-included-libunistring
        BUILD_COMMAND ${REMOTIFY_MAKE_EXECUTABLE}
        INSTALL_COMMAND ${REMOTIFY_MAKE_EXECUTABLE} install
        BUILD_BYPRODUCTS ${LIBIDN2_LIBRARY}
    )

    set(USE_LIBIDN2 ON CACHE BOOL "" FORCE)
endif()

# remotifyd requires libcurl's WebSocket API, which is not enabled by every
# operating-system libcurl. Keep the exact source version beside the other
# vendored dependencies so every default build uses the same implementation.
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_WEBSOCKETS OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH OFF CACHE BOOL "" FORCE)
set(CURL_USE_PKGCONFIG OFF CACHE BOOL "" FORCE)
set(ENABLE_CURL_MANUAL OFF CACHE BOOL "" FORCE)
set(BROTLI_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ENABLE_LIB_ONLY ON CACHE BOOL "" FORCE)
set(ENABLE_FAILMALLOC OFF CACHE BOOL "" FORCE)
set(ENABLE_DOC OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_SCHANNEL OFF CACHE BOOL "" FORCE)
set(CURL_USE_MBEDTLS ON CACHE BOOL "" FORCE)

# A single vendored TLS implementation avoids Homebrew on macOS and keeps the
# HTTPS/WebSocket behavior consistent on Linux and old Windows releases.
FetchContent_Declare(
    mbedtls
    URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2
    URL_HASH SHA256=a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/mbedtls
)
FetchContent_MakeAvailable(mbedtls)

set(MBEDTLS_INCLUDE_DIR ${mbedtls_SOURCE_DIR}/include CACHE PATH "" FORCE)
set(MBEDTLS_LIBRARY mbedtls CACHE STRING "" FORCE)
set(MBEDX509_LIBRARY mbedx509 CACHE STRING "" FORCE)
set(MBEDCRYPTO_LIBRARY mbedcrypto CACHE STRING "" FORCE)
set(MBEDTLS_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    brotli
    URL https://github.com/google/brotli/archive/refs/tags/v1.2.0.tar.gz
    URL_HASH SHA256=816c96e8e8f193b40151dad7e8ff37b1221d019dbcb9c35cd3fadbfe6477dfec
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/brotli
)
FetchContent_MakeAvailable(brotli)

# Defining these inputs makes libcurl's FindBrotli use the vendored CMake
# targets instead of rediscovering a system package through pkg-config.
set(BROTLI_INCLUDE_DIR ${brotli_SOURCE_DIR}/c/include CACHE PATH "" FORCE)
set(BROTLIDEC_LIBRARY brotlidec CACHE STRING "" FORCE)
set(BROTLICOMMON_LIBRARY brotlicommon CACHE STRING "" FORCE)
set(BROTLI_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(CURL_BROTLI ON CACHE STRING "" FORCE)

FetchContent_Declare(
    zstd
    URL https://github.com/facebook/zstd/archive/refs/tags/v1.5.7.tar.gz
    URL_HASH SHA256=37d7284556b20954e56e1ca85b80226768902e2edabd3b649e9e72c0c9012ee3
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/zstd
)
FetchContent_GetProperties(zstd)
if(NOT zstd_POPULATED)
    FetchContent_Populate(zstd)
    add_subdirectory(${zstd_SOURCE_DIR}/build/cmake ${zstd_BINARY_DIR})
endif()

# Keep zstd inside the same reproducible dependency graph as libcurl. Passing
# the CMake target here prevents FindZstd from replacing it with pkg-config's
# system library name at the final link step.
set(ZSTD_INCLUDE_DIR ${zstd_SOURCE_DIR}/lib CACHE PATH "" FORCE)
set(ZSTD_LIBRARY libzstd_static CACHE STRING "" FORCE)
set(ZSTD_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(CURL_ZSTD ON CACHE STRING "" FORCE)

FetchContent_Declare(
    nghttp2
    URL https://github.com/nghttp2/nghttp2/releases/download/v1.69.0/nghttp2-1.69.0.tar.xz
    URL_HASH SHA256=1fb324b6ec2c56f6bde0658f4139ffd8209fa9e77ce98fd7a5f63af8d0e508ad
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/nghttp2
)
FetchContent_MakeAvailable(nghttp2)

# libcurl only needs nghttp2's library target. Supplying it directly keeps the
# final executable independent from whichever libnghttp2 pkg-config finds on
# the build machine.
set(NGHTTP2_INCLUDE_DIR ${nghttp2_SOURCE_DIR}/lib/includes CACHE PATH "" FORCE)
set(NGHTTP2_LIBRARY nghttp2_static CACHE STRING "" FORCE)
set(NGHTTP2_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(USE_NGHTTP2 ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    libcurl
    URL https://curl.se/download/curl-8.20.0.tar.xz
    URL_HASH SHA256=63fe2dc148ba0ceae89922ef838f7e5c946272c2e78b7c59fab4b79d3ce2b896
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libcurl
)
FetchContent_MakeAvailable(libcurl)

if(NOT WIN32)
    add_dependencies(libcurl_static remotify_libidn2)
endif()

set(REMOTIFY_CA_BUNDLE_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/ca_bundle.c")

add_custom_command(
    OUTPUT "${REMOTIFY_CA_BUNDLE_SOURCE}"
    COMMAND ${CMAKE_COMMAND}
        -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/vendor/cacert.pem
        -DOUTPUT=${REMOTIFY_CA_BUNDLE_SOURCE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_ca_bundle.cmake
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/cacert.pem
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_ca_bundle.cmake
    VERBATIM
)

add_executable(remotifyd
    ${REMOTIFY_CA_BUNDLE_SOURCE}
    src/main.c
    src/init.c
    src/api_base.c
    src/event_loop.c
    src/event_loop_executor_readloop.c
    src/event_loop_executor_handle.c
    src/ipc_process_callbacks.c
    src/device_code.c
    src/executor.c
    src/executor_worker.c
    src/executor_worker_readloop.c
    src/executor_worker_handler.c
    src/executor_worker_exec.c
    src/executor_worker_exec_reader.c
    src/executor_worker_shell_locate.c
    src/executor_worker_output.c
    src/enroll.c
    src/state.c
    src/circular_queue.c
    src/curl_multi.c
    src/websocket_eventloop.c
    src/device_protocol_gen.c
    src/websocket_decode.c
    src/websocket_ioloop.c
    src/websocket_send.c
    src/logger.c
)

target_include_directories(remotifyd PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)

target_link_libraries(remotifyd PRIVATE
    uv_a
    CURL::libcurl
    Iconv::Iconv
)

if(WIN32)
    target_compile_definitions(remotifyd PRIVATE
        WINVER=0x0501
        _WIN32_WINNT=0x0501
    )
endif()
