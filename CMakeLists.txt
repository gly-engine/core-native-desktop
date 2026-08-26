cmake_minimum_required(VERSION 3.19)

if(DEFINED TARGET)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/zig-toolchain.cmake" CACHE STRING "zig cross compiler")
endif()
if(NOT DEFINED GECND_PROJECT_NAME)
    set(GECND_PROJECT_NAME "core")
endif()
project("${GECND_PROJECT_NAME}" LANGUAGES C)
include(FetchContent)
include(ExternalProject)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_SKIP_INSTALL_RULES ON CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ENABLE_SHARED OFF CACHE BOOL "" FORCE)
set(ENABLE_STATIC ON  CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)

option(GECND_COMPAT "build to old libc devices" OFF)
option(GECND_AS_LIBRARY "build static lib gecnd intead binary" OFF)
option(GECND_USE_PROFILER "use google gperftools" OFF)
option(GECND_USE_GL_EGL "add opengl backend with egl context" ON)
option(GECND_USE_GL_SDL2 "add opengl backend with sdl2 context" OFF)
option(GECND_USE_GL_GLFW "add opengl backend with glfw context" ON)
option(GECND_USE_GL_WAYLAND "add opengl backend with wayland context" OFF)
option(GECND_USE_LUA51_32BITS "prefer lua 5.1 (integer only) intead lua 5.4" OFF)
option(GECND_USE_LUA51 "prefer lua 5.1 instead lua 5.4" OFF)
option(GECND_USE_LUAJIT "prefer lua jit instead lua 5.4" OFF)
option(GECND_USE_LUAROBLOX "prefer luau instead lua 5.4" OFF)
option(GECND_USE_LUA_CJSON "add cjson library to lua" ON)
option(GECND_USE_FFI "fetch and build ffi (needs: autoconf, automake, libtool)" OFF)
option(GECND_USE_FFMPEG "fetch ffmpeg for dyn linking" ON)
option(GECND_USE_FFMPEG4 "fetch ffmpeg 4 instead 7" OFF)
option(GECND_BUILD_FFMPEG "build static ffmpeg" OFF)
option(GECND_USE_JPEGTURBO "use libjpeg-turbo to decode jpeg straight to planar YUV" ON)
option(GECND_USE_WUFFS "use wuffs for fast PNG decode" ON)
option(GECND_USE_QRCODE "fetch qrcodegen" ON)
option(GECND_USE_WARMCAT "use warmcat to http/webosocket client/server" ON)
option(GECND_USE_MBEDTLS "use mbedtls backend for ssl" ON)
option(GECND_USE_SQLITE3 "fetch and build sqlite3" ON)
option(GECND_USE_LIBUV "fetch and build libuv" ON)
option(GECND_USE_ZLIB "fetch and build zlib" ON)
option(GECND_USE_PLUGIN_LIBRETRO "build plugin libretro for core native" OFF)

# EXE
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/include/gecnd)
file(GLOB_RECURSE SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/lib/Frontend_*/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Common_*/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_*/service_*.c"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_*/driver_easy_*.c"
)
if(GECND_AS_LIBRARY)
    add_library(${PROJECT_NAME} STATIC "${SOURCES}")
    target_compile_definitions(${PROJECT_NAME} PRIVATE "GLY_EMPTY_HOOKS")
else()
    add_executable(${PROJECT_NAME} "${SOURCES}")
endif()
add_library(gecnd_header INTERFACE)
if(NOT TARGET gecnd_cc_flags)
    add_library(gecnd_cc_flags INTERFACE)
endif()
target_include_directories(gecnd_header INTERFACE "${CMAKE_CURRENT_LIST_DIR}/include;${CMAKE_BINARY_DIR}/include")
target_link_libraries(${PROJECT_NAME} PRIVATE gecnd_header gecnd_cc_flags)
if(LINUX)
    file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_*/driver_linux_*.c")
    target_sources(${PROJECT_NAME} PRIVATE "${SOURCES}")
endif()

if(GECND_USE_PROFILER)
    target_compile_options(gecnd_cc_flags INTERFACE -g -O1 -fno-omit-frame-pointer)
endif()

get_target_property(gecnd_cc_options gecnd_cc_flags INTERFACE_COMPILE_OPTIONS)
if(NOT gecnd_cc_options)
    set(gecnd_cc_options "")
endif()

# Gly Engine
set(GLY_VERSION "0.3.13")
set(GLY_DIR "${CMAKE_SOURCE_DIR}/vendor/lua/gly-engine")
set(GLY_CLI "${GLY_DIR}/source/cli/main.lua")
set(GLY_DOWNLOAD "https://github.com/gamelly/gly-engine/archive/refs/tags/${GLY_VERSION}.tar.gz")

# Klib
set(KLIB_DIR "${CMAKE_SOURCE_DIR}/vendor/mono/klib")
set(KLIB_DOWNLOAD "https://github.com/attractivechaos/klib/archive/5c1451caa1ee476624d00eed71810532c89b82d1.tar.gz")
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${KLIB_DIR})

# STB
set(STB_DIR "${CMAKE_SOURCE_DIR}/vendor/mono/stb")
set(STB_DOWNLOAD "https://github.com/nothings/stb/archive/f1c79c02822848a9bed4315b12c8c8f3761e1296.tar.gz")
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${STB_DIR})

