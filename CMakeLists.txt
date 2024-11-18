cmake_minimum_required(VERSION 3.19)
set(CMAKE_C_COMPILER_WORKS TRUE)
set(CMAKE_CXX_COMPILER_WORKS TRUE)

project(game C)
include(FetchContent)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CROSSCOMPILING TRUE)
set(CMAKE_SYSTEM_NAME "Generic")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
include_directories("${CMAKE_SOURCE_DIR}/src")
include_directories("${CMAKE_SOURCE_DIR}/vendor")
include_directories("${CMAKE_SOURCE_DIR}/vendor/ffmpeg")

# Paralelism in cmake stage
if(NOT DEFINED J)
    set(J, 1)
endif()

# Zig
set(ZIG_VERSION "0.13.0")
set(ZIG_DIR "${CMAKE_SOURCE_DIR}/vendor/zig")
set(ZIG_DOWNLOAD "http://ziglang.org/download")
set(ZIG_CC "${ZIG_DIR}/zig_cc")
set(ZIG_CXX "${ZIG_DIR}/zig_cxx")
#set(CMAKE_C_COMPILER ${ZIG_CC} CACHE BOOL "" FORCE) -- TODO: make work
#set(CMAKE_CXX_COMPILER ${ZIG_CXX} CACHE BOOL "" FORCE)
if(${CMAKE_HOST_SYSTEM_NAME} STREQUAL "Darwin")
    set(ZIG_DOWNLOAD "${ZIG_DOWNLOAD}/${ZIG_VERSION}/zig-macos-${CMAKE_HOST_SYSTEM_PROCESSOR}-${ZIG_VERSION}.tar.xz")
elseif(${CMAKE_HOST_SYSTEM_NAME} STREQUAL "Linux")
    set(ZIG_DOWNLOAD "${ZIG_DOWNLOAD}/${ZIG_VERSION}/zig-linux-${CMAKE_HOST_SYSTEM_PROCESSOR}-${ZIG_VERSION}.tar.xz")
elseif(${CMAKE_HOST_SYSTEM_NAME} STREQUAL "Windows")
    add_compile_options("-DNOT_USE_GETOPT")
    set(ZIG_DOWNLOAD "${ZIG_DOWNLOAD}/${ZIG_VERSION}/zig-windows-${CMAKE_HOST_SYSTEM_PROCESSOR}-${ZIG_VERSION}.zip")
endif()
if(NOT EXISTS ${ZIG_CC})
    FetchContent_Populate(dep_zig URL ${ZIG_DOWNLOAD} SOURCE_DIR ${ZIG_DIR})
    file(WRITE ${ZIG_CC} "#!/bin/sh\n${ZIG_DIR}/zig cc \$@\n")
    file(WRITE ${ZIG_CXX} "#!/bin/sh\n${ZIG_DIR}/zig c++ \$@\n")
    file(CHMOD ${ZIG_CC} FILE_PERMISSIONS OWNER_EXECUTE OWNER_READ)
    file(CHMOD ${ZIG_CXX} FILE_PERMISSIONS OWNER_EXECUTE OWNER_READ)
endif()
find_program(ZIGC zig PATHS ${ZIG_DIR} REQUIRED NO_DEFAULT_PATH)

# Lua
set(LUA_VERSION "5.4.7")
set(LUA_DIR "${CMAKE_SOURCE_DIR}/vendor/lua")
set(LUA_DOWNLOAD "https://github.com/lua/lua/archive/refs/tags/v${LUA_VERSION}.tar.gz")
if(NOT EXISTS ${LUA_DIR})
    FetchContent_Populate(dep_lua URL "${LUA_DOWNLOAD}" SOURCE_DIR ${LUA_DIR})
    execute_process(
        COMMAND ${CMAKE_MAKE_PROGRAM} CC=${ZIG_DIR}/zig_cc MYCFLAGS="-std=c99" MYLIBS="" -j ${J}
        WORKING_DIRECTORY ${LUA_DIR}
    )
endif()
file(GLOB lua_files "${LUA_DIR}/*.c")
list(REMOVE_ITEM lua_files "${LUA_DIR}/lua.c")
list(REMOVE_ITEM lua_files "${LUA_DIR}/onelua.c")
add_library(lua-static STATIC ${lua_files})
find_program(LUAC lua PATHS ${LUA_DIR} REQUIRED NO_DEFAULT_PATH)

