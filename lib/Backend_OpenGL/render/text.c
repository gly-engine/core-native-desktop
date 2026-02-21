#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "gefilter.h"
#include "geopengl.h"

#define FONTSTASH_IMPLEMENTATION
#include "fontstash.h"

#include "gecnd/notosans.h"

struct GLFONScontext {
    GLuint tex;
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
    gl->tex = s->atlas_id;
    return 1;
}

static void glfons__renderUpdate(void* userPtr, int* rect, const unsigned char* data) {
    GLFONScontext* gl = (GLFONScontext*)userPtr;
    GLBackendState *s = geogl_get_state();
    int x = rect[0], y = rect[1], w = rect[2] - rect[0], h = rect[3] - rect[1];
    if (w <= 0 || h <= 0) return;
    if (s->batch_count > 0) ge_pipeline_flush_primitives();
    size_t needed = (size_t)(w * h * 4);
    if (gl->scratch_size < needed) {
        gl->scratch = realloc(gl->scratch, needed); gl->scratch_size = needed;
    }
    for (int row = 0; row < h; row++) {
        const unsigned char* src = data + (y + row) * gl->width + x;
        unsigned char* dst = (unsigned char*)gl->scratch + row * w * 4;
        for (int col = 0; col < w; col++) {
            dst[col*4+0] = 255; dst[col*4+1] = 255; dst[col*4+2] = 255; dst[col*4+3] = src[col];
        }
    }
    glBindTexture(GL_TEXTURE_2D, gl->tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, gl->scratch);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

static void glfons__renderDraw(void* userPtr, const float* verts, const float* tcoords, const unsigned int* colors, int nverts) {
    if (nverts == 0) return;
    GLBackendState *s = geogl_get_state();
    if (s->batch_count + nverts >= GE_MAX_VERTICES) ge_pipeline_flush_primitives();
    float uv_scale_x = (float)GE_FONT_ATLAS_SIZE / (float)s->atlas_width; 
    float uv_scale_y = (float)GE_FONT_ATLAS_SIZE / (float)s->atlas_height; 
    for (int i = 0; i < nverts; i++) {
        float c[4];
        unsigned int col = colors[i];
        c[0] = (float)(col & 0xFF) / 255.0f;
        c[1] = (float)((col >> 8) & 0xFF) / 255.0f;
        c[2] = (float)((col >> 16) & 0xFF) / 255.0f;
        c[3] = (float)((col >> 24) & 0xFF) / 255.0f;
        float u = tcoords[i*2] * uv_scale_x;
        float v = tcoords[i*2+1] * uv_scale_y;
        ge_batch_add_vertex(verts[i*2], verts[i*2+1], u, v, c, 0,0, 0,0, 0,0);
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
static int atlas_w = GE_FONT_ATLAS_SIZE, atlas_h = GE_FONT_ATLAS_SIZE;
static unsigned char *default_font_mem;
static size_t default_font_size;

void native_text_terminate(void) {
    if (!initialized) return;
    glfonsDelete(fs); fs = NULL; initialized = 0;
}

static void ensure_init(void) {
    if (initialized) return;
    fs = glfonsCreate(atlas_w, atlas_h, FONS_ZERO_TOPLEFT);
    default_font_mem  = (unsigned char*)Noto_Sans_NotoSans_Regular_ttf;
    default_font_size = Noto_Sans_NotoSans_Regular_ttf_len;
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
    uint8_t r = (uint8_t)(state->current_color[0] * 255.0f);
    uint8_t g = (uint8_t)(state->current_color[1] * 255.0f);
    uint8_t b = (uint8_t)(state->current_color[2] * 255.0f);
    uint8_t a = (uint8_t)(state->current_color[3] * 255.0f);
    fonsSetSize(fs, current_size);
    fonsSetFont(fs, fs_font);
    fonsSetColor(fs, glfonsRGBA(r,g,b,a));
    fonsSetAlign(fs, FONS_ALIGN_LEFT | FONS_ALIGN_TOP);
    fonsDrawText(fs, (float)x, (float)y, text, NULL);
}

void native_text_mensure(const char *text, int16_t *w, int16_t *h) {
    if (!text) return;
    ensure_font_loaded();
    if (fs_font == FONS_INVALID) return;
    fonsSetSize(fs, current_size);
    fonsSetFont(fs, fs_font);
    float bounds[4];
    fonsTextBounds(fs, 0, 0, text, NULL, bounds);
    if (w) *w = (int16_t)(bounds[2] - bounds[0]);
    if (h) *h = (int16_t)(bounds[3] - bounds[1]);
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
