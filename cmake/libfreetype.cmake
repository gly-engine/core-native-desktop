set(FREETYPE_VERSION "VER-2-13-3")
set(FREETYPE_REPO "https://github.com/freetype/freetype.git")
set(FREETYPE_DIR "${CMAKE_SOURCE_DIR}/vendor/freetype")
set(FREETYPE_BIN "${CMAKE_BINARY_DIR}/freetype")

if(GECND_USE_GL_EGL OR GECND_USE_GL_GLFW)
    if(NOT EXISTS ${FREETYPE_DIR})
        FetchContent_Populate(freetype_dep GIT_REPOSITORY ${FREETYPE_REPO} GIT_TAG ${FREETYPE_VERSION} SOURCE_DIR "${FREETYPE_DIR}")
    endif()
    file(MAKE_DIRECTORY ${FREETYPE_BIN}/include/freetype2)
    ExternalProject_Add(freetype_proj
        SOURCE_DIR ${FREETYPE_DIR}
        CMAKE_ARGS
            ${ZIG_TARGET_FLAG}
            -DCMAKE_INSTALL_PREFIX=${FREETYPE_BIN}
            -DCMAKE_INSTALL_LIBDIR=lib
            -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DBUILD_SHARED_LIBS=OFF
            -DFT_DISABLE_ZLIB=ON
            -DFT_DISABLE_BZIP2=ON
            -DFT_DISABLE_PNG=ON
            -DFT_DISABLE_HARFBUZZ=ON
            -DFT_DISABLE_BROTLI=ON
        UPDATE_COMMAND ""
        BUILD_BYPRODUCTS ${FREETYPE_BIN}/lib/libfreetype.a
    )
    add_library(freetype STATIC IMPORTED)
    set_target_properties(freetype PROPERTIES
        IMPORTED_LOCATION ${FREETYPE_BIN}/lib/libfreetype.a
        INTERFACE_INCLUDE_DIRECTORIES ${FREETYPE_BIN}/include/freetype2
    )
    add_dependencies(freetype freetype_proj)
    add_dependencies(${PROJECT_NAME} freetype_proj)
    target_link_libraries(${PROJECT_NAME} PRIVATE freetype)
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../lib/Daemon_Font/driver_freetype.c")
    set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/../lib/Daemon_Font/driver_freetype.c" PROPERTIES INCLUDE_DIRECTORIES "${FREETYPE_BIN}/include/freetype2")
endif()
