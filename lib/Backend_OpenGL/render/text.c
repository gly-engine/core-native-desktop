#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "gefilter.h"
#include "geopengl.h"

#if defined(_WIN32)
#include <windows.h>
#endif

#define FONTSTASH_IMPLEMENTATION
#include "fontstash.h"

#include "gecnd/notosans.h"
#include "gecnd/roboto_regular.h"

static int atlas_w = GE_FONT_ATLAS_SIZE; // Font area width
static int atlas_h = GE_FONT_ATLAS_SIZE; // Font area height

struct GLFONScontext {
    GLuint tex;
    int page_index;
    int width, height;
    void *scratch;
    size_t scratch_size;
};
typedef struct GLFONScontext GLFONScontext;

static GLFONScontext* g_glfons = NULL;

static int glfons__renderCreate(void* userPtr, int width, int height) {
    GLFONScontext* gl = (GLFONScontext*)userPtr;
    GLBackendState *s = geogl_get_state();
    gl->width = width; gl->height = height;
    /* Share the UI alpha8 page (made at pipeline init, holds the rounded-corner
     * mask) so glyphs and corners batch under GE_PROG_ALPHA8 with one texture.
     * fontstash owns the top-left 1024x1024; the corner sits past it. */
    gl->page_index = s->corner_page_index;
    gl->tex = s->atlas_pages.a[gl->page_index].tex_id;
    return 1;
}

static void glfons__renderUpdate(void* userPtr, int* rect, const unsigned char* data) {
    GLFONScontext* gl = (GLFONScontext*)userPtr;
    GLBackendState *s = geogl_get_state();
    int x = rect[0], y = rect[1], w = rect[2] - rect[0], h = rect[3] - rect[1];
    if (w <= 0 || h <= 0) return;
    
    // Fonts live in the top 1024 pixels of Page 0
    if (y + h > GE_FONT_ATLAS_SIZE) return; 

    ge_pipeline_flush_primitives();
    size_t needed = (size_t)(w * h);
    if (gl->scratch_size < needed) {
        gl->scratch = realloc(gl->scratch, needed); gl->scratch_size = needed;
    }
    for (int row = 0; row < h; row++) {
        const unsigned char* src = data + (y + row) * gl->width + x;
        memcpy((unsigned char*)gl->scratch + row * w, src, w);
    }
    glBindTexture(GL_TEXTURE_2D, gl->tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_ALPHA, GL_UNSIGNED_BYTE, gl->scratch);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    s->atlas_dirty = true;
}

static void glfons__renderDraw(void* userPtr, const float* verts, const float* tcoords, const unsigned int* colors, int nverts) {
    if (nverts == 0) return;
    GLFONScontext* gl = (GLFONScontext*)userPtr;
    for (int i = 0; i < nverts; i++) {
        // fontstash atlas is GE_FONT_ATLAS_SIZE; the alpha8 page is GE_ATLAS_SIZE.
        float u = tcoords[i*2]   * (float)GE_FONT_ATLAS_SIZE / (float)GE_ATLAS_SIZE;
        float v = tcoords[i*2+1] * (float)GE_FONT_ATLAS_SIZE / (float)GE_ATLAS_SIZE;
        ge_batch_add_vertex_alpha((int16_t)verts[i*2], (int16_t)verts[i*2+1], u, v, colors[i], gl->page_index);
    }
}

static void glfons__renderDelete(void* userPtr) {
    GLFONScontext* gl = (GLFONScontext*)userPtr;
    if (gl->scratch) free(gl->scratch);
}

FONScontext* glfonsCreate(int width, int height, int flags) {
    FONSparams params;
    if (g_glfons) free(g_glfons);
    g_glfons = (GLFONScontext*)malloc(sizeof(GLFONScontext));
    if (g_glfons == NULL) return NULL;
    memset(g_glfons, 0, sizeof(GLFONScontext));
    memset(&params, 0, sizeof(params));
    params.width = width; params.height = height; params.flags = (unsigned char)flags;
    params.renderCreate = glfons__renderCreate;
    params.renderUpdate = glfons__renderUpdate;
    params.renderDraw = glfons__renderDraw;
    params.renderDelete = glfons__renderDelete;
    params.userPtr = g_glfons;
    return fonsCreateInternal(&params);
}

void glfonsDelete(FONScontext* ctx) {
    fonsDeleteInternal(ctx);
    if (g_glfons) { free(g_glfons); g_glfons = NULL; }
}

unsigned int glfonsRGBA(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    return (r) | (g << 8) | (b << 16) | (a << 24);
}

static FONScontext *fs;
static int fs_font = FONS_INVALID;
static float current_size = 16.0f;
static int initialized = 0;
static unsigned char *default_font_mem;
static size_t default_font_size;

void native_text_terminate(void) {
    if (!initialized) return;
    glfonsDelete(fs); fs = NULL; initialized = 0;
}

static void ensure_init(void) {
    if (initialized) return;
    fs = glfonsCreate(atlas_w, atlas_h, FONS_ZERO_TOPLEFT);
    /* old default font
    default_font_mem  = (unsigned char*)Noto_Sans_NotoSans_Regular_ttf;
    default_font_size = Noto_Sans_NotoSans_Regular_ttf_len;
    */
    default_font_mem  = (unsigned char*)Roboto_roboto_regular_webfont_ttf;
    default_font_size = Roboto_roboto_regular_webfont_ttf_len;
    initialized = 1;
}

static void ensure_font_loaded(void) {
    if (!initialized) ensure_init();
    if (fs_font != FONS_INVALID) return;
    fs_font = fonsAddFontMem(fs, "default", default_font_mem, default_font_size, 0);
}

void native_text_set_default_font_mem(const void *data, size_t size) {
    default_font_mem = (unsigned char*)data;
    default_font_size = size;
}

void native_text_print(int16_t x, int16_t y, const char *text) {
    if (!text || !initialized) return;
    ensure_font_loaded();
    if (fs_font == FONS_INVALID) return;
    GLBackendState *state = geogl_get_state();
    GEColor c = state->current_color;
    fonsSetSize(fs, current_size);
    fonsSetFont(fs, fs_font);
    fonsSetColor(fs, glfonsRGBA(c.rgba[0], c.rgba[1], c.rgba[2], c.rgba[3]));
    fonsSetAlign(fs, FONS_ALIGN_LEFT | FONS_ALIGN_TOP);
    fonsDrawText(fs, (float)x, (float)y, text, NULL);
}

void native_text_mensure(const char *text, int16_t *w, int16_t *h)
{
    ensure_font_loaded();
    fonsSetSize(fs, current_size);
    fonsSetFont(fs, fs_font);

    if (w) {
        *w = (int16_t)fonsTextBounds(fs, 0, 0, text, NULL, NULL);
    }

    if (h) {
        float asc, desc, lineh;
        fonsVertMetrics(fs, &asc, &desc, &lineh);
        *h = (int16_t)lineh;
    }
}

void native_text_font_size(uint8_t size) {
    current_size = size ? (float)size : 16.0f;
}

void native_text_font_name(const char *path) { (void)path; }

void native_text_font_default(uint8_t index) {
    (void)index;
    if (!initialized) ensure_init();
    ensure_font_loaded();
}
