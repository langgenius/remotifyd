cmake_minimum_required(VERSION 3.16)

project(remotifyd C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
include(ExternalProject)
include(CTest)
set(REMOTIFY_BUILD_TESTS ${BUILD_TESTING})

set(REMOTIFY_TARGET "" CACHE STRING "remotifyd minimum operating-system target")
set_property(CACHE REMOTIFY_TARGET PROPERTY STRINGS
    darwin
    linux
    windowsxp
    windows7
    windows10
)

if(REMOTIFY_TARGET STREQUAL "")
    if(WIN32)
        set(REMOTIFY_TARGET windows10)
    elseif(APPLE)
        set(REMOTIFY_TARGET darwin)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(REMOTIFY_TARGET linux)
    else()
        message(FATAL_ERROR "REMOTIFY_TARGET must name a supported operating system")
    endif()
endif()

set(REMOTIFY_USE_OLD_LIBUV OFF)
set(REMOTIFY_USE_XP_TLS OFF)

if(REMOTIFY_TARGET STREQUAL "windowsxp")
    if(NOT WIN32)
        message(FATAL_ERROR "windowsxp requires a Windows toolchain")
    endif()
    set(REMOTIFY_WINDOWS_VERSION 0x0501)
    set(REMOTIFY_USE_OLD_LIBUV ON)
    set(REMOTIFY_USE_XP_TLS ON)
elseif(REMOTIFY_TARGET STREQUAL "windows7")
    if(NOT WIN32)
        message(FATAL_ERROR "windows7 requires a Windows toolchain")
    endif()
    set(REMOTIFY_WINDOWS_VERSION 0x0601)
    set(REMOTIFY_USE_OLD_LIBUV ON)
elseif(REMOTIFY_TARGET STREQUAL "windows10")
    if(NOT WIN32)
        message(FATAL_ERROR "windows10 requires a Windows toolchain")
    endif()
    set(REMOTIFY_WINDOWS_VERSION 0x0A00)
elseif(REMOTIFY_TARGET STREQUAL "darwin")
    if(NOT APPLE)
        message(FATAL_ERROR "darwin requires a macOS toolchain")
    endif()
elseif(REMOTIFY_TARGET STREQUAL "linux")
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "linux requires a Linux toolchain")
    endif()
else()
    message(FATAL_ERROR "Unsupported REMOTIFY_TARGET: ${REMOTIFY_TARGET}")
endif()

if(WIN32)
    add_compile_definitions(
        WINVER=${REMOTIFY_WINDOWS_VERSION}
        _WIN32_WINNT=${REMOTIFY_WINDOWS_VERSION}
    )
    set(CURL_TARGET_WINDOWS_VERSION ${REMOTIFY_WINDOWS_VERSION} CACHE STRING "" FORCE)
endif()

# Pinned utf8proc 2.8 and XP's Mbed TLS 2.28 both predate CMake 4 and
# declare compatibility below the oldest policy version CMake 4 accepts.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

message(STATUS "remotifyd target: ${REMOTIFY_TARGET}")

# Release artifacts run unattended and keep no local symbol files. Optimize
# every CMake-built dependency for size, let LTO remove cross-object dead code,
# and place remaining functions/data in sections the platform linker can drop.
add_compile_options(
    $<$<CONFIG:Release>:-Os>
    $<$<CONFIG:Release>:-ffunction-sections>
    $<$<CONFIG:Release>:-fdata-sections>
)
set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

if(APPLE)
    add_link_options($<$<CONFIG:Release>:-Wl,-dead_strip>)
else()
    add_link_options($<$<CONFIG:Release>:-Wl,--gc-sections>)
endif()

find_program(REMOTIFY_MAKE_EXECUTABLE NAMES gmake make REQUIRED)

