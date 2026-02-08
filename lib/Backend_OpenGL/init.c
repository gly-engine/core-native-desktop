#include <stdio.h>
#include <stdlib.h>
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

/* =====================
   Main
   ===================== */
int main(void)
{
    LOG("program start");

    if (!glfwInit())
        die("GLFW init failed");

    LOG("GLFW initialized");

    // GLFW hints for OpenGL 2.1 (equiv. GLX_RGBA + doublebuffer)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_ANY_PROFILE);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(100, 100, "GLFW test", NULL, NULL);
    if (!window)
        die("GLFW window creation failed");

    LOG("GLFW window created");

    glfwMakeContextCurrent(window);

    LOG("GLFW context made current");

    // Load GL functions via GLAD
    if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress))
        die("Failed to load OpenGL functions with GLAD");

    LOG("GLAD loaded OpenGL functions");

    const GLubyte* vendor = glGetString(GL_VENDOR);
    if (vendor)
        printf("GL_VENDOR: %s\n", vendor);
    else
        printf("GL_VENDOR: NULL\n");

    // Simple loop just to show window
    int frame = 0;
    while (!glfwWindowShouldClose(window) && frame++ < 10000) {
        glClearColor(0.1f, 0.2f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    LOG("done");
    return 0;
}
