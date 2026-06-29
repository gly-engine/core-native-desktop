option(GECND_USE_WUFFS "use wuffs for fast PNG decode" OFF)

set(WUFFS_VERSION "0.3.4")
set(WUFFS_DIR "${CMAKE_SOURCE_DIR}/vendor/mono/wuffs")
set(WUFFS_DOWNLOAD "https://github.com/google/wuffs-mirror-release-c/archive/refs/tags/v${WUFFS_VERSION}.tar.gz")

if(GECND_USE_WUFFS)
    if(NOT EXISTS "${WUFFS_DIR}")
        FetchContent_Populate(wuffs URL "${WUFFS_DOWNLOAD}" SOURCE_DIR "${WUFFS_DIR}")
    endif()
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../lib/Daemon_Img/driver_wuffs.c")
    set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/../lib/Daemon_Img/driver_wuffs.c" PROPERTIES INCLUDE_DIRECTORIES "${WUFFS_DIR}/release/c")
endif()