set(REMOTIFY_DEPENDENCY_ENV)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    # Autoconf dependencies do not inherit CMake's Release flags. Keep every
    # supported encoding, but avoid debug/unused sections in their objects.
    list(APPEND REMOTIFY_DEPENDENCY_ENV
        "CFLAGS=-Os -DNDEBUG -ffunction-sections -fdata-sections"
    )
endif()

# GNU libiconv is part of remotifyd on every platform. Building one pinned
# implementation keeps streaming conversion behavior identical, including on
# Windows versions whose C runtime does not provide iconv.
FetchContent_Declare(
    libiconv
    URL https://ftpmirror.gnu.org/libiconv/libiconv-1.18.tar.gz
    URL_HASH SHA256=3b08f5f4f9b4eb82f151a7040bfd6fe6c6fb922efe4b1659c66ea933276965e8
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libiconv
)
FetchContent_GetProperties(libiconv)
if(NOT libiconv_POPULATED)
    FetchContent_Populate(libiconv)
endif()

set(REMOTIFY_LIBICONV_INSTALL ${CMAKE_CURRENT_BINARY_DIR}/libiconv)
set(REMOTIFY_LIBICONV_CONFIGURE_ARGS
    --prefix=${REMOTIFY_LIBICONV_INSTALL}
    --disable-shared
    --enable-static
)
set(REMOTIFY_LIBICONV_CPPFLAGS "")

if(CMAKE_CROSSCOMPILING)
    execute_process(
        COMMAND ${CMAKE_C_COMPILER} -dumpmachine
        OUTPUT_VARIABLE REMOTIFY_C_COMPILER_TARGET
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE REMOTIFY_C_COMPILER_TARGET_RESULT
    )
    if(NOT REMOTIFY_C_COMPILER_TARGET_RESULT EQUAL 0 OR REMOTIFY_C_COMPILER_TARGET STREQUAL "")
        message(FATAL_ERROR "The cross compiler did not report its target tuple")
    endif()
    list(APPEND REMOTIFY_LIBICONV_CONFIGURE_ARGS --host=${REMOTIFY_C_COMPILER_TARGET})
endif()

if(WIN32)
    set(REMOTIFY_LIBICONV_CPPFLAGS
        "-D_WIN32_WINNT=${REMOTIFY_WINDOWS_VERSION} -DWINVER=${REMOTIFY_WINDOWS_VERSION}"
    )
endif()

set(REMOTIFY_LIBICONV_LIBRARY ${REMOTIFY_LIBICONV_INSTALL}/lib/libiconv.a)
set(REMOTIFY_LIBCHARSET_LIBRARY ${REMOTIFY_LIBICONV_INSTALL}/lib/libcharset.a)
file(MAKE_DIRECTORY ${REMOTIFY_LIBICONV_INSTALL}/include)

ExternalProject_Add(
    remotify_libiconv
    SOURCE_DIR ${libiconv_SOURCE_DIR}
    BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/libiconv-build
    INSTALL_DIR ${REMOTIFY_LIBICONV_INSTALL}
    CONFIGURE_COMMAND
        ${CMAKE_COMMAND} -E env
        CC=${CMAKE_C_COMPILER}
        "CPPFLAGS=${REMOTIFY_LIBICONV_CPPFLAGS}"
        ${REMOTIFY_DEPENDENCY_ENV}
        <SOURCE_DIR>/configure
        ${REMOTIFY_LIBICONV_CONFIGURE_ARGS}
    BUILD_COMMAND ${REMOTIFY_MAKE_EXECUTABLE}
    INSTALL_COMMAND ${REMOTIFY_MAKE_EXECUTABLE} install
    BUILD_BYPRODUCTS
        ${REMOTIFY_LIBICONV_LIBRARY}
        ${REMOTIFY_LIBCHARSET_LIBRARY}
)

add_library(remotify_iconv INTERFACE)
target_include_directories(remotify_iconv INTERFACE ${REMOTIFY_LIBICONV_INSTALL}/include)
target_link_libraries(remotify_iconv INTERFACE
    ${REMOTIFY_LIBICONV_LIBRARY}
    ${REMOTIFY_LIBCHARSET_LIBRARY}
)

