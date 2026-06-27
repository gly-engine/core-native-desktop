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

# Wuffs
set(WUFFS_VERSION "0.3.4")
set(WUFFS_DIR "${CMAKE_SOURCE_DIR}/vendor/mono/wuffs")
set(WUFFS_DOWNLOAD "https://github.com/google/wuffs-mirror-release-c/archive/refs/tags/v${WUFFS_VERSION}.tar.gz")
option(GECND_USE_WUFFS "use wuffs for fast PNG decode" OFF)

# libjpeg-turbo (decode jpeg straight to planar YUV via TurboJPEG)
set(JPEGTURBO_VERSION "3.0.4")
set(JPEGTURBO_REPO "https://github.com/libjpeg-turbo/libjpeg-turbo.git")
set(JPEGTURBO_DIR "${CMAKE_SOURCE_DIR}/vendor/libjpeg-turbo")
set(JPEGTURBO_BIN "${CMAKE_BINARY_DIR}/libjpeg-turbo")
option(GECND_USE_JPEGTURBO "use libjpeg-turbo to decode jpeg straight to planar YUV" OFF)

# Toml
set(TOMLC17_DIR "${CMAKE_SOURCE_DIR}/vendor/mono/toml")
set(TOMLC17_DOWNLOAD "https://github.com/cktan/tomlc17/archive/refs/tags/R260323.tar.gz")
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE "${TOMLC17_DIR}/src")

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
set(SDL2_TEST OFF CACHE BOOL "" FORCE)
set(SDL2_SHARED OFF CACHE BOOL "" FORCE)
set(SDL2_STATIC ON CACHE BOOL "" FORCE)
set(SDL2TTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDL2TTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
set(SDL2TTF_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL2TTF_SAMPLES OFF CACHE BOOL "" FORCE)
option(GECND_USE_SDL2 "Use SDL backend" OFF)

# OpenGLES2
set(GLAD_DOWNLOAD "https://github.com/gly-engine/archive/archive/refs/heads/glad.tar.gz")
set(GLAD_DIR "${CMAKE_SOURCE_DIR}/vendor/opengl/glad")
set(GLFW_VERSION "3.4")
set(GLFW_DIR "${CMAKE_SOURCE_DIR}/vendor/opengl/glfw")
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
option(GECND_USE_OPENGL "Use OpenGL + GLFW backend" ON)
option(GECND_USE_EGL"Use OpenGL ES + EGL for backend" OFF)

# Xorg
set(X11_VERSION "1.8.4")
set(X11_DOWNLOAD "https://github.com/mirror/libX11/archive/refs/tags/libX11-${X11_VERSION}.tar.gz")
set(X11_DIR "${CMAKE_SOURCE_DIR}/vendor/opengl/x11")
set(X11_X11_INCLUDE_PATH "${X11_DIR}/include")
set(X11_X11_LIB "${X11_DIR}/lib/libX11.so")
set(XPROTO_VERSION "2025.1")
set(XPROTO_DOWNLOAD "https://gitlab.freedesktop.org/xorg/proto/xorgproto/-/archive/xorgproto-${XPROTO_VERSION}/xorgproto-xorgproto-${XPROTO_VERSION}.tar.gz")
set(XPROTO_DIR "${CMAKE_SOURCE_DIR}/vendor/opengl/xorgproto")
set(XRANDR_VERSION "1.5.5")
set(XRANDR_DOWNLOAD "https://gitlab.freedesktop.org/xorg/lib/libxrandr/-/archive/libXrandr-${XRANDR_VERSION}/libxrandr-libXrandr-${XRANDR_VERSION}.tar.gz")
set(XRANDR_DIR "${CMAKE_SOURCE_DIR}/vendor/opengl/xrandr")
set(XINERAMA_VERSION "1.1.4")
set(XINERAMA_DOWNLOAD "https://gitlab.freedesktop.org/xorg/lib/libxinerama/-/archive/libXinerama-${XINERAMA_VERSION}/libxinerama-libXinerama-${XINERAMA_VERSION}.tar.gz")
set(XINERAMA_DIR "${CMAKE_SOURCE_DIR}/vendor/opengl/xinerama")
set(XKB_VERSION "1.0.9")
set(XKB_DOWNLOAD "https://gitlab.freedesktop.org/xorg/lib/libxkbfile/-/archive/libxkbfile-${XKB_VERSION}/libxkbfile-libxkbfile-${XKB_VERSION}.tar.gz")
set(XKB_DIR "${CMAKE_SOURCE_DIR}/vendor/opengl/xkbfile")
set(XCURSOR_VERSION "1.1.12")
set(XCURSOR_DOWNLOAD "https://gitlab.freedesktop.org/xorg/lib/libxcursor/-/archive/libXcursor-${XCURSOR_VERSION}/libxcursor-libXcursor-${XCURSOR_VERSION}.tar.gz")
set(XCURSOR_DIR "${CMAKE_SOURCE_DIR}/vendor/opengl/xcursor")
set(XI_VERSION "1.8.2")
set(XI_DOWNLOAD "https://gitlab.freedesktop.org/xorg/lib/libxi/-/archive/libXi-${XI_VERSION}/libxi-libXi-${XI_VERSION}.tar.gz")
set(XI_DIR "${CMAKE_SOURCE_DIR}/vendor/opengl/xi")
set(XEXT_VERSION "1.2.0")  # XShape vem no Xext
set(XEXT_DOWNLOAD "https://gitlab.freedesktop.org/xorg/lib/libXext/-/archive/libXext-${XEXT_VERSION}/libxext-libXext-${XEXT_VERSION}.tar.gz")
set(XEXT_DIR "${CMAKE_SOURCE_DIR}/vendor/opengl/xext")
set(XRENDER_VERSION "0.9.10")
set(XRENDER_DOWNLOAD "https://gitlab.freedesktop.org/xorg/lib/libXrender/-/archive/libXrender-${XRENDER_VERSION}/libxrender-libXrender-${XRENDER_VERSION}.tar.gz")
set(XRENDER_DIR "${CMAKE_SOURCE_DIR}/vendor/opengl/xrender")
set(XFIXES_VERSION "6.0.0")
set(XFIXES_DOWNLOAD "https://gitlab.freedesktop.org/xorg/lib/libxfixes/-/archive/libXfixes-${XFIXES_VERSION}/libxfixes-libXfixes-${XFIXES_VERSION}.tar.gz")
set(XFIXES_DIR "${CMAKE_SOURCE_DIR}/vendor/opengl/xfixes")
set(X11_Xrandr_INCLUDE_PATH "${XRANDR_DIR}/include")
set(X11_Xinerama_INCLUDE_PATH "${XINERAMA_DIR}/include")
set(X11_Xkb_INCLUDE_PATH "${XKB_DIR}/include")
set(X11_Xcursor_INCLUDE_PATH "${XCURSOR_DIR}/include")
set(X11_Xi_INCLUDE_PATH "${XI_DIR}/include")
set(X11_Xshape_INCLUDE_PATH "${XEXT_DIR}/include")
set(X11_Xrender_INCLUDE_PATH "${XRENDER_DIR}/include")
set(X11_Xfixes_INCLUDE_PATH "${XFIXES_DIR}/include")
set(X11_HEADERS "${X11_DIR}/include;${XPROTO_DIR}/include")
set(X11_HEADERS_EXTRA "${X11_Xinerama_INCLUDE_PATH};${X11_Xinerama_INCLUDE_PATH};${X11_Xkb_INCLUDE_PATH};${X11_Xcursor_INCLUDE_PATH};${X11_Xi_INCLUDE_PATH};${X11_Xshape_INCLUDE_PATH};${X11_Xrender_INCLUDE_PATH};${X11_Xfixes_INCLUDE_PATH}")

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

# SPNG
set(SPNG_VERSION "v0.7.4")
set(SPNG_REPO "https://github.com/randy408/libspng.git")
set(SPNG_ROOT "${CMAKE_SOURCE_DIR}/vendor/spng")
set(SPNG_SHARED OFF CACHE BOOL "" FORCE)
set(SPNG_STATIC ON CACHE BOOL "" FORCE)
set(SPNG_INSTALL OFF CACHE BOOL "" FORCE)
option(GECND_USE_MINIZ "use bundled miniz as spng zlib backend instead system zlib" OFF)

# Libretro 
set(LIBRETRO_DIR "${CMAKE_SOURCE_DIR}/vendor/mono/retroarch")
set(LIBRETRO_DOWNLOAD "https://raw.githubusercontent.com/libretro/RetroArch/ee3eae34c425a1645b923395854dda7f2543df31/libretro-common/include/libretro.h")

# FontStash
set(FONTSTASH_DIR "${CMAKE_SOURCE_DIR}/vendor/mono/fontstash")
set(FONTSTASH_DOWNLOAD "https://github.com/memononen/fontstash/archive/b5ddc9741061343740d85d636d782ed3e07cf7be.tar.gz")
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE "${FONTSTASH_DIR}/src")

