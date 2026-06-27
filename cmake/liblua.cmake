option(GECND_USE_LUA51 "prefer lua 5.1 instead lua 5.4" OFF)
option(GECND_USE_LUAJIT "prefer lua jit instead lua 5.4" OFF)
option(GECND_USE_LUA32BITS "lua puc uses float instead double" OFF)
option(GECND_USE_LUA_CJSON "add cjson library to lua" ON)
option(GECND_USE_LUA_BASE64 "add base64 library to lua" ON)

add_library(gecnd_lua INTERFACE)
target_link_libraries(${PROJECT_NAME} PRIVATE gecnd_lua)

set(LUA54_VERSION "v5.4.7")
set(LUA54_DIR "${CMAKE_SOURCE_DIR}/vendor/lua/lua54")
set(LUA54_HOST_DIR "${CMAKE_SOURCE_DIR}/vendor/lua/lua54_host")
set(LUA54_DOWNLOAD "https://github.com/lua/lua/archive/refs/tags/${LUA54_VERSION}.tar.gz")

set(LUA51_VERSION "v5.1")
set(LUA51_DIR "${CMAKE_SOURCE_DIR}/vendor/lua/lua51")
set(LUA51_DOWNLOAD "https://github.com/lua/lua/archive/refs/tags/${LUA51_VERSION}.tar.gz")

set(LUAJIT_VERSION "v2.0.5")
set(LUAJIT_DIR "${CMAKE_SOURCE_DIR}/vendor/lua/luajit")
set(LUAJIT_LIB1 "${CMAKE_SOURCE_DIR}/vendor/lua/luajit/src/libluajit.a")
set(LUAJIT_LIB2 "${CMAKE_BINARY_DIR}/libluajit.a")
set(LUAJIT_DOWNLOAD "https://github.com/luajit/luajit/archive/refs/tags/${LUAJIT_VERSION}.tar.gz")

set(LUACJSON_VERSION "2.1.0.9")
set(LUACJSON_DIR "${CMAKE_SOURCE_DIR}/vendor/lua/cjson")
set(LUACJSON_DOWNLOAD "https://github.com/openresty/lua-cjson/archive/refs/tags/${LUACJSON_VERSION}.tar.gz")

set(BASE64_VERSION "0.5.2")
set(BASE64_DIR "${CMAKE_SOURCE_DIR}/vendor/base64")
set(BASE64_REPO "https://github.com/aklomp/base64")

if(NOT (EXISTS "${CMAKE_BINARY_DIR}/lua" OR EXISTS "${CMAKE_BINARY_DIR}/lua.exe"))
    FetchContent_Populate(lua54-host URL "${LUA54_DOWNLOAD}" SOURCE_DIR ${LUA54_HOST_DIR})
    if(TARGET)
        execute_process(
            COMMAND ${ZIG_DIR}/zig-cc-host -DMAKE_LUA onelua.c -o ${CMAKE_BINARY_DIR}/lua -lm
            WORKING_DIRECTORY ${LUA54_HOST_DIR}
        )
    else()
        execute_process(
            COMMAND ${CMAKE_C_COMPILER} -DMAKE_LUA onelua.c -o ${CMAKE_BINARY_DIR}/lua -lm
            WORKING_DIRECTORY ${LUA54_HOST_DIR}
        )
    endif()
endif()
#if(NOT EXISTS "${GLY_CLI}")
#    FetchContent_Populate(dep_gly-engine URL ${GLY_DOWNLOAD} SOURCE_DIR ${GLY_DIR})
#endif()
find_program(LUA_BIN NAMES lua PATHS ${CMAKE_BINARY_DIR} NO_DEFAULT_PATH REQUIRED)

if(GECND_USE_LUA51)
    FetchContent_Populate(lua51-static URL "${LUA51_DOWNLOAD}" SOURCE_DIR ${LUA51_DIR})
    file(GLOB lua_files "${LUA51_DIR}/*.c")
    list(REMOVE_ITEM lua_files "${LUA51_DIR}/lua.c")
    add_library(lua51-static STATIC ${lua_files})
    target_link_libraries(${PROJECT_NAME} PRIVATE lua51-static)
    target_link_libraries(lua51-static PRIVATE gecnd_cc_flags)
    target_include_directories(gecnd_lua INTERFACE "${LUA51_DIR}")
endif()

