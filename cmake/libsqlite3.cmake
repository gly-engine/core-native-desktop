option(GECND_USE_SQLITE3 "Fetch and build sqlite3" ON)
option(FETCH_SQLITE3 "Fetch and build sqlite3" OFF)

set(SQLITE_VERSION "3410200") # exemplo: 3.41.2
set(SQLITE_DIR "${CMAKE_SOURCE_DIR}/vendor/sqlite3")
set(SQLITE_DOWNLOAD "https://www.sqlite.org/2023/sqlite-amalgamation-${SQLITE_VERSION}.zip")

if ((FETCH_SQLITE3 OR GECND_USE_SQLITE3) AND NOT EXISTS ${SQLITE_DIR})
    FetchContent_Populate(dep_sqlite3 URL ${SQLITE_DOWNLOAD} SOURCE_DIR ${SQLITE_DIR})
endif()

if(GECND_USE_SQLITE3)
    add_library(sqlite3-static STATIC "${SQLITE_DIR}/sqlite3.c")
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../lib/Daemon_IO/driver_db_sqlite3.c")
    target_link_libraries(${PROJECT_NAME} PRIVATE sqlite3-static)
    target_link_libraries("sqlite3-static" PRIVATE gecnd_cc_flags)
    target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${SQLITE_DIR})
endif()
