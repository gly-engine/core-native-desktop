#include "raylib.h"

int main(void) {
    InitWindow(800, 450, "Hello World - Raylib 5.0");

    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        BeginDrawing();
            ClearBackground(RAYWHITE);
            DrawText("Hello, World!", 350, 200, 20, DARKGRAY);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
