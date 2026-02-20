#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

static int glfons__renderCreate(void* userPtr, int width, int height) {
    GLFONScontext* gl = (GLFONScontext*)userPtr;
    gl->width = width;
    gl->height = height;
    gl->scratch = NULL;
    gl->scratch_size = 0;
    glGenTextures(1, &gl->tex);
    glBindTexture(GL_TEXTURE_2D, gl->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, width, height, 0, GL_ALPHA, GL_UNSIGNED_BYTE, NULL);
    return 1;
}

static void glfons__renderUpdate(void* userPtr, int* rect, const unsigned char* data) {
    GLFONScontext* gl = (GLFONScontext*)userPtr;

    int x = rect[0];
    int y = rect[1];
    int w = rect[2] - rect[0];
    int h = rect[3] - rect[1];

    if (w <= 0 || h <= 0)
        return;

    glBindTexture(GL_TEXTURE_2D, gl->tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    size_t needed = (size_t)(w * h);

    if (gl->scratch_size < needed) {
        unsigned char* newbuf = (unsigned char*)realloc(gl->scratch, needed);
        if (!newbuf)
            return;

        gl->scratch = newbuf;
        gl->scratch_size = needed;
    }

    const unsigned char* src = data + y * gl->width + x;
    unsigned char* dst = gl->scratch;

    for (int row = 0; row < h; row++) {
        memcpy(dst, src, w);
        dst += w;
        src += gl->width;
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_ALPHA, GL_UNSIGNED_BYTE, gl->scratch);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

}

static void glfons__renderDraw(void* userPtr, const float* verts, const float* tcoords, const unsigned int* colors, int nverts) {
    if (nverts == 0) return;
    GLFONScontext* gl = (GLFONScontext*)userPtr;
    GLBackendState *state = geogl_get_state();

    glUseProgram(state->font_program);
    glUniformMatrix4fv(state->font_loc_proj, 1, GL_FALSE, state->projection);
    glUniform1i(state->font_loc_sampler, 0);
    glUniform4fv(state->font_loc_color, 1, state->current_color);

    gecnd_filter_t *filter = gecnd_filter_get_config();
    glUniform2f(state->font_loc_tsize, 1.0f / (float)gl->width, 1.0f / (float)gl->height);
    glUniform1f(state->font_loc_aa_blur, filter->aa_blur);
    glUniform1f(state->font_loc_aa_wC, filter->aa_weight_center);
    glUniform1f(state->font_loc_aa_wN, filter->aa_weight_neighbor);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gl->tex);
    glBindBuffer(GL_ARRAY_BUFFER, state->vbo);

    size_t vert_size = sizeof(float) * 2;
    size_t tcoord_size = sizeof(float) * 2;
    size_t stride = vert_size + tcoord_size;
    size_t total_size = stride * nverts;

    if (gl->scratch_size < total_size) {
        gl->scratch = realloc(gl->scratch, total_size);
        gl->scratch_size = total_size;
    }

    unsigned char* p = (unsigned char*)gl->scratch;
    for (int i = 0; i < nverts; i++) {
        memcpy(p, &verts[i*2], vert_size); p += vert_size;
        memcpy(p, &tcoords[i*2], tcoord_size); p += tcoord_size;
    }

    glBufferData(GL_ARRAY_BUFFER, total_size, gl->scratch, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(state->font_loc_pos);
    glEnableVertexAttribArray(state->font_loc_texCoord);
    glVertexAttribPointer(state->font_loc_pos, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glVertexAttribPointer(state->font_loc_texCoord, 2, GL_FLOAT, GL_FALSE, stride, (void*)vert_size);

    glDrawArrays(GL_TRIANGLES, 0, nverts);

    glDisableVertexAttribArray(state->font_loc_pos);
    glDisableVertexAttribArray(state->font_loc_texCoord);
    glUseProgram(0);
}

static void glfons__renderDelete(void* userPtr) {
    GLFONScontext* gl = (GLFONScontext*)userPtr;
    if (gl->tex != 0) glDeleteTextures(1, &gl->tex);
    if (gl->scratch) free(gl->scratch);
    free(gl);
}

FONScontext* glfonsCreate(int width, int height, int flags) {
    FONSparams params;
    GLFONScontext* gl = (GLFONScontext*)malloc(sizeof(GLFONScontext));
    if (gl == NULL) return NULL;
    memset(gl, 0, sizeof(GLFONScontext));

    memset(&params, 0, sizeof(params));
    params.width = width;
    params.height = height;
    params.flags = (unsigned char)flags;
    params.renderCreate = glfons__renderCreate;
    params.renderUpdate = glfons__renderUpdate;
    params.renderDraw = glfons__renderDraw;
    params.renderDelete = glfons__renderDelete;
    params.userPtr = gl;

    return fonsCreateInternal(&params);
}

void glfonsDelete(FONScontext* ctx) {
    fonsDeleteInternal(ctx);
}

unsigned int glfonsRGBA(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    return (r) | (g << 8) | (b << 16) | (a << 24);
}

static FONScontext *fs;
static int fs_font = FONS_INVALID;
static float current_size = 16.0f;
static int initialized = 0;
static int atlas_w = 1024;
static int atlas_h = 1024;
static unsigned char *default_font_mem;
static size_t default_font_size;
static unsigned char *loaded_font_mem;
static size_t loaded_font_size;

void native_text_terminate(void) {
    if (!initialized) return;
    glfonsDelete(fs);
    fs = NULL;
    initialized = 0;
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
    loaded_font_mem  = default_font_mem;
    loaded_font_size = default_font_size;
    fs_font = fonsAddFontMem(fs, "default", loaded_font_mem, loaded_font_size, 0);
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

void native_text_font_name(const char *path) {
    (void)path;
}

void native_text_font_default(uint8_t index) {
    (void)index;
    if (!initialized) ensure_init();
    if (loaded_font_mem && loaded_font_mem != default_font_mem) {
        free(loaded_font_mem);
    }
    fonsResetAtlas(fs, atlas_w, atlas_h);
    fs_font = FONS_INVALID;
    ensure_font_loaded();
}