# Toml
set(TOMLC17_DIR "${CMAKE_SOURCE_DIR}/vendor/mono/toml")
set(TOMLC17_DOWNLOAD "https://github.com/cktan/tomlc17/archive/refs/tags/R260323.tar.gz")
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE "${TOMLC17_DIR}/src")

# Libretro 
set(LIBRETRO_DIR "${CMAKE_SOURCE_DIR}/vendor/mono/retroarch")
set(LIBRETRO_DOWNLOAD "https://raw.githubusercontent.com/libretro/RetroArch/ee3eae34c425a1645b923395854dda7f2543df31/libretro-common/include/libretro.h")

# Google GPERF/PPERF
set(GPERFTOOLS_VERSION "gperftools-2.18")
set(GPERFTOOLS_REPO "https://github.com/gperftools/gperftools.git")
set(GPERFTOOLS_DIR "${CMAKE_SOURCE_DIR}/vendor/gperftools")
set(GPERFTOOLS_BUILD_CPU_PROFILER ON CACHE BOOL "" FORCE)
set(GPERFTOOLS_BUILD_HEAP_PROFILER OFF CACHE BOOL "" FORCE)
set(GPERFTOOLS_BUILD_DEBUGALLOC OFF CACHE BOOL "" FORCE)
set(gperftools_build_minimal OFF CACHE BOOL "" FORCE)
set(gperftools_build_benchmark OFF CACHE BOOL "" FORCE)
set(gperftools_enable_libunwind ON CACHE BOOL "" FORCE)
set(gperftools_enable_stacktrace_via_backtrace ON CACHE BOOL "" FORCE)
set(gperftools_enable_frame_pointers ON CACHE BOOL "" FORCE)

if(NOT EXISTS "${KLIB_DIR}")
    FetchContent_Populate(klib URL "${KLIB_DOWNLOAD}" SOURCE_DIR ${KLIB_DIR})
endif()
if(NOT EXISTS "${STB_DIR}")
    FetchContent_Populate(stb URL "${STB_DOWNLOAD}" SOURCE_DIR ${STB_DIR})
endif()
if(NOT EXISTS "${TOMLC17_DIR}")
    FetchContent_Populate(tomlc17 URL "${TOMLC17_DOWNLOAD}" SOURCE_DIR ${TOMLC17_DIR})
endif()
add_library(tomlc17 STATIC "${TOMLC17_DIR}/src/tomlc17.c")
target_link_libraries(${PROJECT_NAME} PRIVATE tomlc17)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/libgl.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/libffmpeg.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/libuv.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/libz.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/libwarmcat.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/libffi.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/libfreetype.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/libjpeg.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/liblua.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/libqr.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/libsqlite3.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/libwuffs.cmake)

# todo: lirc only linux
# todo: move aui to private repo
target_sources(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_Inputs/driver_void.c"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_Inputs/driver_lirc.c"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_Inputs/driver_serial.c"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_Inputs/driver_read.c"
)

if(GECND_USE_PROFILER)
    enable_language(CXX)
    FetchContent_Declare(profiler GIT_REPOSITORY ${GPERFTOOLS_REPO} GIT_TAG ${GPERFTOOLS_VERSION} SOURCE_DIR ${GPERFTOOLS_DIR})
    FetchContent_MakeAvailable(profiler)
    target_link_libraries(${PROJECT_NAME} PRIVATE profiler)
endif()

if(GECND_USE_PLUGIN_LIBRETRO)
    if(NOT EXISTS "${LIBRETRO_DIR}/libretro.h")
        file(DOWNLOAD "${LIBRETRO_DOWNLOAD}" "${LIBRETRO_DIR}/libretro.h")
    endif()
    file(GLOB_RECURSE libretro_files "${CMAKE_CURRENT_LIST_DIR}/plugins/libretro/*.c")
    add_library(libretro_gecnd SHARED "${libretro_files}")
    target_include_directories(libretro_gecnd PRIVATE "${LIBRETRO_DIR}")
    target_link_libraries(libretro_gecnd PRIVATE gecnd_header)
    set_target_properties(libretro_gecnd PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    )
endif()

# TODO: use custom command with output
if(DEFINED ENGINE)
    execute_process(
        WORKING_DIRECTORY ${GLY_DIR}
        COMMAND ${LUA_BIN} ${GLY_CLI} fs-xxd-i ${ENGINE} --const --name engine_bytecode_lua
            ${CMAKE_BINARY_DIR}/include/engine_bytecode_lua.h
        COMMAND_ERROR_IS_FATAL ANY
    )
    set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/lib/Frontend_Core/update.c"
        PROPERTIES COMPILE_DEFINITIONS "GECND_USE_VENDOR_ENGINE=1"
    )
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set_target_properties(${PROJECT_NAME} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
target_link_libraries(${PROJECT_NAME} PRIVATE m)

if (GECND_AS_LIBRARY)
elseif(DEFINED GECND_ENTRYPOINT AND GECND_ENTRYPOINT)
    target_sources(${PROJECT_NAME} PRIVATE "${GECND_ENTRYPOINT}")
elseif (GECND_USE_LIBUV AND (GECND_USE_GL_EGL OR GECND_USE_GL_GLFW))
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src/main_uvsync.c")
elseif (NOT GECND_USE_LIBUV AND (GECND_USE_GL_EGL OR GECND_USE_GL_GLFW))
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src/main_basic.c")
elseif (GECND_USE_LIBUV)
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src/main_uv.c")
else()
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src/main_basic.c")
endif()