# XP and Windows 7 use the last libuv release whose public API still supports
# the old Windows process and pipe implementation. Modern targets stay on the
# current implementation and receive its newer fixes.
if(REMOTIFY_USE_OLD_LIBUV)
    FetchContent_Declare(
        libuv
        URL https://github.com/libuv/libuv/archive/refs/tags/v1.13.1.tar.gz
        URL_HASH SHA256=6a4976887594ec9f4e8521c95598dbbc478c59733056a5cf46354e93a1bc8400
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libuv-1.13.1
    )
    FetchContent_GetProperties(libuv)
    if(NOT libuv_POPULATED)
        FetchContent_Populate(libuv)
    endif()
    include(cmake/libuv-1.13.1.cmake)
else()
    set(LIBUV_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(LIBUV_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(LIBUV_BUILD_BENCH OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        libuv
        URL https://github.com/libuv/libuv/archive/refs/tags/v1.52.0.tar.gz
        URL_HASH SHA256=eee139c05f7c868f5ae7a54b1e155fd5b6ed13a22329958d2ba711faad016353
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libuv-1.52.0
    )
    FetchContent_MakeAvailable(libuv)
endif()

# Keep Unicode behavior aligned with Go 1.25 and uniseg, which both use
# Unicode 15.0. The same small C library supplies categories now and the
# NFKC/grapheme operations required by source-fuzzy matching later.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(UTF8PROC_INSTALL OFF CACHE BOOL "" FORCE)
set(UTF8PROC_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    utf8proc
    URL https://github.com/JuliaStrings/utf8proc/archive/refs/tags/v2.8.0.tar.gz
    URL_HASH SHA256=a0a60a79fe6f6d54e7d411facbfcc867a6e198608f2cd992490e46f04b1bcecc
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/utf8proc-2.8.0
)
FetchContent_MakeAvailable(utf8proc)

# libgrapheme 2.0.2 supplies the Unicode 15.0 UAX #29 word boundaries used
# by Echo's source-provenance checks. Build only its freestanding word path.
FetchContent_Declare(
    libgrapheme
    URL https://dl.suckless.org/libgrapheme/libgrapheme-2.0.2.tar.gz
    URL_HASH SHA256=a68bbddde76bd55ba5d64116ce5e42a13df045c81c0852de9ab60896aa143125
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libgrapheme-2.0.2
)
FetchContent_GetProperties(libgrapheme)
if(NOT libgrapheme_POPULATED)
    FetchContent_Populate(libgrapheme)
endif()

set(REMOTIFY_LIBGRAPHEME_WORD_DIR ${CMAKE_CURRENT_BINARY_DIR}/libgrapheme-word)
file(MAKE_DIRECTORY
    ${REMOTIFY_LIBGRAPHEME_WORD_DIR}/gen
    ${REMOTIFY_LIBGRAPHEME_WORD_DIR}/src
)
configure_file(
    ${libgrapheme_SOURCE_DIR}/src/word.c
    ${REMOTIFY_LIBGRAPHEME_WORD_DIR}/src/word.c
    COPYONLY
)
configure_file(
    ${libgrapheme_SOURCE_DIR}/grapheme.h
    ${REMOTIFY_LIBGRAPHEME_WORD_DIR}/grapheme.h
    COPYONLY
)

if(CMAKE_CROSSCOMPILING)
    find_program(REMOTIFY_HOST_C_COMPILER NAMES cc clang gcc REQUIRED)
else()
    set(REMOTIFY_HOST_C_COMPILER ${CMAKE_C_COMPILER})
endif()
if(CMAKE_HOST_WIN32)
    set(REMOTIFY_LIBGRAPHEME_GENERATOR ${REMOTIFY_LIBGRAPHEME_WORD_DIR}/word-gen.exe)
else()
    set(REMOTIFY_LIBGRAPHEME_GENERATOR ${REMOTIFY_LIBGRAPHEME_WORD_DIR}/word-gen)
endif()
if(MSVC AND NOT CMAKE_CROSSCOMPILING)
    execute_process(
        COMMAND ${REMOTIFY_HOST_C_COMPILER} /nologo /O2 /std:c11
            /Fe${REMOTIFY_LIBGRAPHEME_GENERATOR}
            ${libgrapheme_SOURCE_DIR}/gen/word.c
            ${libgrapheme_SOURCE_DIR}/gen/util.c
        WORKING_DIRECTORY ${REMOTIFY_LIBGRAPHEME_WORD_DIR}
        RESULT_VARIABLE REMOTIFY_LIBGRAPHEME_COMPILE_RESULT
    )
else()
    execute_process(
        COMMAND ${REMOTIFY_HOST_C_COMPILER} -std=c99 -O2 -D_POSIX_C_SOURCE=200809L
            -o ${REMOTIFY_LIBGRAPHEME_GENERATOR}
            ${libgrapheme_SOURCE_DIR}/gen/word.c
            ${libgrapheme_SOURCE_DIR}/gen/util.c
        WORKING_DIRECTORY ${REMOTIFY_LIBGRAPHEME_WORD_DIR}
        RESULT_VARIABLE REMOTIFY_LIBGRAPHEME_COMPILE_RESULT
    )
endif()
if(NOT REMOTIFY_LIBGRAPHEME_COMPILE_RESULT EQUAL 0)
    message(FATAL_ERROR "The host compiler could not build libgrapheme's word-table generator")
endif()
execute_process(
    COMMAND ${REMOTIFY_LIBGRAPHEME_GENERATOR}
    WORKING_DIRECTORY ${libgrapheme_SOURCE_DIR}
    OUTPUT_FILE ${REMOTIFY_LIBGRAPHEME_WORD_DIR}/gen/word.h.tmp
    RESULT_VARIABLE REMOTIFY_LIBGRAPHEME_GENERATE_RESULT
)
if(NOT REMOTIFY_LIBGRAPHEME_GENERATE_RESULT EQUAL 0)
    file(REMOVE ${REMOTIFY_LIBGRAPHEME_WORD_DIR}/gen/word.h.tmp)
    message(FATAL_ERROR "libgrapheme's word-table generator failed")
endif()
file(RENAME
    ${REMOTIFY_LIBGRAPHEME_WORD_DIR}/gen/word.h.tmp
    ${REMOTIFY_LIBGRAPHEME_WORD_DIR}/gen/word.h
)

add_library(remotify_grapheme_word STATIC
    ${REMOTIFY_LIBGRAPHEME_WORD_DIR}/src/word.c
    ${libgrapheme_SOURCE_DIR}/src/util.c
    ${libgrapheme_SOURCE_DIR}/src/utf8.c
)
target_include_directories(remotify_grapheme_word
    PUBLIC ${REMOTIFY_LIBGRAPHEME_WORD_DIR}
    PRIVATE ${libgrapheme_SOURCE_DIR}/src
)

# libidn2 does not provide a CMake build. Download its fixed source release to
# vendor, build its static library with the bundled libunistring sources, and
# expose the resulting paths before configuring libcurl.
if(NOT WIN32)
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
        DEPENDS remotify_libiconv
        SOURCE_DIR ${libidn2_SOURCE_DIR}
        BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/libidn2-build
        INSTALL_DIR ${REMOTIFY_LIBIDN2_INSTALL}
        CONFIGURE_COMMAND
            ${CMAKE_COMMAND} -E env
            CC=${CMAKE_C_COMPILER}
            ${REMOTIFY_DEPENDENCY_ENV}
            <SOURCE_DIR>/configure
            --prefix=<INSTALL_DIR>
            --disable-doc
            --disable-nls
            --disable-shared
            --enable-static
            --with-included-libunistring
            --with-libiconv-prefix=${REMOTIFY_LIBICONV_INSTALL}
        BUILD_COMMAND ${REMOTIFY_MAKE_EXECUTABLE}
        INSTALL_COMMAND ${REMOTIFY_MAKE_EXECUTABLE} install
        BUILD_BYPRODUCTS ${LIBIDN2_LIBRARY}
    )

    set(USE_LIBIDN2 ON CACHE BOOL "" FORCE)
else()
    # A cross build must never discover a build-host libidn2. The Remotify
    # service origins are ASCII and old Windows has no required IDN behavior.
    set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
    set(USE_WIN32_IDN OFF CACHE BOOL "" FORCE)
endif()

# remotifyd requires libcurl's WebSocket API, which is not enabled by every
# operating-system libcurl. Keep the exact source version beside the other
# vendored dependencies so every default build uses the same implementation.
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CURL_ENABLE_EXPORT_TARGET OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_DICT ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_ALTSVC ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_COOKIES ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_FILE ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_FTP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_GOPHER ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_HSTS ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_HTTP_AUTH ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_IMAP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_IPFS ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAPS ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_MIME ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_MQTT ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_NETRC ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_POP3 ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_RTSP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_SMB ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_SMTP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_TELNET ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_TFTP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_WEBSOCKETS OFF CACHE BOOL "" FORCE)
set(CURL_CA_BUNDLE "none" CACHE STRING "" FORCE)
set(CURL_CA_PATH "none" CACHE STRING "" FORCE)
set(CURL_ENABLE_SMB OFF CACHE BOOL "" FORCE)
set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
set(CURL_ZLIB OFF CACHE BOOL "" FORCE)
set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH OFF CACHE BOOL "" FORCE)
set(CURL_USE_PKGCONFIG OFF CACHE BOOL "" FORCE)
set(ENABLE_CURL_MANUAL OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
if(WIN32)
    # This socket-driven loop cannot receive the threaded resolver's Windows
    # completion wakeup, so resolve before curl publishes connection sockets.
    set(ENABLE_THREADED_RESOLVER OFF CACHE BOOL "" FORCE)
endif()
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_SCHANNEL OFF CACHE BOOL "" FORCE)
set(CURL_USE_MBEDTLS ON CACHE BOOL "" FORCE)
set(USE_NGHTTP2 OFF CACHE BOOL "" FORCE)