if(GECND_USE_LUAJIT)
    FetchContent_Populate(dep_luajit URL "${LUAJIT_DOWNLOAD}" SOURCE_DIR ${LUAJIT_DIR})
    if(TARGET)
        add_library(gcc_float STATIC ${CMAKE_CURRENT_LIST_DIR}/../lib/ThirdParty_Compat/gcc_float.c)
        target_link_libraries(gcc_float PRIVATE gecnd_cc_flags)
        target_link_libraries(${PROJECT_NAME} PRIVATE gcc_float)
        add_custom_command(
            OUTPUT ${LUAJIT_LIB2}
            COMMAND make
                HOST_CC=${ZIG_DIR}/zig-cc-host
                HOST_CFLAGS="--target=x86-linux-musl"
                HOST_LDFLAGS="--target=x86-linux-musl"
                CC=${ZIG_DIR}/zig-cc
                XCFLAGS="-DLUAJIT_NO_UNWIND"
                TARGET_SYS=Linux
                TARGET_STRIP=eu-strip
                TARGET_LDFLAGS="$<TARGET_FILE:gcc_float>"
                BUILDMODE=static
            COMMAND ${CMAKE_COMMAND} -E rename ${LUAJIT_LIB1} "${LUAJIT_LIB2}"
            COMMAND make clean
            WORKING_DIRECTORY ${LUAJIT_DIR}
            COMMENT "Building LuaJIT (static)"
            DEPENDS gcc_float
        )
    else()
        add_custom_command(
            OUTPUT ${LUAJIT_LIB2}
            COMMAND make
            COMMAND ${CMAKE_COMMAND} -E rename ${LUAJIT_LIB1} "${LUAJIT_LIB2}"
            COMMAND make clean
            WORKING_DIRECTORY ${LUAJIT_DIR}
            COMMENT "Building LuaJIT (static)"
        )
    endif()
    add_custom_target(luajit-build DEPENDS ${LUAJIT_LIB2})   
    add_library(luajit-static STATIC IMPORTED GLOBAL)
    set_target_properties(luajit-static PROPERTIES
        IMPORTED_LOCATION ${LUAJIT_LIB2}
        INTERFACE_INCLUDE_DIRECTORIES ${LUAJIT_DIR}/src
    )
    add_dependencies(luajit-static luajit-build)
    target_link_libraries(${PROJECT_NAME} PRIVATE luajit-static)
    target_include_directories(gecnd_lua INTERFACE "${LUAJIT_DIR}/src")
endif()

if(NOT GECND_USE_LUA51 AND NOT GECND_USE_LUAJIT)
    FetchContent_Populate(lua54-static URL "${LUA54_DOWNLOAD}" SOURCE_DIR ${LUA54_DIR})
    file(GLOB lua_files "${LUA54_DIR}/*.c")
    list(REMOVE_ITEM lua_files "${LUA54_DIR}/lua.c")
    list(REMOVE_ITEM lua_files "${LUA54_DIR}/onelua.c")
    add_library(lua54-static STATIC ${lua_files})
    target_link_libraries(${PROJECT_NAME} PRIVATE lua54-static)
    target_link_libraries(lua54-static PRIVATE gecnd_cc_flags)
    target_include_directories(gecnd_lua INTERFACE "${LUA54_DIR}")
endif()

if(GECND_USE_LUA_CJSON)
    if(NOT EXISTS ${LUACJSON_DIR})
        FetchContent_Populate(lua_cjson URL ${LUACJSON_DOWNLOAD} SOURCE_DIR ${LUACJSON_DIR})
    endif()
    add_library(lua_cjson-static STATIC "${LUACJSON_DIR}/lua_cjson.c;${LUACJSON_DIR}/strbuf.c;${LUACJSON_DIR}/fpconv.c")
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../lib/ThirdParty_Api/json.c")
    target_link_libraries(${PROJECT_NAME} PRIVATE lua_cjson-static)
    target_link_libraries("lua_cjson-static" PRIVATE gecnd_cc_flags)
    target_link_libraries("lua_cjson-static" PRIVATE gecnd_lua)
endif()

if(GECND_USE_LUA_BASE64)
    FetchContent_Declare(base64 GIT_REPOSITORY ${BASE64_REPO} GIT_TAG v${BASE64_VERSION} SOURCE_DIR ${BASE64_DIR_DIR})
    FetchContent_MakeAvailable(base64)    
    target_link_libraries(${PROJECT_NAME} PRIVATE base64)
    target_link_libraries(base64 PRIVATE gecnd_cc_flags)
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../lib/ThirdParty_Api/base64.c")
endif()
