#include "geopengl.h"

static int  last_prog       = -1;
static bool last_opaque     = false;
static int  last_page_index = -1;

void ge_zindex_reset(void) {
    GLBackendState *s = geogl_get_state();
    s->current_z    = 0;
    last_prog       = -1;
    last_opaque     = false;
    last_page_index = -1;
}

int16_t ge_zindex_get(GEProgramType prog, bool opaque, int page_index) {
    GLBackendState *s = geogl_get_state();
    if (last_prog != (int)prog || last_opaque != opaque || last_page_index != page_index) {
        if (last_prog != -1 && s->current_z < GE_MAX_LAYERS - 1)
            s->current_z++;
        last_prog       = (int)prog;
        last_opaque     = opaque;
        last_page_index = page_index;
    }
    return s->current_z;
}