# Fonts
set(FONTS_DIR "${CMAKE_SOURCE_DIR}/vendor/fonts")
set(FONTS_INCLUDE_DIR "${CMAKE_BINARY_DIR}/include/gecnd")
set(FONTS_HEADER "${FONTS_INCLUDE_DIR}/notosans.h")
set(FONTS_ROBOTO_REGULAR_HEADER "${FONTS_INCLUDE_DIR}/roboto_regular.h")
set(FONTS_DOWNLOAD "https://github.com/gly-engine/archive/archive/refs/heads/fonts.zip")
set(FONTS_ROBOTO_REGULAR_DOWNLOAD "https://cdn.jsdelivr.net/npm/roboto-font@0.1.0/fonts/Roboto/roboto-regular-webfont.ttf")

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

if(NOT EXISTS "${FONTS_DIR}/Noto_Sans/NotoSans-Regular.ttf")
    FetchContent_Populate(fonts URL "${FONTS_DOWNLOAD}" SOURCE_DIR ${FONTS_DIR})
endif()

file(MAKE_DIRECTORY "${FONTS_DIR}/Roboto")

if(NOT EXISTS "${FONTS_DIR}/Roboto/roboto-regular-webfont.ttf")
    file(DOWNLOAD
        "${FONTS_ROBOTO_REGULAR_DOWNLOAD}"
        "${FONTS_DIR}/Roboto/roboto-regular-webfont.ttf"
        TLS_VERIFY ON
    )
