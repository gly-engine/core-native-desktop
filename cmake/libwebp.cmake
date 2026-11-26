set(LIBWEBP_VERSION "v1.5.0")
set(LIBWEBP_REPO "https://github.com/webmproject/libwebp.git")
set(LIBWEBP_DIR "${CMAKE_SOURCE_DIR}/vendor/libwebp")
set(LIBWEBP_BIN "${CMAKE_BINARY_DIR}/libwebp")

if(GECND_USE_LIBWEBP)
    if(NOT EXISTS ${LIBWEBP_DIR})
        FetchContent_Populate(libwebp_dep GIT_REPOSITORY ${LIBWEBP_REPO} GIT_TAG ${LIBWEBP_VERSION} SOURCE_DIR "${LIBWEBP_DIR}")
    endif()
    file(MAKE_DIRECTORY ${LIBWEBP_BIN}/include/webp)
    ExternalProject_Add(libwebp_proj
        SOURCE_DIR ${LIBWEBP_DIR}
        CMAKE_ARGS
            ${ZIG_TARGET_FLAG}
            -DCMAKE_INSTALL_PREFIX=${LIBWEBP_BIN}
            -DCMAKE_INSTALL_LIBDIR=lib
            -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DBUILD_SHARED_LIBS=OFF
            -DWEBP_BUILD_ANIM_UTILS=OFF
            -DWEBP_BUILD_CWEBP=OFF
            -DWEBP_BUILD_DWEBP=OFF
            -DWEBP_BUILD_GIF2WEBP=OFF
            -DWEBP_BUILD_IMG2WEBP=OFF
            -DWEBP_BUILD_VWEBP=OFF
            -DWEBP_BUILD_WEBPINFO=OFF
            -DWEBP_BUILD_WEBPMUX=OFF
            -DWEBP_BUILD_EXTRAS=OFF
            -DWEBP_BUILD_LIBWEBPMUX=OFF
            -DWEBP_USE_THREAD=OFF
        UPDATE_COMMAND ""
        INSTALL_COMMAND
            ${CMAKE_COMMAND} -E make_directory ${LIBWEBP_BIN}/lib
            COMMAND ${CMAKE_COMMAND} -E make_directory ${LIBWEBP_BIN}/include/webp
            COMMAND ${CMAKE_COMMAND} -E copy
                <BINARY_DIR>/libwebpdecoder.a
                ${LIBWEBP_BIN}/lib/libwebpdecoder.a
            COMMAND ${CMAKE_COMMAND} -E copy
                <SOURCE_DIR>/src/webp/decode.h
                <SOURCE_DIR>/src/webp/types.h
                ${LIBWEBP_BIN}/include/webp/
        BUILD_BYPRODUCTS ${LIBWEBP_BIN}/lib/libwebpdecoder.a
        BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target webpdecoder
    )
    add_library(webpdecoder STATIC IMPORTED)
    set_target_properties(webpdecoder PROPERTIES
        IMPORTED_LOCATION ${LIBWEBP_BIN}/lib/libwebpdecoder.a
        INTERFACE_INCLUDE_DIRECTORIES ${LIBWEBP_BIN}/include
    )
    add_dependencies(webpdecoder libwebp_proj)
    add_dependencies(${PROJECT_NAME} libwebp_proj)
    target_link_libraries(${PROJECT_NAME} PRIVATE webpdecoder)
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../lib/Daemon_Img/driver_libwebp.c")
    set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/../lib/Daemon_Img/driver_libwebp.c" PROPERTIES INCLUDE_DIRECTORIES "${LIBWEBP_BIN}/include")
endif()
