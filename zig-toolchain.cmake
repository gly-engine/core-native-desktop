if(NOT EXISTS "${ZIG_DIR}/zig/zig-cc")
    file(MAKE_DIRECTORY "${ZIG_DIR}")

    if(NOT CMAKE_HOST_SYSTEM_PROCESSOR)
        execute_process(
            COMMAND uname -m
            OUTPUT_VARIABLE ARCH
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(ARCH)
            set(CMAKE_HOST_SYSTEM_PROCESSOR "${ARCH}")
        else()
            message(WARNING "host system processor not detected. Defaulting to x86_64")
            set(CMAKE_HOST_SYSTEM_PROCESSOR "x86_64")
        endif()
    endif()
    string(TOLOWER "${CMAKE_HOST_SYSTEM_NAME}" CMAKE_HOST_SYSTEM_NAME_LOWER)

    if(${CMAKE_HOST_SYSTEM_NAME} STREQUAL "Darwin")
        set(OS "macos")
    elseif(${CMAKE_HOST_SYSTEM_NAME} STREQUAL "Linux")
        set(OS "linux")
    else()
        message(FATAL_ERROR "Unsupported platform: ${CMAKE_HOST_SYSTEM_NAME}")
    endif()

    set(ZIG_VERSION "0.15.2")
    set(ZIG_DIR "${CMAKE_CURRENT_LIST_DIR}/vendor/zig")
    set(ZIG_NAME "zig-${CMAKE_HOST_SYSTEM_PROCESSOR}-${OS}-${ZIG_VERSION}")
    set(ZIG_TAR "${ZIG_DIR}/${ZIG_NAME}.tar.xz")
    set(ZIG_DOWNLOAD "https://ziglang.org/download/${ZIG_VERSION}/${ZIG_NAME}.tar.xz")

    if (NOT EXISTS "${ZIG_TAR}")
        file(DOWNLOAD
            "${ZIG_DOWNLOAD}"
            "${ZIG_TAR}"
            SHOW_PROGRESS
            STATUS download_status
            LOG log
        )
        list(GET download_status 0 status_code)
        if(NOT status_code EQUAL 0)
            message(FATAL_ERROR "failed downloading zig: ${log}")
        endif()
    endif()

    if (NOT EXISTS "${ZIG_DIR}/${ZIG_NAME}")
            execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xJf "${ZIG_TAR}"
            WORKING_DIRECTORY "${ZIG_DIR}"
            RESULT_VARIABLE tar_result
        )
        if(NOT tar_result EQUAL 0)
            message(FATAL_ERROR "failed extracting zig")
        endif()
    endif()

    find_program(ZIG_EXECUTABLE zig PATHS "${ZIG_DIR}/${ZIG_NAME}" REQUIRED NO_DEFAULT_PATH)
    function(create_zig_script name cmd)
        file(WRITE "${ZIG_DIR}/${name}" "#!/bin/sh\n\"${ZIG_EXECUTABLE}\" ${cmd} \"\$@\"\n")
        file(CHMOD "${ZIG_DIR}/${name}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
    endfunction()
    create_zig_script(zig-cc cc)
    create_zig_script(zig-ar ar)
    create_zig_script(zig-ranlib ranlib)
endif()

set(CMAKE_C_COMPILER "${ZIG_DIR}/zig-cc" CACHE FILEPATH "C compiler")
set(CMAKE_C_COMPILER_ID ZIG CACHE STRING "C compiler ID")
set(CMAKE_AR "${ZIG_DIR}/zig-ar" CACHE FILEPATH "Archiver")
set(CMAKE_RANLIB "${ZIG_DIR}/zig-ranlib" CACHE FILEPATH "Ranlib")
set(CMAKE_C_ARCHIVE_CREATE "<CMAKE_C_COMPILER> -r <OBJECTS> -o <TARGET>")
set(CMAKE_C_ARCHIVE_FINISH "")