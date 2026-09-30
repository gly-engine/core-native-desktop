set(WARMCAT_VERSION "4.5.8")
set(WARMCAT_DOWNLOAD "https://github.com/warmcat/libwebsockets/archive/refs/tags/v${WARMCAT_VERSION}.tar.gz")
set(WARMCAT_DIR "${CMAKE_SOURCE_DIR}/vendor/warmcat")
set(WARMCAT_BIN "${CMAKE_BINARY_DIR}/warmcat")

set(MBEDTLS_VERSION "mbedtls-3.6.4")
set(MBEDTLS_REPO "https://github.com/Mbed-TLS/mbedtls.git")
set(MBEDTLS_DIR "${CMAKE_SOURCE_DIR}/vendor/web/mbedtls")
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)

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
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../lib/Daemon_Web/driver_server_uv_warmcat.c")
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../lib/Daemon_Web/driver_client_uv_warmcat.c")
endif()

if((GECND_USE_WARMCAT) AND GECND_USE_MBEDTLS)
    target_link_libraries(${PROJECT_NAME} PRIVATE mbedtls mbedx509 mbedcrypto)
    set_source_files_properties(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../lib/Daemon_Web/driver_server_uv_warmcat.c" PROPERTIES COMPILE_DEFINITIONS "GECND_HAS_SSL=1")
    set_source_files_properties(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../lib/Daemon_Web/driver_client_uv_warmcat.c" PROPERTIES COMPILE_DEFINITIONS "GECND_HAS_SSL=1")
endif()
