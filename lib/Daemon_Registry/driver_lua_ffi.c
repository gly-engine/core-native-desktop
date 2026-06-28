#include <stdio.h>
#include <string.h>
#include <ffi.h>
#include <lua.h>
#ifdef LUAU_FASTMATH_BEGIN
#include <lualib.h>
#else
#include <lauxlib.h>
#endif

#include "gecnd.h"

#define LUA_FFI_MAX_ARGS  16
#define LUA_FFI_NAME_MAX  64
#define LUA_FFI_GROUP_ARGS 0

typedef struct {
    void        *func;
    ffi_cif      cif;
    ffi_type    *arg_types[LUA_FFI_MAX_ARGS];
    gecnd_type_t kinds[LUA_FFI_MAX_ARGS];
    bool         is_ret[LUA_FFI_MAX_ARGS];
    unsigned     nargs;
    char         name[LUA_FFI_NAME_MAX];
} lua_ffi_closure_t;

static ffi_type *lua_ffi_type(gecnd_type_t kind) {
    switch (kind) {
        case GECND_TYPE_VOID:    return &ffi_type_void;
        case GECND_TYPE_BOOLEAN: return &ffi_type_uint8;
        case GECND_TYPE_STRING:  return &ffi_type_pointer;
        case GECND_TYPE_U8:      return &ffi_type_uint8;
        case GECND_TYPE_U16:     return &ffi_type_uint16;
        case GECND_TYPE_U32:     return &ffi_type_uint32;
        case GECND_TYPE_U64:     return &ffi_type_uint64;
        case GECND_TYPE_I8:      return &ffi_type_sint8;
        case GECND_TYPE_I16:     return &ffi_type_sint16;
        case GECND_TYPE_I32:     return &ffi_type_sint32;
        case GECND_TYPE_I64:     return &ffi_type_sint64;
        case GECND_TYPE_F32:     return &ffi_type_float;
        case GECND_TYPE_F64:     return &ffi_type_double;
        default:                 return NULL;
    }
}

static int lua_ffi_dispatch(lua_State *L) {
    lua_ffi_closure_t *c = (lua_ffi_closure_t *)lua_touserdata(L, lua_upvalueindex(1));

    gly_any_t slots[LUA_FFI_MAX_ARGS] = {0};
    gly_any_t store[LUA_FFI_MAX_ARGS] = {0};
    void     *avalue[LUA_FFI_MAX_ARGS];
    int       argn = 1;

    for (unsigned i = 0; i < c->nargs; i++) {
        if (c->is_ret[i]) {
            slots[i].ptr = &store[i];
            avalue[i] = &slots[i];
            continue;
        }
        switch (c->kinds[i]) {
            case GECND_TYPE_BOOLEAN: slots[i].u8  = lua_toboolean(L, argn++) ? 1 : 0;          break;
            case GECND_TYPE_STRING:  slots[i].ptr = (void *)luaL_checkstring(L, argn++);        break;
            case GECND_TYPE_U8:      slots[i].u8  = (uint8_t) luaL_checkinteger(L, argn++);      break;
            case GECND_TYPE_U16:     slots[i].u16 = (uint16_t)luaL_checkinteger(L, argn++);      break;
            case GECND_TYPE_U32:     slots[i].u32 = (uint32_t)luaL_checkinteger(L, argn++);      break;
            case GECND_TYPE_U64:     slots[i].u64 = (uint64_t)luaL_checkinteger(L, argn++);      break;
            case GECND_TYPE_I8:      slots[i].i8  = (int8_t)  luaL_checkinteger(L, argn++);      break;
            case GECND_TYPE_I16:     slots[i].i16 = (int16_t) luaL_checkinteger(L, argn++);      break;
            case GECND_TYPE_I32:     slots[i].i32 = (int32_t) luaL_checkinteger(L, argn++);      break;
            case GECND_TYPE_I64:     slots[i].i64 = (int64_t) luaL_checkinteger(L, argn++);      break;
            case GECND_TYPE_F32:     slots[i].f32 = (float)   luaL_checknumber(L, argn++);       break;
            case GECND_TYPE_F64:     slots[i].f64 = (double)  luaL_checknumber(L, argn++);       break;
            default:                 break;
        }
        avalue[i] = &slots[i];
    }

    void *ret = NULL;
    ffi_call(&c->cif, FFI_FN(c->func), &ret, avalue);

    if (ret) {
        luaL_error(L, "[%s] %s", c->name, (const char *)ret);
        return 0;
    }

    int pushed = 0;
    for (unsigned i = 0; i < c->nargs; i++) {
        if (!c->is_ret[i]) {
            continue;
        }
        switch (c->kinds[i]) {
            case GECND_TYPE_BOOLEAN: lua_pushboolean(L, store[i].u8 != 0);          break;
            case GECND_TYPE_STRING:  lua_pushstring (L, (const char *)store[i].ptr); break;
            case GECND_TYPE_U8:      lua_pushinteger(L, (lua_Integer)store[i].u8);   break;
            case GECND_TYPE_U16:     lua_pushinteger(L, (lua_Integer)store[i].u16);  break;
            case GECND_TYPE_U32:     lua_pushinteger(L, (lua_Integer)store[i].u32);  break;
            case GECND_TYPE_U64:     lua_pushinteger(L, (lua_Integer)store[i].u64);  break;
            case GECND_TYPE_I8:      lua_pushinteger(L, (lua_Integer)store[i].i8);   break;
            case GECND_TYPE_I16:     lua_pushinteger(L, (lua_Integer)store[i].i16);  break;
            case GECND_TYPE_I32:     lua_pushinteger(L, (lua_Integer)store[i].i32);  break;
            case GECND_TYPE_I64:     lua_pushinteger(L, (lua_Integer)store[i].i64);  break;
            case GECND_TYPE_F32:     lua_pushnumber (L, (lua_Number)store[i].f32);   break;
            case GECND_TYPE_F64:     lua_pushnumber (L, (lua_Number)store[i].f64);   break;
            default:                 lua_pushnil    (L);                             break;
        }
        pushed++;
    }
    return pushed;
}