# Mbed TLS 3 requires Vista. XP therefore remains on the final 2.28 LTS
# release, while every other target uses the maintained 3.6 LTS branch.
if(REMOTIFY_USE_XP_TLS)
    FetchContent_Declare(
        mbedtls
        URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-2.28.10/mbedtls-2.28.10.tar.bz2
        URL_HASH SHA256=19e5b81fdac0fe22009b9e2bdcd52d7dcafbf62bc67fc59cf0a76b5b5540d149
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/mbedtls-2.28.10
    )
else()
    FetchContent_Declare(
        mbedtls
        URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2
        URL_HASH SHA256=a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/mbedtls-3.6.7
    )
endif()
set(MBEDTLS_USER_CONFIG_FILE
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/mbedtls_client.h
    CACHE FILEPATH "" FORCE
)
FetchContent_MakeAvailable(mbedtls)

if(REMOTIFY_USE_XP_TLS)
    target_compile_options(mbedcrypto PRIVATE
        -include
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/mbedtls_windows_xp.h
    )
endif()

set(MBEDTLS_INCLUDE_DIR ${mbedtls_SOURCE_DIR}/include CACHE PATH "" FORCE)
set(MBEDTLS_LIBRARY mbedtls CACHE STRING "" FORCE)
set(MBEDX509_LIBRARY mbedx509 CACHE STRING "" FORCE)
set(MBEDCRYPTO_LIBRARY mbedcrypto CACHE STRING "" FORCE)
set(MBEDTLS_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)