endif()


add_custom_command(
    OUTPUT ${FONTS_HEADER}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${FONTS_INCLUDE_DIR}"
    COMMAND xxd -i "Noto_Sans/NotoSans-Regular.ttf" > ${FONTS_HEADER}
    COMMENT "Generating Noto Sans font header"
    WORKING_DIRECTORY ${FONTS_DIR}
)

add_custom_command(
    OUTPUT ${FONTS_ROBOTO_REGULAR_HEADER}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${FONTS_INCLUDE_DIR}"
    COMMAND xxd -i "Roboto/roboto-regular-webfont.ttf" > ${FONTS_ROBOTO_REGULAR_HEADER}
    COMMENT "Generating Roboto regular font header"
    WORKING_DIRECTORY ${FONTS_DIR}
)


add_custom_target(gecnd_fonts_headers DEPENDS
    ${FONTS_HEADER}
    ${FONTS_ROBOTO_REGULAR_HEADER}
)
add_dependencies(${PROJECT_NAME} gecnd_fonts_headers)

if(GECND_USE_SQLITE3)
    if(NOT EXISTS ${SQLITE_DIR})
        FetchContent_Populate(dep_sqlite3 URL ${SQLITE_DOWNLOAD} SOURCE_DIR ${SQLITE_DIR})
    endif()
    add_library(sqlite3-static STATIC "${SQLITE_DIR}/sqlite3.c")
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_IO/driver_db_sqlite3.c")
    target_link_libraries(${PROJECT_NAME} PRIVATE sqlite3-static)
    target_link_libraries("sqlite3-static" PRIVATE gecnd_cc_flags)
    target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${SQLITE_DIR})
