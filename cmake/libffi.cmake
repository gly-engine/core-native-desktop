option(GECND_USE_FFI "fetch ffi" OFF)
set(LIBFFI_VERSION "v3.6.0")
set(LIBFFI_DIR "${CMAKE_SOURCE_DIR}/vendor/libffi")
set(LIBFFI_BIN "${CMAKE_BINARY_DIR}/libffi")
set(LIBFFI_DOWNLOAD "https://github.com/libffi/libffi/archive/refs/tags/${LIBFFI_VERSION}.tar.gz")
set(LIBFFI_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../lib/Daemon_Registry/driver_lua_ffi.c")

set(LIBFFI_HOST_ARG "")
if(ZIG_TARGET_TRIPLE)
    set(LIBFFI_HOST_ARG "--host=${ZIG_TARGET_TRIPLE}")
endif()

if (GECND_USE_FFI)
    set(LIBFFI_LIB ${LIBFFI_BIN}/lib/libffi.a)
    set(LIBFFI_INCLUDE ${LIBFFI_BIN}/include)

    ExternalProject_Add(libffi_proj
        URL ${LIBFFI_DOWNLOAD}
        BUILD_IN_SOURCE 1
        CONFIGURE_COMMAND
            bash -c "./autogen.sh && env \
                    CC=${CMAKE_C_COMPILER} \
                    CXX=${CMAKE_CXX_COMPILER} \
                    AR=${CMAKE_AR} RANLIB=${CMAKE_RANLIB} \
                ./configure \
                    ${LIBFFI_HOST_ARG} \
                    --prefix=${LIBFFI_BIN} \
                    --libdir=${LIBFFI_BIN}/lib \
                    --disable-shared \
                    --enable-static \
                    --disable-docs"
        BUILD_COMMAND make
        INSTALL_COMMAND make install
        UPDATE_COMMAND ""
        BUILD_BYPRODUCTS ${LIBFFI_LIB}
    )

    file(MAKE_DIRECTORY ${LIBFFI_INCLUDE})

    add_library(ffi STATIC IMPORTED GLOBAL)
    set_target_properties(ffi PROPERTIES
        IMPORTED_LOCATION ${LIBFFI_LIB}
        INTERFACE_INCLUDE_DIRECTORIES ${LIBFFI_INCLUDE}
    )
    add_dependencies(ffi libffi_proj)

    target_sources(${PROJECT_NAME} PRIVATE ${LIBFFI_SOURCE})
    set_source_files_properties(${LIBFFI_SOURCE} PROPERTIES COMPILE_OPTIONS "-I${LIBFFI_INCLUDE}")
    target_link_libraries(${PROJECT_NAME} PRIVATE ffi)
endif()
