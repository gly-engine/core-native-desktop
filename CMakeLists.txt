cmake_minimum_required(VERSION 3.19)

project(game LANGUAGES C)
include(FetchContent)

include_directories("${CMAKE_SOURCE_DIR}/src")
include_directories("${CMAKE_SOURCE_DIR}/vendor")
include_directories("${CMAKE_SOURCE_DIR}/vendor/ffmpeg")
include_directories("${CMAKE_SOURCE_DIR}/vendor/raylib/include")

# Raylib
set(RAYLIB_VERSION "5.5")
set(RAYLIB_DIR "${CMAKE_SOURCE_DIR}/vendor/raylib")
set(RAYLIB_DOWNLOAD "https://github.com/raysan5/raylib/releases/download/${RAYLIB_VERSION}/raylib-${RAYLIB_VERSION}_linux_amd64.tar.gz")
if(NOT EXISTS "${RAYLIB_DIR}")
    FetchContent_Populate(dep_raylib URL "${RAYLIB_DOWNLOAD}" SOURCE_DIR ${RAYLIB_DIR})
endif()
add_library(raylib-static STATIC IMPORTED)
set_target_properties(raylib-static PROPERTIES IMPORTED_LOCATION "${RAYLIB_DIR}/lib/libraylib.a")

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

# EXE
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
file(GLOB_RECURSE SOURCES "src/*.c")
add_executable(${PROJECT_NAME} ${SOURCES})
target_link_libraries(${PROJECT_NAME} PRIVATE "${ffmpeg_files}")
target_link_libraries(${PROJECT_NAME} PRIVATE raylib-static)
target_link_libraries(${PROJECT_NAME} PRIVATE m z)
set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
