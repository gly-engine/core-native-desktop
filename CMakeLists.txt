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

option(GECND_EXPORTS "allow rdynamic" OFF)
option(GECND_AS_LIBRARY "build static lib gecnd intead binary" OFF)
option(GECND_NO_SANITIZE "disable UB flags on clang/llvm" OFF)
option(GECND_USE_PROFILER "use google gperftools" OFF)
option(GECND_MOCK_HTTP "add package http but raise error on request" OFF)

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

if(GECND_NO_SANITIZE)
    target_compile_options(gecnd_cc_flags INTERFACE -fno-sanitize=all -fno-sanitize-recover=all)
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
set(FFMPEG4_VERSION "4.4.6")
option(GECND_USE_FFMPEG "fetch ffmpeg" ON)
option(GECND_USE_FFMPEG4 "use ffmpeg 4 instead 7" OFF)
option(GECND_BUILD_FFMPEG "build static ffmpeg" OFF)

# LibUV
set(LIBUV_VERSION "v1.48.0")
set(LIBUV_REPO "https://github.com/libuv/libuv.git")
set(LIBUV_DIR "${CMAKE_SOURCE_DIR}/vendor/libuv")
set(LIBUV_BUILD_SHARED OFF CACHE BOOL "" FORCE)
option(GECND_USE_LIBUV "fetch libuv" ON)

# Zlib
set(ZLIB_VERSION "v1.3.1")
set(ZLIB_REPO "https://github.com/madler/zlib.git")
set(ZLIB_DIR "${CMAKE_SOURCE_DIR}/vendor/zlib")
set(ZLIB_ROOT "${ZLIB_DIR}" CACHE STRING "" FORCE)
set(ZLIB_LIBRARY "${CMAKE_SOURCE_DIR}/build/_deps/zlib-build/libz.a" CACHE FILEPATH "")
set(ZLIB_INCLUDE_DIR "${ZLIB_DIR}" CACHE PATH "")
set(ZLIB_FOUND TRUE CACHE BOOL "")
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
option(GECND_USE_ZLIB "fetch zlib" ON)

# Libretro 
set(LIBRETRO_DIR "${CMAKE_SOURCE_DIR}/vendor/mono/retroarch")
set(LIBRETRO_DOWNLOAD "https://raw.githubusercontent.com/libretro/RetroArch/ee3eae34c425a1645b923395854dda7f2543df31/libretro-common/include/libretro.h")

# Fonts
set(FONTS_DIR "${CMAKE_SOURCE_DIR}/vendor/fonts")
set(FONTS_INCLUDE_DIR "${CMAKE_BINARY_DIR}/include/gecnd")
set(FONTS_DEFAULT_HEADER "${FONTS_INCLUDE_DIR}/font_noto_sans.h")
set(NOTO_SANS_DOWNLOAD "https://github.com/gly-engine/archive/archive/refs/heads/fonts.tar.gz")
set(NOTO_SANS_DIR "${CMAKE_SOURCE_DIR}/vendor/fonts/noto_sans")

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

# Warmcat 
set(WARMCAT_VERSION "4.5.8")
set(WARMCAT_DOWNLOAD "https://github.com/warmcat/libwebsockets/archive/refs/tags/v${WARMCAT_VERSION}.tar.gz")
set(WARMCAT_DIR "${CMAKE_SOURCE_DIR}/vendor/warmcat")
set(WARMCAT_BIN "${CMAKE_BINARY_DIR}/warmcat")
option(GECND_USE_WARMCAT "use warmcat to http/webosocket client/server" ON)

# MbedTLS
set(MBEDTLS_VERSION "mbedtls-3.6.4")
set(MBEDTLS_REPO "https://github.com/Mbed-TLS/mbedtls.git")
set(MBEDTLS_DIR "${CMAKE_SOURCE_DIR}/vendor/web/mbedtls")
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
option(GECND_USE_MBEDTLS "use mbedtls backend for ssl" OFF)

# Plugins
option(GECND_USE_PLUGIN_CHROMIUM "build plugin chromium for core native" OFF)
option(GECND_USE_PLUGIN_WPA "build plugin wpa for core native" OFF)
option(GECND_USE_PLUGIN_LIBRETRO "build plugin libretro for core native" OFF)

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

if(NOT EXISTS "${NOTO_SANS_DIR}")
    FetchContent_Populate(noto_sans URL "${NOTO_SANS_DOWNLOAD}" SOURCE_DIR ${NOTO_SANS_DIR})
endif()

