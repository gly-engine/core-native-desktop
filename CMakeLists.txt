cmake_minimum_required(VERSION 3.19)

project(core LANGUAGES C)
include(FetchContent)

include_directories("${CMAKE_SOURCE_DIR}/inc")
include_directories("${CMAKE_SOURCE_DIR}/lib/Common_Utils")
include_directories("${CMAKE_SOURCE_DIR}/vendor/lua")
include_directories("${CMAKE_SOURCE_DIR}/vendor/ffmpeg")
include_directories("${CMAKE_SOURCE_DIR}/vendor/raylib/include")

# EXE
set(ENTRYPOINT "src/main_basic.c")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
file(GLOB_RECURSE SOURCES "lib/Frontend_*/*.c" "lib/Common_*/*.c")
if(BACKEND)
    file(GLOB_RECURSE BACKEND_SOURCES "lib/Backend_${BACKEND}/*.c")
    set(SOURCES "${SOURCES};${BACKEND_SOURCES}")
endif()
add_executable(${PROJECT_NAME} "${ENTRYPOINT};${SOURCES}")

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
set(LUAC "${LUAC}" CACHE INTERNAL "LUA COMPILER")

# Raylib
set(RAYLIB_VERSION "5.5")
set(RAYLIB_DIR "${CMAKE_SOURCE_DIR}/vendor/raylib")
set(RAYLIB_DOWNLOAD "https://github.com/raysan5/raylib/releases/download/${RAYLIB_VERSION}/raylib-${RAYLIB_VERSION}_linux_amd64.tar.gz")
option(GECND_USE_RAYLIB "Use Raylib backend" ON)
if(GECND_USE_RAYLIB)
    if(NOT "${BACKEND}" STREQUAL "Raylib")
        message(FATAL_ERROR "You must define -DBACKEND=\"Raylib\" when running cmake")
    endif()
    if(NOT EXISTS "${RAYLIB_DIR}")
        FetchContent_Populate(dep_raylib URL "${RAYLIB_DOWNLOAD}" SOURCE_DIR ${RAYLIB_DIR})
    endif()
    add_library(raylib-static STATIC IMPORTED)
    set_target_properties(raylib-static PROPERTIES IMPORTED_LOCATION "${RAYLIB_DIR}/lib/libraylib.a")
    target_link_libraries(${PROJECT_NAME} PRIVATE raylib-static)
endif()

# FFMPEG
set(FFMPEG_VERSION "7.0.2")
set(FFMPEG_DOWNLOAD "https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n${FFMPEG_VERSION}.tar.gz")
set(FFMPEG_DIR "${CMAKE_SOURCE_DIR}/vendor/ffmpeg")
option(GECND_USE_FFMPEG "fetch ffmpeg" OFF)
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

# LibUV
set(LIBUV_VERSION "v1.48.0")
set(LIBUV_REPO "https://github.com/libuv/libuv.git")
set(LIBUV_DIR "${CMAKE_SOURCE_DIR}/vendor/libuv")
option(GECND_USE_LIBUV "fetch libuv" OFF)
if (GECND_USE_LIBUV)
    FetchContent_Declare(libuv GIT_REPOSITORY ${LIBUV_REPO} GIT_TAG ${LIBUV_VERSION} SOURCE_DIR ${LIBUV_DIR})
    FetchContent_MakeAvailable(libuv)
    target_link_libraries(${PROJECT_NAME} PRIVATE uv_a)
endif()

# Zlib
set(ZLIB_VERSION "v1.3.1")
set(ZLIB_REPO "https://github.com/madler/zlib.git")
set(ZLIB_DIR "${CMAKE_SOURCE_DIR}/vendor/zlib")
option(GECND_USE_ZLIB "fetch zlib" OFF)
if (GECND_USE_LIBUV)
    FetchContent_Declare(zlib GIT_REPOSITORY ${ZLIB_REPO} GIT_TAG ${ZLIB_VERSION} SOURCE_DIR ${ZLIB_DIR})
    FetchContent_MakeAvailable(zlib)
    target_link_libraries(${PROJECT_NAME} PRIVATE zlib)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
target_link_libraries(${PROJECT_NAME} PRIVATE m)