# Engine
set(ENGINE_VERSION "0.0.12")
set(ENGINE_DIR "${CMAKE_SOURCE_DIR}/vendor/engine")
set(ENGINE_CLI "${ENGINE_DIR}/src/cli/main.lua")
set(ENGINE_DOWNLOAD "https://github.com/gamelly/gly-engine/archive/refs/tags/${ENGINE_VERSION}.tar.gz")
set(ENGINE_HEADER "${ENGINE_DIR}/bytecode.h")
if(NOT EXISTS ${ENGINE_HEADER})
    FetchContent_Populate(dep_engine URL ${ENGINE_DOWNLOAD} SOURCE_DIR ${ENGINE_DIR})
    file(MAKE_DIRECTORY "${ENGINE_DIR}/lite")
    file(MAKE_DIRECTORY "${ENGINE_DIR}/native")
    file(MAKE_DIRECTORY "${ENGINE_DIR}/examples/template")
    execute_process(
        WORKING_DIRECTORY ${ENGINE_DIR}
        COMMAND ${LUAC} ${ENGINE_CLI} build --core lite --bundler --dist ${ENGINE_DIR}/lite/
        COMMAND_ERROR_IS_FATAL ANY
    )
    execute_process(
        WORKING_DIRECTORY ${ENGINE_DIR}
        COMMAND ${LUAC} ${ENGINE_CLI} build --core native --bundler --dist ${ENGINE_DIR}/native/
        COMMAND_ERROR_IS_FATAL ANY
    )
    execute_process(
        WORKING_DIRECTORY ${ENGINE_DIR}
        COMMAND ${LUAC} ${ENGINE_CLI} tool-template-fill ${ENGINE_DIR}/examples/template/game.lua 204800
    )
    set(ENGINE "@native")
endif()
if(DEFINED ENGINE)
    if (ENGINE MATCHES "^\\@")
        string(SUBSTRING "${ENGINE}" 1 -1 ENGINE)
        set(ENGINE "${ENGINE_DIR}/${ENGINE}/main.lua")
    endif()
    if(NOT EXISTS ${ENGINE})
        message(FATAL_ERROR "engine not found: ${ENGINE}")
    endif()
    get_filename_component(ENGINE "${ENGINE}" ABSOLUTE)
    execute_process(
        WORKING_DIRECTORY ${ENGINE_DIR}
        COMMAND ${LUAC} ${ENGINE_CLI} fs-xxd-i ${ENGINE} ${ENGINE_HEADER} --const --name engine_bytecode_lua
        COMMAND_ERROR_IS_FATAL ANY
    )
endif()

# Lua CJson
set(LUACJSON_VERSION "2.1.0.9")
set(LUACJSON_DIR "${CMAKE_SOURCE_DIR}/vendor/lua_cjson")
set(LUACJSON_FILE "${LUACJSON_DIR}/lua_cjson.c")
set(LUACJSON_DOWNLOAD "https://github.com/openresty/lua-cjson/archive/refs/tags/${LUACJSON_VERSION}.tar.gz")
if(NOT EXISTS ${LUACJSON_FILE})
    FetchContent_Populate(dep_cjson_lua URL ${LUACJSON_DOWNLOAD} SOURCE_DIR ${LUACJSON_DIR})
    execute_process(
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/vendor/engine"
        COMMAND ${LUAC} ${ENGINE_CLI} fs-replace ${LUACJSON_FILE} ${LUACJSON_FILE}
            --format "<lua.h>" --replace "\"lua/lua.h\""
        COMMAND_ERROR_IS_FATAL ANY
    )
    execute_process(
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/vendor/engine"
        COMMAND ${LUAC} ${ENGINE_CLI} fs-replace ${LUACJSON_FILE} ${LUACJSON_FILE}
            --format "<lauxlib.h>" --replace "\"lua/lauxlib.h\""
        COMMAND_ERROR_IS_FATAL ANY
    )
endif()
add_library(lua_cjson-static STATIC "${LUACJSON_DIR}/lua_cjson.c;${LUACJSON_DIR}/strbuf.c;${LUACJSON_DIR}/fpconv.c")

# Font
set(FONT_DIR "${CMAKE_SOURCE_DIR}/vendor/font")
set(FONT_DOWNLOAD "https://github.com/gamelly/fonts/archive/refs/heads/main.tar.gz")
if(NOT EXISTS "${FONT_DIR}")
    FetchContent_Populate(dep_font URL ${FONT_DOWNLOAD} SOURCE_DIR ${FONT_DIR})
    set(FONT_NOTO "${FONT_DIR}/Noto_Sans")
    execute_process(
    WORKING_DIRECTORY ${ENGINE_DIR}
        COMMAND ${LUAC} ${ENGINE_CLI} fs-xxd-i --const --name notosans_regular_ttf
            ${FONT_NOTO}/NotoSans-Regular.ttf
            ${FONT_NOTO}/notosans_regular.h 
        COMMAND_ERROR_IS_FATAL ANY
    )
