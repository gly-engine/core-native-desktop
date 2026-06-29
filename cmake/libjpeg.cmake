option(GECND_USE_JPEGTURBO "use libjpeg-turbo to decode jpeg straight to planar YUV" OFF)

set(JPEGTURBO_VERSION "3.0.4")
set(JPEGTURBO_REPO "https://github.com/libjpeg-turbo/libjpeg-turbo.git")
set(JPEGTURBO_DIR "${CMAKE_SOURCE_DIR}/vendor/libjpeg-turbo")
set(JPEGTURBO_BIN "${CMAKE_BINARY_DIR}/libjpeg-turbo")

if(GECND_USE_JPEGTURBO)
    if(NOT EXISTS ${JPEGTURBO_DIR})
        FetchContent_Populate(libjpeg_turbo_dep GIT_REPOSITORY ${JPEGTURBO_REPO} GIT_TAG ${JPEGTURBO_VERSION} SOURCE_DIR "${JPEGTURBO_DIR}")
    endif()
    file(MAKE_DIRECTORY ${JPEGTURBO_BIN}/include)
    ExternalProject_Add(libjpeg_turbo_proj
        SOURCE_DIR ${JPEGTURBO_DIR}
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX=${JPEGTURBO_BIN}
            -DCMAKE_INSTALL_LIBDIR=lib
            -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DENABLE_SHARED=OFF
            -DENABLE_STATIC=ON
            -DWITH_TURBOJPEG=ON
        UPDATE_COMMAND ""
        BUILD_BYPRODUCTS ${JPEGTURBO_BIN}/lib/libturbojpeg.a
    )
    add_library(turbojpeg STATIC IMPORTED)
    set_target_properties(turbojpeg PROPERTIES
        IMPORTED_LOCATION ${JPEGTURBO_BIN}/lib/libturbojpeg.a
        INTERFACE_INCLUDE_DIRECTORIES ${JPEGTURBO_BIN}/include
    )
    add_dependencies(turbojpeg libjpeg_turbo_proj)
    add_dependencies(${PROJECT_NAME} libjpeg_turbo_proj)
    target_link_libraries(${PROJECT_NAME} PRIVATE turbojpeg)
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_Img/driver_jpegturbo.c")
    set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/lib/Daemon_Img/driver_jpegturbo.c" PROPERTIES INCLUDE_DIRECTORIES "${JPEGTURBO_BIN}/include")
    set_property(SOURCE "${CMAKE_CURRENT_LIST_DIR}/lib/Frontend_Core/hypervisor.c" APPEND PROPERTY COMPILE_DEFINITIONS "GECND_USE_JPEGTURBO=1")
endif()