# curl 8.16 raised its Mbed TLS minimum to 3.2, which is newer than XP can run.
# Version 8.15 is the newest release that combines WebSocket support, XP, and
# Mbed TLS 2.28.
if(REMOTIFY_USE_XP_TLS)
    FetchContent_Declare(
        libcurl
        URL https://curl.se/download/curl-8.15.0.tar.xz
        URL_HASH SHA256=6cd0a8a5b126ddfda61c94dc2c3fc53481ba7a35461cf7c5ab66aa9d6775b609
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libcurl-8.15.0
    )
else()
    FetchContent_Declare(
        libcurl
        URL https://curl.se/download/curl-8.20.0.tar.xz
        URL_HASH SHA256=63fe2dc148ba0ceae89922ef838f7e5c946272c2e78b7c59fab4b79d3ce2b896
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libcurl-8.20.0
    )
endif()
FetchContent_MakeAvailable(libcurl)
set(BUILD_TESTING ${REMOTIFY_BUILD_TESTS} CACHE BOOL "Build tests" FORCE)

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
    src/random_xp.c
    src/random_vista.c
    src/random_unix.c
    src/executor.c
    src/executor_worker.c
    src/executor_worker_readloop.c
    src/executor_worker_handler.c
    src/executor_worker_exec.c
    src/executor_worker_exec_reader.c
    src/executor_worker_file_transfer.c
    src/executor_worker_patch.c
    src/executor_worker_patch_apply_exact.c
    src/executor_worker_patch_candidates.c
    src/executor_worker_patch_commit.c
    src/executor_worker_patch_format.c
    src/executor_worker_patch_git_path.c
    src/executor_worker_patch_json_unicode.c
    src/executor_worker_patch_model_escape.c
    src/executor_worker_patch_model_escape_document.c
    src/executor_worker_patch_normalize.c
    src/executor_worker_patch_parser.c
    src/executor_worker_patch_path.c
    src/executor_worker_patch_preview.c
    src/executor_worker_patch_preview_policy.c
    src/executor_worker_patch_source.c
    src/executor_worker_patch_source_fuzzy.c
    src/executor_worker_patch_source_fuzzy_block.c
    src/executor_worker_patch_source_fuzzy_character_merge.c
    src/executor_worker_patch_source_fuzzy_character_diff.c
    src/executor_worker_patch_source_fuzzy_match.c
    src/executor_worker_patch_source_fuzzy_permutation.c
    src/executor_worker_patch_source_fuzzy_document.c
    src/executor_worker_patch_source_fuzzy_evidence.c
    src/executor_worker_patch_source_fuzzy_provenance.c
    src/executor_worker_patch_source_fuzzy_presentation.c
    src/executor_worker_patch_source_fuzzy_raw.c
    src/executor_worker_patch_source_fuzzy_semantic.c
    src/executor_worker_patch_source_fuzzy_unicode.c
    src/executor_worker_patch_unicode.c
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

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    # CMake's Release mode does not remove symbols emitted by every static
    # dependency. Strip only the finished executable, after the linker has
    # completed LTO and section garbage collection.
    if(APPLE)
        add_custom_command(TARGET remotifyd POST_BUILD
            COMMAND ${CMAKE_STRIP} $<TARGET_FILE:remotifyd>
        )
    else()
        add_custom_command(TARGET remotifyd POST_BUILD
            COMMAND ${CMAKE_STRIP} --strip-all $<TARGET_FILE:remotifyd>
        )
    endif()
