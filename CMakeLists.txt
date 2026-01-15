cmake_minimum_required(VERSION 3.19)

if(NOT DEFINED GECND_PROJECT_NAME)
    set(GECND_PROJECT_NAME "core")
endif()
project("${GECND_PROJECT_NAME}" LANGUAGES C)
include(FetchContent)

# EXE
option(GECND_AS_LIBRARY "build static lib gecnd intead binary" OFF)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/bin")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/lib")
file(GLOB_RECURSE SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/lib/Frontend_*/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Common_Utils/*.c"
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
add_library(gecnd_lua INTERFACE)
add_library(gecnd_header INTERFACE)
if(NOT TARGET gecnd_cc_flags)
    add_library(gecnd_cc_flags INTERFACE)
endif()
target_include_directories(gecnd_header INTERFACE "${CMAKE_CURRENT_LIST_DIR}/include")
target_link_libraries(${PROJECT_NAME} PRIVATE gecnd_header gecnd_lua gecnd_cc_flags)
get_target_property(gecnd_cc_options gecnd_cc_flags INTERFACE_COMPILE_OPTIONS)
if(NOT gecnd_cc_options)
    set(gecnd_cc_options "")
endif()

# Lua 5.4
set(LUA54_VERSION "5.4.7")
set(LUA54_DIR "${CMAKE_SOURCE_DIR}/vendor/lua/lua54")
set(LUA54_DOWNLOAD "https://github.com/lua/lua/archive/refs/tags/v${LUA54_VERSION}.tar.gz")
set(LUA54_OPTMIZE FALSE)
if(NOT EXISTS "${LUA54_DIR}/lua")
    message("TODO: building a lua interpreter.")
endif()

# Lua 5.1
set(LUA51_VERSION "5.1")
set(LUA51_DIR "${CMAKE_SOURCE_DIR}/vendor/lua/lua51")
set(LUA51_DOWNLOAD "https://github.com/lua/lua/archive/refs/tags/v${LUA51_VERSION}.tar.gz")
option(GECND_USE_LUA51 "prefer lua 5.1 instead lua 5.4" OFF)

# Lua JIT
set(LUAJIT_VERSION "2.0.5")
set(LUAJIT_DIR "${CMAKE_SOURCE_DIR}/vendor/luajit/luajit")
set(LUAJIT_LIB "${CMAKE_SOURCE_DIR}/vendor/luajit/luajit/src/libluajit.a")
set(LUAJIT_DOWNLOAD "https://github.com/luajit/luajit/archive/refs/tags/v${LUAJIT_VERSION}.tar.gz")
option(GECND_USE_LUAJIT "prefer lua jit instead lua 5.4" OFF)

# Klib
set(KLIB_DIR "${CMAKE_SOURCE_DIR}/vendor/mono/klib")
set(KLIB_DOWNLOAD "https://github.com/attractivechaos/klib/archive/5c1451caa1ee476624d00eed71810532c89b82d1.tar.gz")
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${KLIB_DIR})

# STB
set(STB_DIR "${CMAKE_SOURCE_DIR}/vendor/mono/stb")
set(STB_DOWNLOAD "https://github.com/nothings/stb/archive/f1c79c02822848a9bed4315b12c8c8f3761e1296.tar.gz")
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${STB_DIR})

# Lua CJson
set(LUACJSON_VERSION "2.1.0.9")
set(LUACJSON_DIR "${CMAKE_SOURCE_DIR}/vendor/lua/cjson")
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

# SDL2
set(SDL_VERSION "2.30.6")
set(SDLTTF_VERSION "2.22.0")
set(SDL_DIR "${CMAKE_SOURCE_DIR}/vendor/SDL2")
set(SDLTTF_DIR "${CMAKE_SOURCE_DIR}/vendor/SDL_ttf")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDL2_TEST OFF CACHE BOOL "" FORCE)
set(SDL2_SHARED OFF CACHE BOOL "" FORCE)
set(SDL2_STATIC ON CACHE BOOL "" FORCE)
set(SDL2TTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDL2TTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
set(SDL2TTF_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL2TTF_SAMPLES OFF CACHE BOOL "" FORCE)
option(GECND_USE_SDL2 "Use SDL backend" OFF)

# FFMPEG
set(FFMPEG_VERSION "7.0.2")
set(FFMPEG_DOWNLOAD "https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n${FFMPEG_VERSION}.tar.gz")
set(FFMPEG_DIR "${CMAKE_SOURCE_DIR}/vendor/ffmpeg")
option(GECND_USE_FFMPEG "fetch ffmpeg" OFF)

# LibUV
set(LIBUV_VERSION "v1.48.0")
set(LIBUV_REPO "https://github.com/libuv/libuv.git")
set(LIBUV_DIR "${CMAKE_SOURCE_DIR}/vendor/libuv")
option(GECND_USE_LIBUV "fetch libuv" ON)

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

if(GECND_USE_LUA51)
    FetchContent_Populate(lua51-static URL "${LUA51_DOWNLOAD}" SOURCE_DIR ${LUA51_DIR})
    file(GLOB lua_files "${LUA51_DIR}/*.c")
    list(REMOVE_ITEM lua_files "${LUA51_DIR}/lua.c")
    add_library(lua51-static STATIC ${lua_files})
    target_link_libraries(${PROJECT_NAME} PRIVATE lua51-static)
    target_link_libraries(lua51-static PRIVATE gecnd_cc_flags)
    target_include_directories(gecnd_lua INTERFACE "${LUA51_DIR}")
endif()

if(GECND_USE_LUAJIT)
    FetchContent_Populate(dep_luajit URL "${LUAJIT_DOWNLOAD}" SOURCE_DIR ${LUAJIT_DIR})
    add_custom_command(
        OUTPUT ${LUAJIT_LIB}
        COMMAND make
        WORKING_DIRECTORY ${LUAJIT_DIR}
        COMMENT "Building LuaJIT (static)"
    )
    add_custom_target(
        luajit-build
        DEPENDS ${LUAJIT_LIB}
    )
    add_library(luajit-static STATIC IMPORTED GLOBAL)
    set_target_properties(luajit-static PROPERTIES
        IMPORTED_LOCATION ${LUAJIT_LIB}
        INTERFACE_INCLUDE_DIRECTORIES ${LUAJIT_DIR}/src
    )
    add_dependencies(luajit-static luajit-build)
    target_link_libraries(${PROJECT_NAME} PRIVATE luajit-static)
    target_include_directories(gecnd_lua INTERFACE "${LUAJIT_DIR}/src")
endif()

if(NOT GECND_USE_LUA51 AND NOT GECND_USE_LUAJIT)
    FetchContent_Populate(lua54-static URL "${LUA54_DOWNLOAD}" SOURCE_DIR ${LUA54_DIR})
    file(GLOB lua_files "${LUA54_DIR}/*.c")
    list(REMOVE_ITEM lua_files "${LUA54_DIR}/lua.c")
    list(REMOVE_ITEM lua_files "${LUA54_DIR}/onelua.c")
    add_library(lua54-static STATIC ${lua_files})
    target_link_libraries(${PROJECT_NAME} PRIVATE lua54-static)
    target_link_libraries(lua54-static PRIVATE gecnd_cc_flags)
    target_include_directories(gecnd_lua INTERFACE "${LUA54_DIR}")
endif()

if(NOT EXISTS "${KLIB_DIR}")
    FetchContent_Populate(klib URL "${KLIB_DOWNLOAD}" SOURCE_DIR ${KLIB_DIR})
endif()

if(NOT EXISTS "${STB_DIR}")
    FetchContent_Populate(stb URL "${STB_DOWNLOAD}" SOURCE_DIR ${STB_DIR})
endif()

if(GECND_USE_JSON)
    if(NOT EXISTS ${LUACJSON_DIR})
        FetchContent_Populate(lua_cjson URL ${LUACJSON_DOWNLOAD} SOURCE_DIR ${LUACJSON_DIR})
    endif()
    add_library(lua_cjson-static STATIC "${LUACJSON_DIR}/lua_cjson.c;${LUACJSON_DIR}/strbuf.c;${LUACJSON_DIR}/fpconv.c")
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/lib/ThirdParty_Api/json.c")
    target_link_libraries(${PROJECT_NAME} PRIVATE lua_cjson-static)
    target_link_libraries("lua_cjson-static" PRIVATE gecnd_cc_flags)
    target_link_libraries("lua_cjson-static" PRIVATE gecnd_lua)
endif()

if(GECND_USE_BASE64)
    if(NOT EXISTS ${BASE64_DIR})
        FetchContent_Populate(dep_libb64 URL ${BASE64_DOWNLOAD} SOURCE_DIR ${BASE64_DIR})
    endif()
    add_library(base64-static STATIC "${BASE64_DIR}/src/cencode.c" "${BASE64_DIR}/src/cdecode.c")
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/lib/ThirdParty_Api/base64.c")
    target_link_libraries(${PROJECT_NAME} PRIVATE base64-static)
    target_link_libraries("base64-static" PRIVATE gecnd_cc_flags)
    target_include_directories("base64-static" SYSTEM PRIVATE "${BASE64_DIR}/include")
    target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE "${BASE64_DIR}/include")
endif()

if(GECND_USE_SQLITE3)
    if(NOT EXISTS ${SQLITE_DIR})
        FetchContent_Populate(dep_sqlite3 URL ${SQLITE_DOWNLOAD} SOURCE_DIR ${SQLITE_DIR})
    endif()
    add_library(sqlite3-static STATIC "${SQLITE_DIR}/sqlite3.c")
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/lib/ThirdParty_Api/storage.c")
    target_link_libraries(${PROJECT_NAME} PRIVATE sqlite3-static)
    target_link_libraries("sqlite3-static" PRIVATE gecnd_cc_flags)
    target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${SQLITE_DIR})
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
    target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${RAYLIB_DIR}/include)
    #todo cross linking
endif()

if(GECND_USE_SDL2)
    if(NOT "${GECND_BACKEND}" STREQUAL "SDL2")
        message(FATAL_ERROR "You must define -DGECND_BACKEND=\"SDL2\" when running cmake")
    endif()
    FetchContent_Declare(
        dep_sdl2
        GIT_REPOSITORY https://github.com/libsdl-org/SDL
        GIT_TAG "release-${SDL_VERSION}"
        SOURCE_DIR ${SDL_DIR}
    )
    FetchContent_Declare(
        dep_sdl2_ttf
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf
        GIT_TAG "release-${SDLTTF_VERSION}"
        SOURCE_DIR ${SDLTTF_DIR}
    )
    FetchContent_MakeAvailable(dep_sdl2 dep_sdl2_ttf)
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2-static SDL2_ttf::SDL2_ttf-static)
    target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${SDLTTF_DIR};${SDL_DIR}/include)
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
    target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${FFMPEG_DIR})
endif()

if (GECND_USE_LIBUV)
    FetchContent_Declare(uv_a GIT_REPOSITORY ${LIBUV_REPO} GIT_TAG ${LIBUV_VERSION} SOURCE_DIR ${LIBUV_DIR})
    FetchContent_MakeAvailable(uv_a)
    target_link_libraries(${PROJECT_NAME} PRIVATE uv_a)
    target_compile_options(uv_a PRIVATE ${gecnd_cc_options})
endif()

if (GECND_USE_ZLIB)
    FetchContent_Declare(zlib GIT_REPOSITORY ${ZLIB_REPO} GIT_TAG ${ZLIB_VERSION} SOURCE_DIR ${ZLIB_DIR})
    FetchContent_MakeAvailable(zlib)
    target_link_libraries(${PROJECT_NAME} PRIVATE zlib)
    target_link_libraries(zlib PRIVATE gcnd_cc_flags)
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
        libcurl_static
        GIT_REPOSITORY https://github.com/curl/curl
        GIT_TAG curl-8_9_1
        SOURCE_DIR ${CURL_DIR}
    )
    FetchContent_MakeAvailable(libcurl_static)
    target_link_libraries(${PROJECT_NAME} PRIVATE libcurl_static)
    target_compile_options(libcurl_static PRIVATE ${gecnd_cc_options})
    target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${CURL_DIR}/include)
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
