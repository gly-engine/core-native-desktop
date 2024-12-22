#include "zeebo.h"

static char error_message[1048];
static uint16_t error_index;

void kernel_add_error(const char *message) {
    error_index += snprintf(&error_message[error_index], sizeof(error_message), "%s\n", message);
}

bool kernel_has_error() {
    return error_index > 0;
}

const char *const kernel_get_error() {
    return error_message;
}

void kernel_add_error_traceback(lua_State *L)
{
    const char *lua_error = lua_tostring(L, -1);
    error_index += snprintf(&error_message[error_index], sizeof(error_message) - error_index, "lua error:\n\t%s\n", lua_error);
}
