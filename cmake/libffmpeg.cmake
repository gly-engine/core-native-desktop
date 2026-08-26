set(FFMPEG_VERSION "7.0.2")
set(FFMPEG4_VERSION "4.4.6")

if(GECND_USE_FFMPEG4)
    set(FFMPEG_DOWNLOAD "https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n${FFMPEG4_VERSION}.tar.gz")
    set(FFMPEG_DIR "${CMAKE_SOURCE_DIR}/vendor/avlib/ffmpeg4")
else()
    set(FFMPEG_DOWNLOAD "https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n${FFMPEG_VERSION}.tar.gz")
    set(FFMPEG_DIR "${CMAKE_SOURCE_DIR}/vendor/avlib/ffmpeg7")
endif()

if(GECND_USE_FFMPEG)
    FILE(GLOB gecnd_ffmpeg_files "${CMAKE_CURRENT_LIST_DIR}/../lib/Daemon_Media/driver_av_*.c")
    list(REMOVE_ITEM gecnd_ffmpeg_files "${CMAKE_CURRENT_LIST_DIR}/../lib/Daemon_Media/driver_av_stub.c")
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
        list(REMOVE_ITEM gecnd_ffmpeg_files "${CMAKE_CURRENT_LIST_DIR}/../lib/Daemon_Media/driver_av_dym.c")
    else()
        list(REMOVE_ITEM gecnd_ffmpeg_files "${CMAKE_CURRENT_LIST_DIR}/../lib/Daemon_Media/driver_av_static.c")
    endif()
    target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${FFMPEG_DIR})
    target_sources(${PROJECT_NAME} PRIVATE "${gecnd_ffmpeg_files}")
else()
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../lib/Daemon_Media/driver_av_stub.c")
endif()