else()
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_IO/driver_db_stub.c")
endif()

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

if(GECND_USE_OPENGL)
    file(GLOB_RECURSE gecnd_bakend_files "${CMAKE_CURRENT_LIST_DIR}/lib/Backend_OpenGL/*/*.c")
    if (NOT EXISTS "${GLAD_DIR}/include")
        FetchContent_Populate(glad URL ${GLAD_DOWNLOAD} SOURCE_DIR ${GLAD_DIR})
    endif()
    if(GECND_USE_EGL)
        add_library(glad_egl STATIC "${GLAD_DIR}/src/gles2.c;${GLAD_DIR}/src/egl.c")
        target_include_directories(glad_egl SYSTEM PRIVATE "${GLAD_DIR}/include")
        target_link_libraries(${PROJECT_NAME} PRIVATE glad_egl)
        list(REMOVE_ITEM gecnd_bakend_files "${CMAKE_CURRENT_LIST_DIR}/lib/Backend_OpenGL/window/glfw.c")
        set_source_files_properties(${gecnd_bakend_files} PROPERTIES COMPILE_DEFINITIONS "GECND_OPENGLES=1")
    else()
        if (NOT EXISTS "${X11_DIR}/include")
            FetchContent_Populate(x11 URL ${X11_DOWNLOAD} SOURCE_DIR ${X11_DIR})
        endif()
        if (NOT EXISTS "${XPROTO_DIR}/include")
            FetchContent_Populate(xproto URL ${XPROTO_DOWNLOAD} SOURCE_DIR ${XPROTO_DIR})
        endif()
        if (NOT EXISTS "${XRANDR_DIR}/include")
            FetchContent_Populate(xrandr URL ${XRANDR_DOWNLOAD} SOURCE_DIR ${XRANDR_DIR})
        endif()
        if (NOT EXISTS "${XINERAMA_DIR}/include")
            FetchContent_Populate(xinerama URL ${XINERAMA_DOWNLOAD} SOURCE_DIR ${XINERAMA_DIR})
        endif()
        if (NOT EXISTS "${XKB_DIR}/include")
            FetchContent_Populate(xkbfile URL ${XKB_DOWNLOAD} SOURCE_DIR ${XKB_DIR})
        endif()
        if (NOT EXISTS "${XCURSOR_DIR}/include")
            FetchContent_Populate(xcursor URL ${XCURSOR_DOWNLOAD} SOURCE_DIR ${XCURSOR_DIR})
        endif()
        if (NOT EXISTS "${XI_DIR}/include")
            FetchContent_Populate(xi URL ${XI_DOWNLOAD} SOURCE_DIR ${XI_DIR})
        endif()
        if (NOT EXISTS "${XEXT_DIR}/include")
            FetchContent_Populate(xext URL ${XEXT_DOWNLOAD} SOURCE_DIR ${XEXT_DIR})
        endif()
        if (NOT EXISTS "${XRENDER_DIR}/include")
            FetchContent_Populate(xext URL ${XRENDER_DOWNLOAD} SOURCE_DIR ${XRENDER_DIR})
        endif()
        if (NOT EXISTS "${XFIXES_DIR}/include")
            FetchContent_Populate(xext URL ${XFIXES_DOWNLOAD} SOURCE_DIR ${XFIXES_DIR})
        endif()
        FetchContent_Declare(
            glfw
            GIT_REPOSITORY https://github.com/glfw/glfw
            GIT_TAG ${GLFW_VERSION}
            SOURCE_DIR ${GLFW_DIR}
        )
        FetchContent_MakeAvailable(glfw)
        add_library(glad_gl STATIC "${GLAD_DIR}/src/gl.c")
        target_link_libraries(${PROJECT_NAME} PRIVATE glfw glad_gl)
        target_include_directories(glfw SYSTEM PRIVATE "${X11_HEADERS};${X11_HEADERS_EXTRA}")
        target_include_directories(glad_gl SYSTEM PRIVATE "${GLAD_DIR}/include")
        list(REMOVE_ITEM gecnd_bakend_files "${CMAKE_CURRENT_LIST_DIR}/lib/Backend_OpenGL/window/egl.c")
        set_source_files_properties(${gecnd_bakend_files} PROPERTIES COMPILE_DEFINITIONS "GECND_OPENGLES=0")
        set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_Inputs/service_keymap.c"
            PROPERTIES COMPILE_DEFINITIONS "GLFW_DEFAULT_INPUT_CLASS=\"void://glfw\""
        )
    endif()

    file(GLOB_RECURSE BACKEND_SOURCES "${CMAKE_CURRENT_LIST_DIR}/lib/Backend_OpenGL/*/*.c")
    file(GLOB_RECURSE GLSL_SOURCES "${CMAKE_CURRENT_LIST_DIR}/lib/Backend_OpenGL/shadder/*")
    foreach(GLSL ${GLSL_SOURCES})
        string(REGEX REPLACE "^.*Backend_OpenGL/?" "" GLSL_SHORT "${GLSL}")
        string(REGEX REPLACE "[./]" "_" GLSL_NAME "${GLSL_SHORT}")
        set(GLSL_OUT ${CMAKE_BINARY_DIR}/include/gecnd/${GLSL_NAME}.h)
        add_custom_command(
            OUTPUT ${GLSL_OUT}
            COMMAND xxd -i ${GLSL_SHORT} > ${GLSL_OUT}
            DEPENDS ${GLSL}
            COMMENT "Generating header ${GLSL_OUT}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/lib/Backend_OpenGL"
        )
        list(APPEND GENERATED_HEADERS ${GLSL_OUT})
    endforeach()
    
    add_custom_target(gecnd_glsl_headers DEPENDS ${GENERATED_HEADERS})
    add_dependencies(${PROJECT_NAME} gecnd_glsl_headers)
    set_source_files_properties(${gecnd_bakend_files} PROPERTIES INCLUDE_DIRECTORIES "${GLAD_DIR}/include;${GLFW_DIR}/include")
    target_sources(${PROJECT_NAME} PRIVATE "${gecnd_bakend_files}")
