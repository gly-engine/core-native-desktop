cmake_minimum_required(VERSION 3.19)

if(NOT DEFINED GECND_PROJECT_NAME)
    set(GECND_PROJECT_NAME "core")
endif()
project("${GECND_PROJECT_NAME}" LANGUAGES C)
include(FetchContent)

include_directories("${CMAKE_CURRENT_LIST_DIR}/inc")
include_directories("${CMAKE_SOURCE_DIR}/vendor/lua")
include_directories("${CMAKE_SOURCE_DIR}/vendor/klib")
include_directories("${CMAKE_SOURCE_DIR}/vendor/ffmpeg")
include_directories("${CMAKE_SOURCE_DIR}/vendor/sqlite3")
include_directories("${CMAKE_SOURCE_DIR}/vendor/hv/include")
include_directories("${CMAKE_SOURCE_DIR}/vendor/curl/include")
include_directories("${CMAKE_SOURCE_DIR}/vendor/raylib/include")
include_directories("${CMAKE_SOURCE_DIR}/vendor/libb64/include")

# EXE
option(GECND_AS_LIBRARY "build static lib gecnd intead binary" OFF)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/bin")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/lib")
file(GLOB_RECURSE SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/lib/Frontend_*/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Common_*/*.c"
)
if(GECND_AS_LIBRARY)
    add_library(${PROJECT_NAME} STATIC "${SOURCES}")
    target_compile_definitions(${PROJECT_NAME} PRIVATE "GLY_EMPTY_HOOKS")
else()
    if(NOT DEFINED GECND_ENTRYPOINT)
        if (GECND_USE_LIBUV)
            set(GECND_ENTRYPOINT "${CMAKE_CURRENT_LIST_DIR}/src/main_uv.c")
        else()
            set(GECND_ENTRYPOINT "${CMAKE_CURRENT_LIST_DIR}/src/main_basic.c")
        endif()
    endif()
    if(NOT DEFINED GECND_BACKEND)
        set(GECND_USE_RAYLIB TRUE CACHE BOOL "" FORCE)
        set(GECND_BACKEND "Raylib")
    endif()
    if(GECND_BACKEND)
        file(GLOB_RECURSE BACKEND_SOURCES "${CMAKE_CURRENT_LIST_DIR}/lib/Backend_${GECND_BACKEND}/*.c")
        set(SOURCES "${SOURCES};${BACKEND_SOURCES}")
    endif()
    add_executable(${PROJECT_NAME} "${GECND_ENTRYPOINT};${SOURCES}")
endif()

# Zig
set(ZIG_VERSION "0.13.0")
set(ZIG_DIR "${CMAKE_SOURCE_DIR}/vendor/zig")
set(ZIG_DOWNLOAD "http://ziglang.org/download")
string(TOLOWER ${CMAKE_HOST_SYSTEM_NAME} CMAKE_HOST_SYSTEM_NAME_LOWER)
if(${CMAKE_HOST_SYSTEM_NAME} STREQUAL "Darwin")
    set(ZIG_DOWNLOAD "${ZIG_DOWNLOAD}/${ZIG_VERSION}/zig-macos-${CMAKE_HOST_SYSTEM_PROCESSOR}-${ZIG_VERSION}.tar.xz")
elseif(${CMAKE_HOST_SYSTEM_NAME} STREQUAL "Linux")
    set(ZIG_DOWNLOAD "${ZIG_DOWNLOAD}/${ZIG_VERSION}/zig-linux-${CMAKE_HOST_SYSTEM_PROCESSOR}-${ZIG_VERSION}.tar.xz")
elseif(${CMAKE_HOST_SYSTEM_NAME} STREQUAL "Windows")
    set(ZIG_DOWNLOAD "${ZIG_DOWNLOAD}/${ZIG_VERSION}/zig-windows-${CMAKE_HOST_SYSTEM_PROCESSOR}-${ZIG_VERSION}.zip")
endif()
if(NOT EXISTS ${ZIG_DIR})
    FetchContent_Populate(dep_zig URL ${ZIG_DOWNLOAD} SOURCE_DIR ${ZIG_DIR})
endif()
find_program(ZIGC zig PATHS ${ZIG_DIR} REQUIRED NO_DEFAULT_PATH)
set(ZIGC "${ZIGC}" CACHE INTERNAL "ZIG COMPILER")

# Lua
set(LUA_VERSION "5.4.7")
set(LUA_DIR "${CMAKE_SOURCE_DIR}/vendor/lua")
set(LUA_DOWNLOAD "https://github.com/lua/lua/archive/refs/tags/v${LUA_VERSION}.tar.gz")
set(LUA_OPTMIZE FALSE)
if(NOT EXISTS "${LUA_DIR}/lua")
    set(LUA_OPTMIZE TRUE)
    FetchContent_Populate(dep_lua URL "${LUA_DOWNLOAD}" SOURCE_DIR ${LUA_DIR})
    execute_process(
        WORKING_DIRECTORY ${LUA_DIR}
        COMMAND ${ZIGC} cc -DMAKE_LUA onelua.c -o lua
            -target ${CMAKE_HOST_SYSTEM_PROCESSOR}-${CMAKE_HOST_SYSTEM_NAME_LOWER}
    )
endif()
file(GLOB lua_files "${LUA_DIR}/*.c")
list(REMOVE_ITEM lua_files "${LUA_DIR}/lua.c")
list(REMOVE_ITEM lua_files "${LUA_DIR}/onelua.c")
add_library(lua-static STATIC ${lua_files})
find_program(LUAC lua PATHS ${LUA_DIR} REQUIRED NO_DEFAULT_PATH)
target_link_libraries(${PROJECT_NAME} PRIVATE lua-static)
target_compile_options("lua-static" PRIVATE "${GECND_CC_FLAGS}")
set(LUAC "${LUAC}" CACHE INTERNAL "LUA COMPILER")

# Klib
set(KLIB_DIR "${CMAKE_SOURCE_DIR}/vendor/klib")
set(KLIB_DOWNLOAD "https://github.com/attractivechaos/klib/archive/5c1451caa1ee476624d00eed71810532c89b82d1.tar.gz")
if(NOT EXISTS "${KLIB_DIR}")
    FetchContent_Populate(dep_klib URL "${KLIB_DOWNLOAD}" SOURCE_DIR ${KLIB_DIR})
endif()

# Lua CJson
set(LUACJSON_VERSION "2.1.0.9")
set(LUACJSON_DIR "${CMAKE_SOURCE_DIR}/vendor/lua_cjson")
set(LUACJSON_DOWNLOAD "https://github.com/openresty/lua-cjson/archive/refs/tags/${LUACJSON_VERSION}.tar.gz")
option(GECND_USE_JSON "fetch lua cjson" ON)

# Lua Base 64
set(BASE64_VERSION "2.0.0.1")
set(BASE64_DIR "${CMAKE_SOURCE_DIR}/vendor/libb64")
set(BASE64_DOWNLOAD "https://github.com/libb64/libb64/archive/refs/tags/v${BASE64_VERSION}.tar.gz")
option(GECND_USE_BASE64 "fetch base64" ON)

# SQLite3
set(SQLITE_VERSION "3410200") # exemplo: 3.41.2
set(SQLITE_DIR "${CMAKE_SOURCE_DIR}/vendor/sqlite3")
set(SQLITE_DOWNLOAD "https://www.sqlite.org/2023/sqlite-amalgamation-${SQLITE_VERSION}.zip")
option(GECND_USE_SQLITE3 "Fetch and build sqlite3" ON)

# Raylib
set(RAYLIB_VERSION "5.5")
set(RAYLIB_DIR "${CMAKE_SOURCE_DIR}/vendor/raylib")
set(RAYLIB_DOWNLOAD "https://github.com/raysan5/raylib/releases/download/${RAYLIB_VERSION}/raylib-${RAYLIB_VERSION}_linux_amd64.tar.gz")
option(GECND_USE_RAYLIB "Use Raylib backend" OFF)

# FFMPEG
set(FFMPEG_VERSION "7.0.2")
set(FFMPEG_DOWNLOAD "https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n${FFMPEG_VERSION}.tar.gz")
set(FFMPEG_DIR "${CMAKE_SOURCE_DIR}/vendor/ffmpeg")
option(GECND_USE_FFMPEG "fetch ffmpeg" OFF)

# LibUV
set(LIBUV_VERSION "v1.48.0")
set(LIBUV_REPO "https://github.com/libuv/libuv.git")
set(LIBUV_DIR "${CMAKE_SOURCE_DIR}/vendor/libuv")
option(GECND_USE_LIBUV "fetch libuv" OFF)

# Zlib
set(ZLIB_VERSION "v1.3.1")
set(ZLIB_REPO "https://github.com/madler/zlib.git")
set(ZLIB_DIR "${CMAKE_SOURCE_DIR}/vendor/zlib")
option(GECND_USE_ZLIB "fetch zlib" OFF)

# Bear SSL
set(BEARSSL_VERSION "0.6")
set(BEARSSL_DIR "${CMAKE_SOURCE_DIR}/vendor/bearssl")
set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
set(BEARSSL_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/vendor/bearssl/inc")
set(CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/vendor/bearssl/build;${CMAKE_PREFIX_PATH}")
option(GECND_USE_BEARSSL "use curl with bear ssl" ON)

# Curl
set(CURL_VERSION "8_9_1")
set(CURL_DIR "${CMAKE_SOURCE_DIR}/vendor/curl")
set(HTTP_ONLY ON CACHE BOOL "" FORCE)
set(PICKY_COMPILER OFF CACHE BOOL "" FORCE)
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_STATIC_CURL OFF CACHE BOOL "" FORCE)
set(ENABLE_ARES OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_TESTS ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
set(ENABLE_CURL_MANUAL OFF CACHE BOOL "" FORCE)
set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_BEARSSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
option(GECND_USE_CURL "use curl" ON)

# Mock
option(GECND_MOCK_HTTP "add package http but raise error on request" OFF)

if(GECND_USE_JSON)
    if(NOT EXISTS ${LUACJSON_DIR})
        FetchContent_Populate(dep_cjson_lua URL ${LUACJSON_DOWNLOAD} SOURCE_DIR ${LUACJSON_DIR})
    endif()
    add_library(lua_cjson-static STATIC "${LUACJSON_DIR}/lua_cjson.c;${LUACJSON_DIR}/strbuf.c;${LUACJSON_DIR}/fpconv.c")
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/lib/ThirdParty_Api/json.c")
    target_link_libraries(${PROJECT_NAME} PRIVATE lua_cjson-static)
    target_compile_options("lua_cjson-static" PRIVATE "${GECND_CC_FLAGS}")
endif()

if(GECND_USE_BASE64)
    if(NOT EXISTS ${BASE64_DIR})
        FetchContent_Populate(dep_libb64 URL ${BASE64_DOWNLOAD} SOURCE_DIR ${BASE64_DIR})
    endif()
    add_library(base64-static STATIC "${BASE64_DIR}/src/cencode.c" "${BASE64_DIR}/src/cdecode.c")
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/lib/ThirdParty_Api/base64.c")
    target_link_libraries(${PROJECT_NAME} PRIVATE base64-static)
    target_compile_options("base64-static" PRIVATE "${GECND_CC_FLAGS}")
endif()

if(GECND_USE_SQLITE3)
    if(NOT EXISTS ${SQLITE_DIR})
        FetchContent_Populate(dep_sqlite3 URL ${SQLITE_DOWNLOAD} SOURCE_DIR ${SQLITE_DIR})
    endif()
    add_library(sqlite3-static STATIC "${SQLITE_DIR}/sqlite3.c")
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/lib/ThirdParty_Api/storage.c")
    target_link_libraries(${PROJECT_NAME} PRIVATE sqlite3-static)
    target_compile_options("sqlite3-static" PRIVATE "${GECND_CC_FLAGS}")
endif()

if(GECND_USE_RAYLIB)
    if(NOT "${GECND_BACKEND}" STREQUAL "Raylib")
        message(FATAL_ERROR "You must define -DGECND_BACKEND=\"Raylib\" when running cmake")
    endif()
    if(NOT EXISTS "${RAYLIB_DIR}")
        FetchContent_Populate(dep_raylib URL "${RAYLIB_DOWNLOAD}" SOURCE_DIR ${RAYLIB_DIR})
    endif()
    add_library(raylib-static STATIC IMPORTED)
    set_target_properties(raylib-static PROPERTIES IMPORTED_LOCATION "${RAYLIB_DIR}/lib/libraylib.a")
    target_link_libraries(${PROJECT_NAME} PRIVATE raylib-static)
endif()

if(GECND_USE_FFMPEG)
    if (NOT EXISTS "${FFMPEG_DIR}/libavcodec/libavcodec.a")
        FetchContent_Populate(dep_ffmpeg URL ${FFMPEG_DOWNLOAD} SOURCE_DIR ${FFMPEG_DIR})
        execute_process(
            COMMAND ./configure --prefix=${FFMPEG_DIR}/build --enable-static --disable-shared
            --disable-bzlib --disable-lzma --disable-x86asm --disable-libdrm
            WORKING_DIRECTORY ${FFMPEG_DIR}
            COMMAND_ERROR_IS_FATAL ANY
        )
        execute_process(
            COMMAND ${CMAKE_MAKE_PROGRAM} -j ${J}
            WORKING_DIRECTORY ${FFMPEG_DIR}
            COMMAND_ERROR_IS_FATAL ANY
        )
    endif()
    FILE(GLOB ffmpeg_files "${FFMPEG_DIR}/*/*.a")
    target_link_libraries(${PROJECT_NAME} PRIVATE "${ffmpeg_files}")
endif()

if (GECND_USE_LIBUV)
    FetchContent_Declare(libuv GIT_REPOSITORY ${LIBUV_REPO} GIT_TAG ${LIBUV_VERSION} SOURCE_DIR ${LIBUV_DIR})
    FetchContent_MakeAvailable(libuv)
    target_link_libraries(${PROJECT_NAME} PRIVATE uv_a)
endif()

if (GECND_USE_LIBUV)
    FetchContent_Declare(zlib GIT_REPOSITORY ${ZLIB_REPO} GIT_TAG ${ZLIB_VERSION} SOURCE_DIR ${ZLIB_DIR})
    FetchContent_MakeAvailable(zlib)
    target_link_libraries(${PROJECT_NAME} PRIVATE zlib)
    target_compile_options("zlib" PRIVATE "${GECND_CC_FLAGS}")
endif()

if(GECND_USE_CURL)
    if(GECND_USE_BEARSSL)
        set(CURL_USE_BEARSSL ON CACHE BOOL "" FORCE)
        FetchContent_Declare(
            dep_bearssl
            GIT_REPOSITORY https://www.bearssl.org/git/BearSSL
            GIT_TAG "v${BEARSSL_VERSION}"
            SOURCE_DIR ${BEARSSL_DIR}
        )
        FetchContent_MakeAvailable(dep_bearssl)
        if (NOT EXISTS "${BEARSSL_DIR}/build/libbearssl.a")
            execute_process(
                COMMAND ${CMAKE_MAKE_PROGRAM} build/libbearssl.a -j ${J}
                WORKING_DIRECTORY ${BEARSSL_DIR}
                COMMAND_ERROR_IS_FATAL ANY
            )
        endif()
    endif()
    FetchContent_Declare(
        dep_curl
        GIT_REPOSITORY https://github.com/curl/curl
        GIT_TAG curl-8_9_1
        SOURCE_DIR ${CURL_DIR}
    )
    FetchContent_MakeAvailable(dep_curl)
    target_link_libraries(${PROJECT_NAME} PRIVATE CURL::libcurl)
    if(GECND_USE_LIBUV)
        target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/lib/ThirdParty_Api/http_curl_uv.c")
    else()
        target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/lib/ThirdParty_Api/http_curl.c")
    endif()
endif()

if(GECND_MOCK_HTTP)
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/lib/ThirdParty_Api/http_mock.c")
endif()


set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set_target_properties(${PROJECT_NAME} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
target_link_libraries(${PROJECT_NAME} PRIVATE m)