static void lua_global_ffi(const char *name, void *func, gecnd_t *const gly) {
    lua_State        *L = gly->L;
    gecnd_lang_rdsl_t ctx = {0};

    const char  *fn_name = NULL;
    size_t       fn_len  = 0;
    gecnd_type_t kinds[LUA_FFI_MAX_ARGS];
    bool         is_ret[LUA_FFI_MAX_ARGS];
    ffi_type    *arg_types[LUA_FFI_MAX_ARGS];
    unsigned     nargs = 0;

    while (gecnd_lang_rdsl_iterator(&ctx, name, NULL)) {
        if (ctx.typeidx == -1) {
            if (ctx.keyidx == 1) {
                fn_name = ctx.ptr;
                fn_len  = ctx.len;
            }
            continue;
        }

        if (ctx.kind == GECND_TYPE_VOID) {
            continue;
        }

        ffi_type *type = lua_ffi_type(ctx.kind);
        if (!type) {
            gecnd_add_error(gly, "[%s] %s", name, "unsupported type!");
            return;
        }

        if (nargs >= LUA_FFI_MAX_ARGS) {
            gecnd_add_error(gly, "[%s] %s", name, "too many arguments!");
            return;
        }

        bool ret = (ctx.plusidx != LUA_FFI_GROUP_ARGS);
        kinds[nargs]     = ctx.kind;
        is_ret[nargs]    = ret;
        arg_types[nargs] = ret ? &ffi_type_pointer : type;
        nargs++;
    }

    if (ctx.error) {
        gecnd_add_error(gly, "[%s] %s", name, "invalid syntax!");
        return;
    }

    if (!fn_name || fn_len == 0 || fn_len >= LUA_FFI_NAME_MAX) {
        gecnd_add_error(gly, "[%s] %s", name, "invalid function name!");
        return;
    }

    if (!func) {
        gecnd_add_error(gly, "[%.*s] %s", (int)fn_len, fn_name, "function not bound!");
        return;
    }

    lua_ffi_closure_t *c = (lua_ffi_closure_t *)lua_newuserdata(L, sizeof(*c));
    c->func  = func;
    c->nargs = nargs;
    memcpy(c->kinds, kinds, sizeof(kinds));
    memcpy(c->is_ret, is_ret, sizeof(is_ret));
    memcpy(c->arg_types, arg_types, sizeof(arg_types));
    memcpy(c->name, fn_name, fn_len);
    c->name[fn_len] = '\0';

    if (ffi_prep_cif(&c->cif, FFI_DEFAULT_ABI, nargs, &ffi_type_pointer, c->arg_types) != FFI_OK) {
        gecnd_add_error(gly, "[%s] %s", c->name, "ffi_prep_cif failed!");
        lua_pop(L, 1);
        return;
    }

#ifdef LUAU_FASTMATH_BEGIN
    lua_pushcclosure(L, lua_ffi_dispatch, c->name, 1);
#else
    lua_pushcclosure(L, lua_ffi_dispatch, 1);
#endif
    lua_setglobal(L, c->name);
}

const char *testing(uint8_t foo, uint8_t *const bar) {
    printf("FFI function %d\n", foo);
    *bar = foo + 1;
    return NULL;
}

__attribute__((constructor))
static void init() {
    gecnd_registry("set", "lua_global_ffi:testing+$u8+$u8", testing, NULL);
    gecnd_registry("set", "function:lua_global_ffi", lua_global_ffi, NULL);
}