endif()

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

if((GECND_USE_OPENGL OR GECND_USE_WARMCAT) AND GECND_USE_ZLIB)
    FetchContent_Declare(zlib GIT_REPOSITORY ${ZLIB_REPO} GIT_TAG ${ZLIB_VERSION} SOURCE_DIR ${ZLIB_DIR})
    FetchContent_MakeAvailable(zlib)
    target_link_libraries(${PROJECT_NAME} PRIVATE zlib)
    target_compile_options(uv_a PRIVATE ${gecnd_cc_options})
    set_target_properties(zlib PROPERTIES EXCLUDE_FROM_ALL TRUE)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif()

if(GECND_USE_OPENGL)
    if(GECND_USE_MINIZ)
        set(SPNG_USE_MINIZ ON CACHE BOOL "" FORCE)
    else()
        set(SPNG_USE_MINIZ OFF CACHE BOOL "" FORCE)
    endif()
    FetchContent_Declare(spng GIT_REPOSITORY ${SPNG_REPO} GIT_TAG ${SPNG_VERSION} SOURCE_DIR ${SPNG_ROOT})
    FetchContent_MakeAvailable(spng)
    target_compile_options(spng_static PRIVATE ${gecnd_cc_options})
    target_link_libraries(${PROJECT_NAME} PRIVATE spng_static)
    target_include_directories(${PROJECT_NAME} PRIVATE ${SPNG_ROOT}/spng)
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_Img/driver_spng.c")
    set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/lib/Frontend_Core/hypervisor.c" PROPERTIES COMPILE_DEFINITIONS "GECND_USE_SPNG=1")
endif()