endif()

if(REMOTIFY_BUILD_TESTS)
    add_executable(remotifyd_patch_tests EXCLUDE_FROM_ALL
        tests/executor_worker_patch_test.c
        tests/executor_worker_patch_core_test.c
        tests/executor_worker_patch_fault.c
        tests/executor_worker_patch_stress.c
        tests/executor_worker_patch_test_support.c
        src/executor_worker_patch.c
        src/executor_worker_patch_apply_exact.c
        src/executor_worker_patch_candidates.c
        src/executor_worker_patch_commit.c
        src/executor_worker_patch_format.c
        src/executor_worker_patch_git_path.c
        src/executor_worker_patch_json_unicode.c
        src/executor_worker_patch_model_escape.c
        src/executor_worker_patch_model_escape_document.c
        src/executor_worker_patch_normalize.c
        src/executor_worker_patch_parser.c
        src/executor_worker_patch_path.c
        src/executor_worker_patch_preview.c
        src/executor_worker_patch_preview_policy.c
        src/executor_worker_patch_source.c
        src/executor_worker_patch_source_fuzzy.c
        src/executor_worker_patch_source_fuzzy_block.c
        src/executor_worker_patch_source_fuzzy_character_merge.c
        src/executor_worker_patch_source_fuzzy_character_diff.c
        src/executor_worker_patch_source_fuzzy_match.c
        src/executor_worker_patch_source_fuzzy_permutation.c
        src/executor_worker_patch_source_fuzzy_document.c
        src/executor_worker_patch_source_fuzzy_evidence.c
        src/executor_worker_patch_source_fuzzy_provenance.c
        src/executor_worker_patch_source_fuzzy_presentation.c
        src/executor_worker_patch_source_fuzzy_raw.c
        src/executor_worker_patch_source_fuzzy_semantic.c
        src/executor_worker_patch_source_fuzzy_unicode.c
        src/executor_worker_patch_unicode.c
        src/executor_worker_output.c
        src/logger.c
        src/random_xp.c
        src/random_vista.c
        src/random_unix.c
    )
    target_include_directories(remotifyd_patch_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_compile_definitions(remotifyd_patch_tests PRIVATE
        uv_fs_open=patch_test_uv_fs_open
        uv_fs_write=patch_test_uv_fs_write
        uv_fs_fsync=patch_test_uv_fs_fsync
        uv_fs_fstat=patch_test_uv_fs_fstat
        uv_fs_rename=patch_test_uv_fs_rename
        uv_fs_chmod=patch_test_uv_fs_chmod
        uv_fs_unlink=patch_test_uv_fs_unlink
    )
    target_link_libraries(remotifyd_patch_tests PRIVATE
        uv_a utf8proc remotify_grapheme_word
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(remotifyd_patch_tests PRIVATE _GNU_SOURCE)
    elseif(WIN32 AND REMOTIFY_TARGET STREQUAL "windowsxp")
        target_link_libraries(remotifyd_patch_tests PRIVATE advapi32)
    elseif(WIN32)
        target_link_libraries(remotifyd_patch_tests PRIVATE bcrypt)
    endif()
    add_test(NAME executor_worker_patch COMMAND remotifyd_patch_tests)
    add_test(NAME executor_worker_patch_stress COMMAND remotifyd_patch_tests --stress)
    add_test(NAME executor_worker_patch_fault_stress COMMAND remotifyd_patch_tests --fault-stress)
    set_tests_properties(executor_worker_patch_stress PROPERTIES TIMEOUT 300)
    set_tests_properties(executor_worker_patch_fault_stress PROPERTIES TIMEOUT 300)
endif()

# iconv.h is installed by the external build, so the daemon's compilation must
# not begin until that dependency has completed its install step.
add_dependencies(remotifyd remotify_libiconv)

target_include_directories(remotifyd PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    ${mbedtls_SOURCE_DIR}/include
)

target_link_libraries(remotifyd PRIVATE
    uv_a
    CURL::libcurl
    mbedcrypto
    remotify_iconv
    utf8proc
    remotify_grapheme_word
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(remotifyd PRIVATE _GNU_SOURCE)
    target_link_options(remotifyd PRIVATE -static)
endif()

if(WIN32)
    if(REMOTIFY_TARGET STREQUAL "windowsxp")
        target_link_libraries(remotifyd PRIVATE advapi32)
    else()
        target_link_libraries(remotifyd PRIVATE bcrypt)
    endif()
endif()
