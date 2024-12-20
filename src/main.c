#include "zeebo.h"

extern void sdl_install();

/**
 * @mainpage
 *
 * @section build How to build
 *
 * You should only need @b cmake and @b make, the project has the recipe
 * for downloading all dependencies including cross compilers.
 *
 * @par only engine
 * @code
 * git clone https://github.com/gamelly/core-native-sdl
 * cmake -Bbuild -H.
 * make -C build
 * @endcode
 *
 * @par engine + game
 * @code
 * git clone
 * cmake -Bbuild -H. -DGAME="game.lua"
 * make -C build
 * @endcode
 *
 * @par custom engine + game
 * @code
 * git clone
 * cmake -Bbuild -H. -DGAME="game.lua" -DENGINE="engine.lua"
 * make -C build
 * @endcode
 *
 * @section run How to run
 *
 * @par simple way
 *
 * put a @c game.exe in some path to your @c game.lua and double click the
 * executable!
 *
 * @par passing file location flag
 * @code
 * game.exe -g /path/to/your/game.lua
 * @endcode
 *
 * @par another flags
 * @code
 * game.exe -f -w 1980 -h 1280 -e engine.lua -g game.lua
 * @endcode
 */
int main(int argc, char *argv[]) {
    lua_install();
    sdl_install();

    native_draw_install();
    native_text_install();
    native_json_install();
    native_http_install();

    ffmpeg_install();
    engine_install();

    kernel_init(argc, argv);

    while (kernel_runtime_online() && !kernel_has_error()) { kernel_update(); }

    if (kernel_has_error()) {
        fprintf(stderr, "FATAL ERROR!\n\n%s\n\nexit status: %d\n", kernel_get_error(), kernel_runtime_get_status());
    }

    kernel_exit();

    return kernel_runtime_get_status();
}