if(GECND_USE_WUFFS)
    if(NOT EXISTS "${WUFFS_DIR}")
        FetchContent_Populate(wuffs URL "${WUFFS_DOWNLOAD}" SOURCE_DIR "${WUFFS_DIR}")
    endif()
    target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE "${WUFFS_DIR}")
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_Img/driver_wuffs.c")
    set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_Img/driver_wuffs.c" PROPERTIES INCLUDE_DIRECTORIES "${WUFFS_DIR}/release/c")
    set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/lib/Frontend_Core/hypervisor.c" PROPERTIES COMPILE_DEFINITIONS "GECND_USE_WUFFS=1")
endif()

if(GECND_USE_JPEGTURBO)
    if(NOT EXISTS ${JPEGTURBO_DIR})
        FetchContent_Populate(libjpeg_turbo_dep GIT_REPOSITORY ${JPEGTURBO_REPO} GIT_TAG ${JPEGTURBO_VERSION} SOURCE_DIR "${JPEGTURBO_DIR}")
    endif()
    file(MAKE_DIRECTORY ${JPEGTURBO_BIN}/include)
    ExternalProject_Add(libjpeg_turbo_proj
        SOURCE_DIR ${JPEGTURBO_DIR}
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX=${JPEGTURBO_BIN}
            -DCMAKE_INSTALL_LIBDIR=lib
            -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DENABLE_SHARED=OFF
            -DENABLE_STATIC=ON
            -DWITH_TURBOJPEG=ON
        UPDATE_COMMAND ""
        BUILD_BYPRODUCTS ${JPEGTURBO_BIN}/lib/libturbojpeg.a
    )
    add_library(turbojpeg STATIC IMPORTED)
    set_target_properties(turbojpeg PROPERTIES
        IMPORTED_LOCATION ${JPEGTURBO_BIN}/lib/libturbojpeg.a
        INTERFACE_INCLUDE_DIRECTORIES ${JPEGTURBO_BIN}/include
    )
    add_dependencies(turbojpeg libjpeg_turbo_proj)
    add_dependencies(${PROJECT_NAME} libjpeg_turbo_proj)
    target_link_libraries(${PROJECT_NAME} PRIVATE turbojpeg)
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_Img/driver_jpegturbo.c")
    set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_Img/driver_jpegturbo.c" PROPERTIES INCLUDE_DIRECTORIES "${JPEGTURBO_BIN}/include")
    set_property(SOURCE "${CMAKE_CURRENT_LIST_DIR}/lib/Frontend_Core/hypervisor.c" APPEND PROPERTY COMPILE_DEFINITIONS "GECND_USE_JPEGTURBO=1")
endif()

if(GECND_USE_OPENGL AND NOT EXISTS "${FONTSTASH_DIR}")
    FetchContent_Populate(fontstash URL "${FONTSTASH_DOWNLOAD}" SOURCE_DIR ${FONTSTASH_DIR})
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
            -DCMAKE_INSTALL_PREFIX=${WARMCAT_BIN}
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
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_WebServer/driver_warmcat.c")
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_WebClient/driver_warmcat.c")
endif()

if((GECND_USE_WARMCAT) AND GECND_USE_MBEDTLS)
    target_link_libraries(${PROJECT_NAME} PRIVATE mbedtls mbedx509 mbedcrypto)
    set_source_files_properties(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_WebServer/driver_warmcat.c" PROPERTIES COMPILE_DEFINITIONS "GECND_HAS_SSL=1")
    set_source_files_properties(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_WebClient/driver_warmcat.c" PROPERTIES COMPILE_DEFINITIONS "GECND_HAS_SSL=1")
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

include(${CMAKE_CURRENT_LIST_DIR}/cmake/liblua.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/libffi.cmake)

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
elseif (GECND_USE_LIBUV AND GECND_USE_OPENGL)
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src/main_uvsync.c")
elseif (NOT GECND_USE_LIBUV AND GECND_USE_OPENGL)
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src/main_basic.c")
elseif (GECND_USE_LIBUV)
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src/main_uv.c")
else()
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src/main_basic.c")
endif()