add_custom_command(
    OUTPUT ${FONTS_DEFAULT_HEADER}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${FONTS_INCLUDE_DIR}"
    COMMAND xxd -i "Noto_Sans/NotoSans-Regular-ASCII.ttf" > ${FONTS_DEFAULT_HEADER}
    COMMENT "Generating default font header"
    WORKING_DIRECTORY ${NOTO_SANS_DIR}
)

add_custom_target(gecnd_fonts_headers DEPENDS
    ${FONTS_DEFAULT_HEADER}
)
add_dependencies(${PROJECT_NAME} gecnd_fonts_headers)

if(GECND_USE_RAYLIB)
    if(NOT EXISTS "${RAYLIB_DIR}")
        FetchContent_Populate(dep_raylib URL "${RAYLIB_DOWNLOAD}" SOURCE_DIR ${RAYLIB_DIR})
    endif()
    add_library(raylib-static STATIC IMPORTED)
    set_target_properties(raylib-static PROPERTIES IMPORTED_LOCATION "${RAYLIB_DIR}/lib/libraylib.a")
    file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_LIST_DIR}/lib/Backend_Raylib/*.c")
    target_sources(${PROJECT_NAME} PRIVATE "${SOURCES}")
    target_link_libraries(${PROJECT_NAME} PRIVATE raylib-static)
    target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${RAYLIB_DIR}/include)
    #todo cross linking
endif()

if(GECND_USE_SDL2)
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
    file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_LIST_DIR}/lib/Backend_SDL2/*.c")
    target_sources(${PROJECT_NAME} PRIVATE "${SOURCES}")
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2-static SDL2_ttf::SDL2_ttf-static)
    target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${SDLTTF_DIR};${SDL_DIR}/include)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/cmake/libgl.cmake)

if(GECND_USE_FFMPEG4)
    set(FFMPEG_DOWNLOAD "https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n${FFMPEG4_VERSION}.tar.gz")
    set(FFMPEG_DIR "${CMAKE_SOURCE_DIR}/vendor/avlib/ffmpeg4")
else()
    set(FFMPEG_DOWNLOAD "https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n${FFMPEG_VERSION}.tar.gz")
    set(FFMPEG_DIR "${CMAKE_SOURCE_DIR}/vendor/avlib/ffmpeg7")
endif()

if(GECND_USE_FFMPEG)
    FILE(GLOB gecnd_ffmpeg_files "${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_Media/driver_av_*.c")
    list(REMOVE_ITEM gecnd_ffmpeg_files "${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_Media/driver_av_stub.c")
    if (NOT EXISTS "${FFMPEG_DIR}/libavutil/avconfig.h")
        FetchContent_Populate(dep_ffmpeg URL ${FFMPEG_DOWNLOAD} SOURCE_DIR ${FFMPEG_DIR})
        execute_process(
            COMMAND ./configure --prefix=${FFMPEG_DIR}/build --enable-static --disable-shared
            --disable-bzlib --disable-lzma --disable-x86asm --disable-libdrm
            WORKING_DIRECTORY ${FFMPEG_DIR}
            COMMAND_ERROR_IS_FATAL ANY
        )
    endif()
    if(GECND_BUILD_FFMPEG)
        if (NOT EXISTS "${FFMPEG_DIR}/libavcodec/libavcodec.a")
            execute_process(
                COMMAND ${CMAKE_MAKE_PROGRAM} -j ${J}
                WORKING_DIRECTORY ${FFMPEG_DIR}
                COMMAND_ERROR_IS_FATAL ANY
            )
        endif()
        FILE(GLOB ffmpeg_files "${FFMPEG_DIR}/*/*.a")
        target_link_libraries(${PROJECT_NAME} PRIVATE "${ffmpeg_files}")
        list(REMOVE_ITEM gecnd_ffmpeg_files "${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_Media/driver_av_dym.c")
    else()
        list(REMOVE_ITEM gecnd_ffmpeg_files "${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_Media/driver_av_static.c")
    endif()
    target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${FFMPEG_DIR})
    target_sources(${PROJECT_NAME} PRIVATE "${gecnd_ffmpeg_files}")
else()
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_Media/driver_av_stub.c")
endif()

if (GECND_USE_LIBUV)
    FetchContent_Declare(uv_a GIT_REPOSITORY ${LIBUV_REPO} GIT_TAG ${LIBUV_VERSION} SOURCE_DIR ${LIBUV_DIR})
    FetchContent_MakeAvailable(uv_a)
    target_link_libraries(${PROJECT_NAME} PRIVATE uv_a)
    target_compile_options(uv_a PRIVATE ${gecnd_cc_options})
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_IO/driver_fs_uv.c")
endif()

