#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ketopt.h>

#include "gecnd.h"
#include "gehook.h"
#include "gemetrics.h"

static ko_longopt_t longopts[] = {
    { "window", ko_required_argument, 300},
    { "screen", ko_required_argument, 301},
    { "fps", ko_required_argument, 302 },
    { "metrics", ko_required_argument, 303 },
    { "frameskip", ko_required_argument, 304 },
    { "game", ko_required_argument, 305 },
    { "engine", ko_required_argument, 306 },
    { "play", ko_required_argument, 307 },
    { "filter-aa", ko_required_argument, 401 },
    { "filter-color", ko_required_argument, 402 },
    { "filter-grain", ko_required_argument, 403 },
    { "filter-crt", ko_required_argument, 404 },
    { "filter-rotate", ko_required_argument, 405 },
    { "filter-scratch", ko_required_argument, 406 },
    { "filter-jitter", ko_required_argument, 407 },
    { "offset", ko_required_argument, 408 },
    { NULL, 0, 0 }
};

static void check_range(gecnd_t *gly, float val, float min, float max, const char *name) {
    if (val < min || val > max) {
        char buf[128];
        snprintf(buf, sizeof(buf), "argument %s out of range [%.1f, %.1f]: %.2f", name, min, max, val);
        gly->error_string = strdup(buf);
    }
}

void gecnd_set_args(gecnd_t *gly, int argc, char* argv[]) {
    if (!gly) return;

    ketopt_t opt = KETOPT_INIT;
    int c;

    while ((c = ketopt(&opt, argc, argv, 1, "", longopts)) >= 0) {
        if (c == 300 && sscanf(opt.arg, "%hdx%hd", &gly->window_width, &gly->window_height) != 2) {
            gly->error_string = "invalid window size!";
        }
        if (c == 301 && sscanf(opt.arg, "%hdx%hd", &gly->width, &gly->height) != 2) {
            gly->error_string = "invalid screen size!";
        }
        if (c == 302 && (sscanf(opt.arg, "%hhi", &gly->target_fps) != 1 || gly->target_fps > 100)) {
            gly->error_string = "invalid fps!";
        }
        if (c == 303) {
            gecnd_metrics_setup((uint32_t)atoi(opt.arg));
        }
        if (c == 304 && (sscanf(opt.arg, "%hhi", &gly->frameskip) != 1 || gly->frameskip > 10)) {
            gly->error_string = "invalid frameskip!";
        }
        if (c == 305) {
            gly->lua_game_code = opt.arg;
        }
        if (c == 306) {
            gly->lua_engine_code = opt.arg;
        }
        if (c == 307) {
            native_media_source(0, opt.arg);
            native_media_play(0);
        }
        if (c == 401) {
            float a, b, c;
            if (sscanf(opt.arg, "%f,%f,%f", &a, &b, &c) == 3) {
                gecnd_filter_set_aa(a, b, c);
            }
        }
        if (c == 402) {
            float b = 1.0f, cv = 1.0f, s = 1.0f;
            if (sscanf(opt.arg, "%f,%f,%f", &b, &cv, &s) >= 1) {
                gecnd_filter_set_brightness(b);
                gecnd_filter_set_contrast(cv);
                gecnd_filter_set_saturation(s);
            }
        }
        if (c == 403) {
            float g = (float)atof(opt.arg);
            check_range(gly, g, 0.0f, 1.0f, "film-grain");
            gecnd_filter_set_film_grain(g);
        }
        if (c == 404) {
            gecnd_filter_set_crt((float)atof(opt.arg));
        }
        if (c == 405) {
            gecnd_filter_set_rotation((float)atof(opt.arg));
        }
        if (c == 406) {
            gecnd_filter_set_scratch((float)atof(opt.arg));
        }
        if (c == 407) {
            gecnd_filter_set_jitter((float)atof(opt.arg));
        }
        if (c == 408) {
            float x1, y1, x2, y2, x3, y3, x4, y4;
            if (sscanf(opt.arg, "%f,%f,%f,%f,%f,%f,%f,%f", &x1, &y1, &x2, &y2, &x3, &y3, &x4, &y4) == 8) {
                gecnd_filter_set_corners(x1, y1, x2, y2, x3, y3, x4, y4);
            } else if (sscanf(opt.arg, "%f,%f,%f,%f", &x1, &y1, &x3, &y3) == 4) {
                gecnd_filter_set_corners(x1, y1, x3, y1, x3, y3, x1, y3);
            }
        }
    }
}
