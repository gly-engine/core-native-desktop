cmake_minimum_required(VERSION 3.19)

if(DEFINED TARGET)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/zig-toolchain.cmake" CACHE STRING "zig cross compiler")
endif()
if(NOT DEFINED GECND_PROJECT_NAME)
    set(GECND_PROJECT_NAME "core")
endif()
project("${GECND_PROJECT_NAME}" LANGUAGES C)
include(FetchContent)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_SKIP_INSTALL_RULES ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)

option(GECND_AS_LIBRARY "build static lib gecnd intead binary" OFF)
option(GECND_NO_SANITIZE "disable UB flags on clang/llvm" OFF)
option(GECND_MOCK_HTTP "add package http but raise error on request" OFF)

# EXE
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/include/gecnd)
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
    add_executable(${PROJECT_NAME} "${GECND_ENTRYPOINT};${SOURCES}")
endif()
add_library(gecnd_lua INTERFACE)
add_library(gecnd_header INTERFACE)
if(NOT TARGET gecnd_cc_flags)
    add_library(gecnd_cc_flags INTERFACE)
endif()
target_include_directories(gecnd_header INTERFACE "${CMAKE_CURRENT_LIST_DIR}/include;${CMAKE_BINARY_DIR}/include")
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
option(GECND_USE_LUA32BITS "lua uses float instead double" OFF)
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

# SPNG
set(SPNG_VERSION "v0.7.4")
set(SPNG_REPO "https://github.com/randy408/libspng.git")
set(SPNG_ROOT "${CMAKE_SOURCE_DIR}/vendor/spng")
set(SPNG_SHARED OFF CACHE BOOL "" FORCE)
set(SPNG_STATIC ON CACHE BOOL "" FORCE)
set(SPNG_USE_MINIZ OFF CACHE BOOL "" FORCE)
set(SPNG_INSTALL OFF CACHE BOOL "" FORCE)

# FontStash
set(FONTSTASH_DIR "${CMAKE_SOURCE_DIR}/vendor/mono/fontstash")
set(FONTSTASH_DOWNLOAD "https://github.com/memononen/fontstash/archive/b5ddc9741061343740d85d636d782ed3e07cf7be.tar.gz")
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE "${FONTSTASH_DIR}/src")

# Fonts
set(FONTS_DIR "${CMAKE_SOURCE_DIR}/vendor/fonts")
set(FONTS_HEADER "${CMAKE_BINARY_DIR}/include/gecnd/notosans.h")
set(FONTS_DOWNLOAD "https://github.com/gly-engine/archive/archive/refs/heads/fonts.zip")

# Curl
set(CURL_VERSION "8_9_1")
set(CURL_DIR "${CMAKE_SOURCE_DIR}/vendor/curl")
set(HTTP_ONLY ON CACHE BOOL "" FORCE)
set(PICKY_COMPILER OFF CACHE BOOL "" FORCE)
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_STATIC_CURL OFF CACHE BOOL "" FORCE)
set(ENABLE_ARES OFF CACHE BOOL "" FORCE)
set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
set(USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
set(CURL_ENABLE_EXPORT_TARGET OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_COOKIES ON CACHE BOOL "" FORCE)
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

if(GECND_NO_SANITIZE)
    target_compile_options(gecnd_cc_flags INTERFACE -fno-sanitize=all -fno-sanitize-recover=all)
endif()

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

if(NOT EXISTS "${FONTS_HEADER}")
    FetchContent_Populate(fonts URL "${FONTS_DOWNLOAD}" SOURCE_DIR ${FONTS_DIR})
    add_custom_command(
        OUTPUT ${FONTS_HEADER}
        COMMAND xxd -i "Noto_Sans/NotoSans-Regular.ttf" > ${FONTS_HEADER}
        COMMENT "Generating default font"
        WORKING_DIRECTORY ${FONTS_DIR}
    )
    add_custom_target(gecnd_fonts_headers DEPENDS ${FONTS_HEADER})
    add_dependencies(${PROJECT_NAME} gecnd_fonts_headers)
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
    if (NOT EXISTS "${GLAD_DIR}/include")
        FetchContent_Populate(glad URL ${GLAD_DOWNLOAD} SOURCE_DIR ${GLAD_DIR})
    endif()
    if(GECND_USE_EGL)
        target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/lib/Backend_OpenGL/egl.c")
        add_library(glad_egl STATIC "${GLAD_DIR}/src/gles2.c;${GLAD_DIR}/src/egl.c")
        target_include_directories(glad_egl SYSTEM PRIVATE "${GLAD_DIR}/include")
        target_link_libraries(${PROJECT_NAME} PRIVATE glad_egl)
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
        target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/lib/Backend_OpenGL/glfw.c")
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
    target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE "${GLAD_DIR}/include;${GLFW_DIR}/include")