if((GECND_USE_GL_EGL OR GECND_USE_GL_GLFW OR GECND_USE_WARMCAT) AND GECND_USE_ZLIB)
    FetchContent_Declare(zlib GIT_REPOSITORY ${ZLIB_REPO} GIT_TAG ${ZLIB_VERSION} SOURCE_DIR ${ZLIB_DIR})
    FetchContent_MakeAvailable(zlib)
    target_link_libraries(${PROJECT_NAME} PRIVATE zlibstatic)
    target_compile_options(zlibstatic PRIVATE ${gecnd_cc_options})
    set_target_properties(zlib PROPERTIES EXCLUDE_FROM_ALL TRUE)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif()

if((GECND_USE_WARMCAT) AND GECND_USE_MBEDTLS)
    FetchContent_Declare(mbedtls GIT_REPOSITORY ${MBEDTLS_REPO} GIT_TAG ${MBEDTLS_VERSION} SOURCE_DIR "${MBEDTLS_DIR}")
    FetchContent_MakeAvailable(mbedtls)
endif()

if(GECND_USE_WARMCAT)
    if(NOT EXISTS ${WARMCAT_DIR})
        FetchContent_Populate(libwebsockets_dep URL "${WARMCAT_DOWNLOAD}" SOURCE_DIR "${WARMCAT_DIR}")            
    endif()
    file(MAKE_DIRECTORY ${WARMCAT_BIN}/include)
    ExternalProject_Add(libwebsockets_proj
        SOURCE_DIR ${WARMCAT_DIR}
        CMAKE_ARGS
            ${ZIG_TARGET_FLAG}
            -DCMAKE_INSTALL_PREFIX=${WARMCAT_BIN}
            -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
            -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
            -DLWS_WITH_STATIC=ON
            -DLWS_WITH_SHARED=OFF
            -DLWS_ROLE_H1=ON
            -DLWS_ROLE_WS=ON
            -DLWS_ROLE_H2=ON
            -DLWS_ROLE_MQTT=OFF
            -DLWS_ROLE_RAW_FILE=OFF
            -DLWS_ROLE_RAW_PROXY=OFF
            -DLWS_WITH_SSL=${GECND_USE_MBEDTLS}
            -DLWS_WITH_MBEDTLS=${GECND_USE_MBEDTLS}
            -DLWS_HAVE_MBEDTLS_NET_SOCKETS=ON
            -DLWS_HAVE_MBEDTLS_SSL_NEW_SESSION_TICKET=ON
            -DLWS_HAVE_mbedtls_md_setup=ON
            -DLWS_HAVE_mbedtls_ssl_conf_alpn_protocols=ON
            -DLWS_HAVE_mbedtls_ssl_get_alpn_protocol=ON
            -DLWS_WITH_OPENSSL=OFF
            -DLWS_WITH_WOLFSSL=OFF
            -DLWS_WITH_MINIMAL_EXAMPLES=OFF
            -DLWS_WITHOUT_TESTAPPS=ON
            -DLWS_WITHOUT_TEST_SERVER=ON
            -DLWS_WITHOUT_TEST_SERVER_EXTPOLL=ON
            -DLWS_WITHOUT_TEST_PING=ON
            -DLWS_WITHOUT_TEST_CLIENT=ON
            -DLWS_WITH_PLUGINS=OFF
            -DLWS_WITH_PLUGINS_BUILTIN=OFF
            -DLWS_WITH_CGI=OFF
            -DLWS_WITH_HTTP_PROXY=OFF
            -DLWS_WITH_HTTP_BASIC_AUTH=OFF
            -DLWS_WITH_HTTP_UNCOMMON_HEADERS=ON
            -DLWS_WITH_ZIP_FOPS=OFF
            -DLWS_WITH_JPEG=OFF
            -DLWS_WITH_PNG=OFF
            -DLWS_WITH_DLO=OFF
            -DLWS_WITH_LHP=OFF
            -DLWS_WITH_SECURE_STREAMS=OFF
            -DLWS_WITH_SECURE_STREAMS_PROXY_API=OFF
            -DLWS_WITH_SECURE_STREAMS_STATIC_POLICY_ONLY=OFF
            -DLWS_WITH_STRUCT_JSON=OFF
            -DLWS_WITH_STRUCT_SQLITE3=OFF
            -DLWS_WITH_CBOR=OFF
            -DLWS_WITH_CBOR_FLOAT=OFF
            -DLWS_WITH_DISKCACHE=OFF
            -DLWS_WITH_THREADPOOL=OFF
            -DLWS_WITH_LIBUV=ON
            -DLWS_WITH_LIBEVENT=OFF
            -DLWS_WITH_LIBEV=OFF
            -DLWS_WITH_GLIB=OFF
            -DLWS_WITH_ZLIB=ON
            -DLWS_WITH_BUNDLED_ZLIB=OFF
            -DLWS_WITHOUT_EXTENSIONS=ON
            -DZLIB_ROOT=${ZLIB_DIR}
            -DZLIB_LIBRARY=${ZLIB_LIBRARY}
            -DZLIB_INCLUDE_DIR=${ZLIB_INCLUDE_DIR}
            -DZLIB_FOUND=TRUE
            -DLIBUV_INCLUDE_DIRS=${LIBUV_DIR}/include
            -DLWS_MBEDTLS_INCLUDE_DIRS=${MBEDTLS_DIR}/include
        UPDATE_COMMAND ""
        INSTALL_COMMAND ${CMAKE_COMMAND} --build . --target install
    )
    add_library(libwebsockets STATIC IMPORTED)
    set_target_properties(libwebsockets PROPERTIES
        IMPORTED_LOCATION ${WARMCAT_BIN}/lib/libwebsockets.a
        INTERFACE_INCLUDE_DIRECTORIES ${WARMCAT_BIN}/include
    )
    add_dependencies(libwebsockets libwebsockets_proj)
    add_dependencies(${PROJECT_NAME} libwebsockets)
    target_link_libraries(${PROJECT_NAME} PRIVATE libwebsockets)
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_Web/driver_server_uv_warmcat.c")
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_Web/driver_client_uv_warmcat.c")
endif()