endif()

# Game
string(LENGTH "${GAME}" GAME_LENGTH)
set(GAME_HEADER "${CMAKE_SOURCE_DIR}/vendor/game/bytecode.h")
if (DEFINED GAME AND GAME_LENGTH AND GAME MATCHES "^\\@")
    string(SUBSTRING "${GAME}" 1 -1 GAME)
    set(GAME "${ENGINE_DIR}/examples/${GAME}/game.lua")
endif()
if ((NOT DEFINED GAME AND NOT EXISTS ${GAME_HEADER}) OR (DEFINED GAME AND NOT GAME_LENGTH))
    file(WRITE ${GAME_HEADER} "unsigned char* game_bytecode_lua = (char*) 0u;\nunsigned int game_bytecode_lua_len = 0;")
endif()
if(DEFINED GAME AND GAME_LENGTH)
    if(NOT EXISTS ${GAME})
        message(FATAL_ERROR "game not found: ${GAME}")
    endif()
    get_filename_component(GAME "${GAME}" ABSOLUTE)
    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/vendor/game")
    execute_process(
        WORKING_DIRECTORY ${ENGINE_DIR}
        COMMAND ${LUAC} src/cli/main.lua fs-xxd-i ${GAME} ${GAME_HEADER} --const --name game_bytecode_lua
        COMMAND_ERROR_IS_FATAL ANY
    )
endif()

# SDL
set(SDL_VERSION "2.30.6")
set(SDLTTF_VERSION "2.22.0")
set(SDL_DIR "${CMAKE_SOURCE_DIR}/vendor/SDL")
set(SDLTTF_DIR "${CMAKE_SOURCE_DIR}/vendor/SDL_ttf")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDL2_TEST OFF CACHE BOOL "" FORCE)
set(SDL2_SHARED OFF CACHE BOOL "" FORCE)
set(SDL2_STATIC ON CACHE BOOL "" FORCE)
set(SDL2TTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDL2TTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
set(SDL2TTF_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL2TTF_SAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    dep_sdl2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL
    GIT_TAG "release-${SDL_VERSION}"
    SOURCE_DIR ${SDL_DIR}
)
FetchContent_Declare(
    dep_ttf_sdl2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf
    GIT_TAG "release-${SDLTTF_VERSION}"
    SOURCE_DIR ${SDLTTF_DIR}
)
FetchContent_MakeAvailable(dep_sdl2 dep_ttf_sdl2)

# Bear SSL
set(BEARSSL_VERSION "0.6")
set(BEARSSL_DIR "${CMAKE_SOURCE_DIR}/vendor/bearssl")
set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
set(BEARSSL_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/vendor/bearssl/inc")
set(CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/vendor/bearssl/build;${CMAKE_PREFIX_PATH}")
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
set(CURL_DISABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_BEARSSL ON CACHE BOOL "" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    dep_curl
    GIT_REPOSITORY https://github.com/curl/curl
    GIT_TAG curl-8_9_1
    SOURCE_DIR ${CURL_DIR}
)
FetchContent_MakeAvailable(dep_curl)

# FFMPEG
set(FFMPEG_VERSION "7.0.2")
set(FFMPEG_DOWNLOAD "https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n${FFMPEG_VERSION}.tar.gz")
set(FFMPEG_DIR "${CMAKE_SOURCE_DIR}/vendor/ffmpeg")
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
#add_library(ffmpeg-static STATIC IMPORTED)
#set_target_properties(ffmpeg-static PROPERTIES IMPORTED_LOCATION ${ffmpeg_files})
#set(ffmpeg_files "${FFMPEG_DIR}/libavcodec/libavcodec.a;${FFMPEG_DIR}/libavformat/libavformat.a;${FFMPEG_DIR}/libavutil/libavutil.a;${FFMPEG_DIR}/libswscale/libswscale.a")

FILE(GLOB engine_files "${CMAKE_SOURCE_DIR}/src/*.c")
add_executable(${PROJECT_NAME} ${engine_files})
target_link_libraries(${PROJECT_NAME} PRIVATE lua-static)
target_link_libraries(${PROJECT_NAME} PRIVATE lua_cjson-static)
target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2-static)
target_link_libraries(${PROJECT_NAME} PRIVATE SDL2_ttf::SDL2_ttf-static)
target_link_libraries(${PROJECT_NAME} PRIVATE CURL::libcurl)
target_link_libraries(${PROJECT_NAME} PRIVATE "${ffmpeg_files}")