endif()

if(GECND_USE_FFMPEG4)
    set(FFMPEG_DOWNLOAD "https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n${FFMPEG4_VERSION}.tar.gz")
    set(FFMPEG_DIR "${CMAKE_SOURCE_DIR}/vendor/avlib/ffmpeg4")
else()
    set(FFMPEG_DOWNLOAD "https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n${FFMPEG_VERSION}.tar.gz")
    set(FFMPEG_DIR "${CMAKE_SOURCE_DIR}/vendor/avlib/ffmpeg7")
endif()

if(GECND_USE_FFMPEG)
    FILE(GLOB gecnd_ffmpeg_files "${CMAKE_CURRENT_LIST_DIR}/lib/Stream_AVlib/*.c")
    if (NOT EXISTS "${FFMPEG_DIR}/libavcodec")
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
        list(REMOVE_ITEM gecnd_ffmpeg_files "${CMAKE_CURRENT_LIST_DIR}/lib/Stream_AVlib/open_dynamic.c")
    else()
        list(REMOVE_ITEM gecnd_ffmpeg_files "${CMAKE_CURRENT_LIST_DIR}/lib/Stream_AVlib/open_static.c")
    endif()
    target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${FFMPEG_DIR})
    target_sources(${PROJECT_NAME} PRIVATE "${gecnd_ffmpeg_files}")
endif()

if (GECND_USE_LIBUV)
    FetchContent_Declare(uv_a GIT_REPOSITORY ${LIBUV_REPO} GIT_TAG ${LIBUV_VERSION} SOURCE_DIR ${LIBUV_DIR})
    FetchContent_MakeAvailable(uv_a)
    target_link_libraries(${PROJECT_NAME} PRIVATE uv_a)
    target_compile_options(uv_a PRIVATE ${gecnd_cc_options})
endif()

if(GECND_USE_OPENGL OR GECND_USE_CURL)
    FetchContent_Declare(zlib GIT_REPOSITORY ${ZLIB_REPO} GIT_TAG ${ZLIB_VERSION} SOURCE_DIR ${ZLIB_DIR})
    FetchContent_MakeAvailable(zlib)
    target_link_libraries(${PROJECT_NAME} PRIVATE zlib)
    target_compile_options(uv_a PRIVATE ${gecnd_cc_options})
    set_target_properties(zlib PROPERTIES EXCLUDE_FROM_ALL TRUE)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif()

if(GECND_USE_OPENGL)
    FetchContent_Declare(spng GIT_REPOSITORY ${SPNG_REPO} GIT_TAG ${SPNG_VERSION} SOURCE_DIR ${SPNG_ROOT})
    FetchContent_MakeAvailable(spng)
    target_link_libraries(${PROJECT_NAME} PRIVATE spng_static)
    target_include_directories(${PROJECT_NAME} PRIVATE ${SPNG_ROOT}/spng)
endif()

if(GECND_USE_OPENGL AND NOT EXISTS "${FONTSTASH_DIR}")
    FetchContent_Populate(fontstash URL "${FONTSTASH_DOWNLOAD}" SOURCE_DIR ${FONTSTASH_DIR})
endif()

if(GECND_USE_CURL)
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
