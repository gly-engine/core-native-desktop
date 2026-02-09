#ifndef GEC_BACKEND_GL_INTERNAL_H
#define GEC_BACKEND_GL_INTERNAL_H

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <stdint.h>
#include <stdbool.h>

/* Global state for the OpenGL backend */

// Window
extern GLFWwindow *g_window;
extern uint16_t g_window_width;
extern uint16_t g_window_height;

// Shape drawing
extern GLuint g_shape_program;
extern GLint g_shape_loc_pos;
extern GLint g_shape_loc_proj;
extern GLint g_shape_loc_color;
extern GLuint g_vbo;

// Drawing state
extern uint32_t g_current_color;
extern uint32_t g_clear_color;

// Timing
extern double g_last_frame_time;

extern GLint g_shape_loc_rect;
extern GLint g_shape_loc_radius;
extern GLint g_shape_loc_mode;

// Line drawing
extern GLuint g_line_program;
extern GLint g_line_loc_pos;
extern GLint g_line_loc_proj;
extern GLint g_line_loc_color;

#endif // GEC_BACKEND_GL_INTERNAL_H
