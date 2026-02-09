#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glad/gl.h>
#include <GLFW/glfw3.h>

#define LOG(fmt, ...) \
    printf("[dbg] " fmt "\n", ##__VA_ARGS__)

/* =====================
   Helpers
   ===================== */
static void die(const char *msg)
{
    fprintf(stderr, "[FATAL] %s\n", msg);
    exit(1);
}

static GLuint compile_shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "shader compile error:\n%s\n", log);
        exit(1);
    }
    return s;
}

static GLuint create_program(const char *vs, const char *fs)
{
    GLuint p = glCreateProgram();

    GLuint sv = compile_shader(GL_VERTEX_SHADER, vs);
    GLuint sf = compile_shader(GL_FRAGMENT_SHADER, fs);

    glAttachShader(p, sv);
    glAttachShader(p, sf);
    glLinkProgram(p);

    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "program link error:\n%s\n", log);
        exit(1);
    }

    glDeleteShader(sv);
    glDeleteShader(sf);
    return p;
}

/* =====================
   Shader sources
   ===================== */

/* OpenGL 2.1 (desktop) */
static const char *vs_gl =
    "#version 120\n"
    "attribute vec2 a_pos;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

static const char *fs_gl =
    "#version 120\n"
    "void main() {\n"
    "    gl_FragColor = vec4(1.0, 0.2, 0.2, 1.0);\n"
    "}\n";

/* OpenGL ES 2.0 */
static const char *vs_gles =
    "attribute vec2 a_pos;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

static const char *fs_gles =
    "precision mediump float;\n"
    "void main() {\n"
    "    gl_FragColor = vec4(1.0, 0.2, 0.2, 1.0);\n"
    "}\n";

/* =====================
   Geometry
   ===================== */
static float vertices[] = {
    -0.5f, -0.5f,
     0.5f, -0.5f,
     0.5f,  0.5f,

    -0.5f, -0.5f,
     0.5f,  0.5f,
    -0.5f,  0.5f,
};

/* =====================
   Main
   ===================== */
int main(void)
{
    LOG("program start");

    if (!glfwInit())
        die("GLFW init failed");

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow *window =
        glfwCreateWindow(1280, 720, "GL 2.0 / GLES 2.0 Square", NULL, NULL);

    if (!window)
        die("window creation failed");

    glfwMakeContextCurrent(window);

    if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress))
        die("GLAD load failed");

    const char *version = (const char *)glGetString(GL_VERSION);
    int is_gles = version && strstr(version, "OpenGL ES");

    LOG("GL_VERSION: %s", version);

    const char *vs = is_gles ? vs_gles : vs_gl;
    const char *fs = is_gles ? fs_gles : fs_gl;

    GLuint program = create_program(vs, fs);

    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    GLint loc_pos = glGetAttribLocation(program, "a_pos");

    while (!glfwWindowShouldClose(window)) {
        glClearColor(0.1f, 0.2f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(program);

        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(loc_pos);
        glVertexAttribPointer(
            loc_pos,
            2,
            GL_FLOAT,
            GL_FALSE,
            2 * sizeof(float),
            (void*)0
        );

        glDrawArrays(GL_TRIANGLES, 0, 6);

        glDisableVertexAttribArray(loc_pos);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}