if((GECND_USE_WARMCAT) AND GECND_USE_MBEDTLS)
    target_link_libraries(${PROJECT_NAME} PRIVATE mbedtls mbedx509 mbedcrypto)
    set_source_files_properties(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_Web/driver_server_uv_warmcat.c" PROPERTIES COMPILE_DEFINITIONS "GECND_HAS_SSL=1")
    set_source_files_properties(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_Web/driver_client_uv_warmcat.c" PROPERTIES COMPILE_DEFINITIONS "GECND_HAS_SSL=1")
endif()

# todo: lirc only linux
# todo: move aui to private repo
target_sources(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_Inputs/driver_void.c"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_Inputs/driver_lirc.c"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_Inputs/driver_serial.c"
    "${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_Inputs/driver_read.c"
)

if(GECND_MOCK_HTTP)
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/lib/ThirdParty_Api/http_mock.c")
endif()

if(GECND_USE_PROFILER)
    enable_language(CXX)
    FetchContent_Declare(profiler GIT_REPOSITORY ${GPERFTOOLS_REPO} GIT_TAG ${GPERFTOOLS_VERSION} SOURCE_DIR ${GPERFTOOLS_DIR})
    FetchContent_MakeAvailable(profiler)
    target_link_libraries(${PROJECT_NAME} PRIVATE profiler)
endif()

if(GECND_USE_PLUGIN_CHROMIUM)
    add_library(chromium_gecnd SHARED "${CMAKE_CURRENT_LIST_DIR}/plugins/chromium/main.c")
    target_include_directories(chromium_gecnd PRIVATE ${LIBUV_DIR}/include)
    target_link_libraries(chromium_gecnd PRIVATE gecnd_header gecnd_lua)
    set_target_properties(chromium_gecnd PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    )
endif()

if(GECND_USE_PLUGIN_WPA)
    add_library(wpa_gecnd SHARED "${CMAKE_CURRENT_LIST_DIR}/plugins/wpa/main.c")
    target_link_libraries(wpa_gecnd PRIVATE gecnd_header)
    set_target_properties(wpa_gecnd PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    )
endif()

include(${CMAKE_CURRENT_LIST_DIR}/cmake/libffi.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/libfreetype.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/libjpeg.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/liblua.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/libqr.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/libsqlite3.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/libwuffs.cmake)

if(GECND_USE_PLUGIN_LIBRETRO)
    if(NOT EXISTS "${LIBRETRO_DIR}/libretro.h")
        file(DOWNLOAD "${LIBRETRO_DOWNLOAD}" "${LIBRETRO_DIR}/libretro.h")
    endif()
    file(GLOB_RECURSE libretro_files "${CMAKE_CURRENT_LIST_DIR}/plugins/libretro/*.c")
    add_library(libretro_gecnd SHARED "${libretro_files}")
    target_include_directories(libretro_gecnd PRIVATE "${LIBRETRO_DIR}")
    target_link_libraries(libretro_gecnd PRIVATE gecnd_header gecnd_lua)
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

if(GECND_EXPORTS)                                                                                                                                                                                 
  set_target_properties(${PROJECT_NAME} PROPERTIES ENABLE_EXPORTS ON)
endif()  

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
