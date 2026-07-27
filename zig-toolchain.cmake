set(ZIG_VERSION "0.16.0")
set(ZIG_DIR "${CMAKE_CURRENT_LIST_DIR}/vendor/zig")
set(ZIG_BIN "${CMAKE_BINARY_DIR}")

if(${CMAKE_HOST_SYSTEM_NAME} STREQUAL "Darwin")
    set(ZIG_HOST_OS "macos")
elseif(${CMAKE_HOST_SYSTEM_NAME} STREQUAL "Linux")
    set(ZIG_HOST_OS "linux")
else()
    message(FATAL_ERROR "Unsupported platform: ${CMAKE_HOST_SYSTEM_NAME}")
endif()

if(NOT DEFINED TARGET)
    message(FATAL_ERROR "zig-toolchain: TARGET is required, e.g. -DTARGET=arm-linux-gnueabihf-cortex_a7-neon-o3")
endif()

set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES TARGET)

set(ZIG_TARGET_TRIPLE "")
set(ZIG_TARGET_CPU "")
set(ZIG_TARGET_FPU "")
set(ZIG_TARGET_OPT "")
set(ZIG_TARGET_ARGS "")
set(ZIG_TARGET_FLAG "-DTARGET=${TARGET}")

set(ZIG_TARGET "${TARGET}")

if(ZIG_TARGET MATCHES "-o([0-9]+)$")
    set(ZIG_TARGET_OPT "${CMAKE_MATCH_1}")
    string(REGEX REPLACE "-o[0-9]+$" "" ZIG_TARGET "${ZIG_TARGET}")
endif()

string(REPLACE "-" ";" TARGET_PARTS "${ZIG_TARGET}")
list(LENGTH TARGET_PARTS TARGET_LEN)

if(TARGET_LEN GREATER 4)
    list(GET TARGET_PARTS -1 ZIG_TARGET_FPU)
    list(REMOVE_AT TARGET_PARTS -1)
endif()

if(TARGET_LEN GREATER 3)
    list(SUBLIST TARGET_PARTS 3 -1 TARGET_EXTRAS)
    list(SUBLIST TARGET_PARTS 0 3 TARGET_PARTS)
    foreach(TARGET_EXTRA IN LISTS TARGET_EXTRAS)
        if(ZIG_TARGET_CPU STREQUAL "")
            set(ZIG_TARGET_CPU "${TARGET_EXTRA}")
        else()
            set(ZIG_TARGET_CPU "${ZIG_TARGET_CPU}+${TARGET_EXTRA}")
        endif()
    endforeach()
endif()

string(REPLACE ";" "-" ZIG_TARGET_TRIPLE "${TARGET_PARTS}")
string(REPLACE "gnueabi." "gnueabi" ZIG_TARGET_TRIPLE "${ZIG_TARGET_TRIPLE}")

string(FIND "${ZIG_TARGET_TRIPLE}" "64" ZIG_TARGET_IS64_POS)
if(ZIG_TARGET_IS64_POS GREATER -1)
    set(ZIG_HOST_ARCH "x86_64")
else()
    set(ZIG_HOST_ARCH "x86")
endif()
set(ZIG_HOST_TARGET "${ZIG_HOST_ARCH}-${ZIG_HOST_OS}-musl")

set(ZIG_TARGET_ARGS "-target ${ZIG_TARGET_TRIPLE}")

if(ZIG_TARGET_CPU)
    set(ZIG_TARGET_ARGS "${ZIG_TARGET_ARGS} -mcpu=${ZIG_TARGET_CPU}")
endif()

if(ZIG_TARGET_FPU)
    set(ZIG_TARGET_ARGS "${ZIG_TARGET_ARGS} -mfpu=${ZIG_TARGET_FPU}")
endif()

if(ZIG_TARGET_OPT)
    set(ZIG_TARGET_ARGS "${ZIG_TARGET_ARGS} -O${ZIG_TARGET_OPT}")
endif()

if(NOT EXISTS "${ZIG_BIN}/zig-cc")
    file(MAKE_DIRECTORY "${ZIG_DIR}")
    file(MAKE_DIRECTORY "${ZIG_BIN}")

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
    set(ZIG_NAME "zig-${CMAKE_HOST_SYSTEM_PROCESSOR}-${ZIG_HOST_OS}-${ZIG_VERSION}")
    set(ZIG_TAR "${ZIG_DIR}/${ZIG_NAME}.tar.xz")
    set(ZIG_DOWNLOAD "https://ziglang.org/download/${ZIG_VERSION}/${ZIG_NAME}.tar.xz")

    if(NOT EXISTS "${ZIG_TAR}" AND NOT EXISTS "${ZIG_DIR}/${ZIG_NAME}")
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

    if(NOT EXISTS "${ZIG_DIR}/${ZIG_NAME}")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xJf "${ZIG_TAR}"
            WORKING_DIRECTORY "${ZIG_DIR}"
            RESULT_VARIABLE tar_result
        )
        if(NOT tar_result EQUAL 0)
            message(FATAL_ERROR "failed extracting zig")
        endif()

        file(REMOVE "${ZIG_TAR}")
    endif()

    find_program(ZIG_EXECUTABLE zig PATHS "${ZIG_DIR}/${ZIG_NAME}" REQUIRED NO_DEFAULT_PATH)

    function(create_zig_script name cmd extra)
        file(WRITE "${ZIG_BIN}/${name}" "#!/bin/sh\n\"${ZIG_EXECUTABLE}\" ${cmd} ${extra} \"\$@\"\n")
        file(CHMOD "${ZIG_BIN}/${name}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
    endfunction()

    set(ZIG_NO_SANITIZE "-fno-sanitize=all -fno-sanitize-recover=all")
    create_zig_script(zig-cc cc "${ZIG_TARGET_ARGS} ${ZIG_NO_SANITIZE}")
    create_zig_script(zig-cxx c++ "${ZIG_TARGET_ARGS} ${ZIG_NO_SANITIZE}")
    create_zig_script(zig-cc-host cc "${ZIG_NO_SANITIZE}")
    create_zig_script(zig-cxx-host c++ "${ZIG_NO_SANITIZE}")
    create_zig_script(zig-ar ar "")
    create_zig_script(zig-ranlib ranlib "")
endif()

set(CMAKE_C_COMPILER "${ZIG_BIN}/zig-cc" CACHE FILEPATH "C compiler")
set(CMAKE_CXX_COMPILER "${ZIG_BIN}/zig-cxx" CACHE FILEPATH "C++ compiler")
set(CMAKE_AR "${ZIG_BIN}/zig-ar" CACHE FILEPATH "Archiver")
set(CMAKE_RANLIB "${ZIG_BIN}/zig-ranlib" CACHE FILEPATH "Ranlib")
set(CMAKE_C_ARCHIVE_CREATE "<CMAKE_C_COMPILER> -r <OBJECTS> -o <TARGET>")
set(CMAKE_C_ARCHIVE_FINISH "")
set(CMAKE_CXX_ARCHIVE_CREATE "<CMAKE_CXX_COMPILER> -r <OBJECTS> -o <TARGET>")
set(CMAKE_CXX_ARCHIVE_FINISH "")
