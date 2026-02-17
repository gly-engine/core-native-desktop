#include <spng.h>

#include "gehook.h"
#include "geopengl.h"

void native_image_load(const char *path, int32_t image_id, bool *success) {
    GLBackendState *state = geogl_get_state();
    int r = 0;
    spng_ctx *ctx = NULL;
    unsigned char *image = NULL;
    size_t image_size;

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "Error: Could not open file %s\n", path);
        if (success) *success = false;
        return;
    }

    ctx = spng_ctx_new(0);
    if (ctx == NULL) {
        fprintf(stderr, "Error: spng_ctx_new() failed\n");
        fclose(fp);
        if (success) *success = false;
        return;
    }

    spng_set_png_file(ctx, fp);

    struct spng_ihdr ihdr;
    r = spng_get_ihdr(ctx, &ihdr);
    if (r) {
        fprintf(stderr, "Error: spng_get_ihdr() failed: %s\n", spng_strerror(r));
        spng_ctx_free(ctx);
        fclose(fp);
        if (success) *success = false;
        return;
    }

    // Determine output format (RGBA8)
    enum spng_color_type color_type = ihdr.color_type;
    unsigned int bit_depth = ihdr.bit_depth;
    unsigned int trns = 0; // SPNG_TRANSFORM_NONE

    if (color_type == SPNG_COLOR_TYPE_TRUECOLOR || color_type == SPNG_COLOR_TYPE_INDEXED) {
        trns |= (1 << 0); // SPNG_TRANSFORM_ADD_ALPHA
    }
    if (bit_depth != 8) {
        trns |= (1 << 3); // SPNG_TRANSFORM_SCALE_8
    }

    r = spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &image_size);
    if (r) {
        fprintf(stderr, "Error: spng_decoded_image_size() failed: %s\n", spng_strerror(r));
        spng_ctx_free(ctx);
        fclose(fp);
        if (success) *success = false;
        return;
    }

    image = (unsigned char*)malloc(image_size);
    if (image == NULL) {
        fprintf(stderr, "Error: Failed to allocate image buffer\n");
        spng_ctx_free(ctx);
        fclose(fp);
        if (success) *success = false;
        return;
    }

    r = spng_decode_image(ctx, image, image_size, SPNG_FMT_RGBA8, trns);
    if (r) {
        fprintf(stderr, "Error: spng_decode_image() failed: %s\n", spng_strerror(r));
        free(image);
        spng_ctx_free(ctx);
        fclose(fp);
        if (success) *success = false;
        return;
    }

    // Create OpenGL texture
    GLuint texture_id;
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ihdr.width, ihdr.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);

    // Store texture information
    size_t index = image_id - 1;

    while (kv_size(state->textures) <= index) {
        kv_push(GLTexture, state->textures, (GLTexture){0});
    }

    if (kv_A(state->textures, index).id != 0) {
        glDeleteTextures(1, &kv_A(state->textures, index).id);
    }

    kv_A(state->textures, index) = (GLTexture){ texture_id, (int)ihdr.width, (int)ihdr.height };

    if (success) *success = true;

    free(image);
    spng_ctx_free(ctx);
    fclose(fp);
}

void native_image_draw(int32_t image_id, int16_t x, int16_t y) {
    GLBackendState *state = geogl_get_state();
    size_t index = image_id - 1;

    if (image_id <= 0 || kv_size(state->textures) <= index) {
        return; // Invalid image_id
    }

    GLTexture texture = kv_A(state->textures, index);
    if (texture.id == 0) {
        return; // Texture not loaded
    }

    glUseProgram(state->video_program);

    glUniformMatrix4fv(state->video_loc_proj, 1, GL_FALSE, state->projection);
    glUniform1i(state->video_loc_format, 0); // 0 = RGBA format

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture.id);
    glUniform1i(state->video_loc_tex_rgba, 0);

    float vertices[] = {
        (float)x, (float)y,             0.0f, 0.0f, // Top-left
        (float)x + texture.width, (float)y, 1.0f, 0.0f, // Top-right
        (float)x + texture.width, (float)y + texture.height, 1.0f, 1.0f, // Bottom-right
        (float)x, (float)y + texture.height, 0.0f, 1.0f  // Bottom-left
    };

    glBindBuffer(GL_ARRAY_BUFFER, state->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);

    glEnableVertexAttribArray(state->video_loc_pos);
    glVertexAttribPointer(state->video_loc_pos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

    glEnableVertexAttribArray(state->video_loc_texCoord);
    glVertexAttribPointer(state->video_loc_texCoord, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    glDisableVertexAttribArray(state->video_loc_pos);
    glDisableVertexAttribArray(state->video_loc_texCoord);
}

void native_image_mensure(int32_t image_id, int16_t *w, int16_t *h) {
    GLBackendState *state = geogl_get_state();
    size_t index = image_id - 1;

    if (image_id > 0 && kv_size(state->textures) > index) {
        GLTexture texture = kv_A(state->textures, index);
        if (texture.id != 0 && w && h) {
            *w = (int16_t)texture.width;
            *h = (int16_t)texture.height;
        }
    }
}

void native_image_unload(int32_t image_id, bool *success) {
    GLBackendState *state = geogl_get_state();
    size_t index = image_id - 1;

    if (image_id > 0 && kv_size(state->textures) > index) {
        GLTexture *texture = &kv_A(state->textures, index);
        if (texture->id != 0) {
            glDeleteTextures(1, &texture->id);
            texture->id = 0; // Mark as unloaded
            if (success) *success = true;
            return;
        }
    }
    if (success) *success = false;
}
