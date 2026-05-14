#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ketopt.h>

#include "gecnd.h"
#include "gehook.h"
#include "gemetrics.h"

void gamely_set_toml(gecnd_t *gly, const char *path, ko_longopt_t *longopts);
void gecnd_set_opt(gecnd_t *gly, int c, ketopt_t opt);

static ko_longopt_t longopts[] = {
    { "window",         ko_required_argument, 1300 },
    { "screen",         ko_required_argument, 1301 },
    { "fps",            ko_required_argument, 1302 },
    { "metrics",        ko_required_argument, 1303 },
    { "frameskip",      ko_required_argument, 1304 },
    { "game",           ko_required_argument, 1305 },
    { "engine",         ko_required_argument, 1306 },
    { "play",           ko_required_argument, 1307 },
    { "game+engine",    ko_required_argument, 1308 },
    { "disable-radius", ko_no_argument,       1309 },
    { "port",           ko_required_argument, 1310 },
    { "filter-aa",      ko_required_argument, 1401 },
    { "filter-color",   ko_required_argument, 1402 },
    { "filter-grain",   ko_required_argument, 1403 },
    { "filter-crt",     ko_required_argument, 1404 },
    { "filter-rotate",  ko_required_argument, 1405 },
    { "filter-scratch", ko_required_argument, 1406 },
    { "filter-jitter",  ko_required_argument, 1407 },
    { "offset",         ko_required_argument, 1408 },
    { "remote",         ko_required_argument, 1503 },
    { "plugin",         ko_required_argument, 2501 },
    { "input",          ko_required_argument, 2502 },
    { "conf",           ko_required_argument, 9999 },
    { NULL, 0, 0 }
};

static void check_range(gecnd_t *gly, float val, float min, float max, const char *name) {
    if (val < min || val > max)
        gecnd_add_error(gly, "argument %s out of range [%.1f, %.1f]: %.2f", name, min, max, val);
}

void gecnd_set_args(gecnd_t *gly, int argc, char* argv[]) {
    if (!gly) return;

    ketopt_t opt = KETOPT_INIT;
    int c;

    while ((c = ketopt(&opt, argc, argv, 1, "", longopts)) >= 0) {
        gecnd_set_opt(gly, c, opt);
    }
}

void gecnd_set_opt(gecnd_t *gly, int c, ketopt_t opt)
{
    if (c == 1300 && sscanf(opt.arg, "%hdx%hd", &gecnd_get_display()->window_width, &gecnd_get_display()->window_height) != 2)
        gecnd_add_error(gly, "invalid window size!");
    if (c == 1301 && sscanf(opt.arg, "%hdx%hd", &gly->width, &gly->height) != 2)
        gecnd_add_error(gly, "invalid screen size!");
    if (c == 1302 && (sscanf(opt.arg, "%hhi", &gly->target_fps) != 1 || gly->target_fps > 100))
        gecnd_add_error(gly, "invalid fps!");
    if (c == 1303) {
        gecnd_metrics_setup((uint32_t)atoi(opt.arg));
    }
    if (c == 1304 && (sscanf(opt.arg, "%hhi", &gly->frameskip) != 1 || gly->frameskip > 10))
        gecnd_add_error(gly, "invalid frameskip!");
    if (c == 1305) {
        if (strncmp(opt.arg, "http://", 7) == 0 || strncmp(opt.arg, "https://", 8) == 0) {
            gly->game_source.kind = GECND_LUA_SOURCE_HTTP;
            char *d = gecnd_get_display()->game_base_url;
            strncpy(d, opt.arg, 511); d[511] = '\0';
            char *s = strrchr(d, '/'), *p = strstr(d, "://");
            if (s && p && s > p + 3) s[1] = '\0'; else d[0] = '\0';
        } else {
            gly->game_source.kind = GECND_LUA_SOURCE_FILE;
        }
        gly->game_source.uri = opt.arg;
    }
    if (c == 1308) {
        bool is_http = strncmp(opt.arg, "http://", 7) == 0 || strncmp(opt.arg, "https://", 8) == 0;
        size_t n = strlen(opt.arg);
        int    sl = opt.arg[n - 1] == '/';
        char  *eng = malloc(n + 12), *gam = malloc(n + 9);
        if (!eng || !gam) { free(eng); free(gam); return; }
        sprintf(eng, "%s%sengine.lua", opt.arg, sl ? "" : "/");
        sprintf(gam, "%s%sgame.lua",   opt.arg, sl ? "" : "/");
        gly->engine_source.kind = is_http ? GECND_LUA_SOURCE_HTTP : GECND_LUA_SOURCE_FILE;
        gly->engine_source.uri  = eng;
        gly->game_source.kind   = is_http ? GECND_LUA_SOURCE_HTTP : GECND_LUA_SOURCE_FILE;
        gly->game_source.uri    = gam;
        if (is_http)
            snprintf(gecnd_get_display()->game_base_url, 512, "%s%s", opt.arg, sl ? "" : "/");
    }
    if (c == 1306) {
        if (strncmp(opt.arg, "http://", 7) == 0 || strncmp(opt.arg, "https://", 8) == 0)
            gly->engine_source.kind = GECND_LUA_SOURCE_HTTP;
        else
            gly->engine_source.kind = GECND_LUA_SOURCE_FILE;
        gly->engine_source.uri = opt.arg;
    }
    if (c == 1307) {
        gamely_daemon_media_playback_source(0, opt.arg);
        gamely_daemon_media_playback_play(0);
    }
    if (c == 1309) {
        gecnd_get_display()->disable_radius = true;
    }
    if (c == 1310 && (sscanf(opt.arg, "%hu", &gecnd_get_display()->port) != 1))
        gecnd_add_error(gly, "invalid port!");
    if (c == 1402) {
        float b = 1.0f, cv = 1.0f, s = 1.0f;
        if (sscanf(opt.arg, "%f,%f,%f", &b, &cv, &s) >= 1) {
            gecnd_filter_set_brightness(b);
            gecnd_filter_set_contrast(cv);
            gecnd_filter_set_saturation(s);
        }
    }
    if (c == 1403) {
        float g = (float)atof(opt.arg);
        check_range(gly, g, 0.0f, 1.0f, "film-grain");
        gecnd_filter_set_film_grain(g);
    }
    if (c == 1404) {
        gecnd_filter_set_crt((float)atof(opt.arg));
    }
    if (c == 1405) {
        gecnd_filter_set_rotation((float)atof(opt.arg));
    }
    if (c == 1406) {
        gecnd_filter_set_scratch((float)atof(opt.arg));
    }
    if (c == 1407) {
        gecnd_filter_set_jitter((float)atof(opt.arg));
    }
    if (c == 1408) {
        float x1, y1, x2, y2, x3, y3, x4, y4;
        if (sscanf(opt.arg, "%f,%f,%f,%f,%f,%f,%f,%f", &x1, &y1, &x2, &y2, &x3, &y3, &x4, &y4) == 8) {
            gecnd_filter_set_corners(x1, y1, x2, y2, x3, y3, x4, y4);
        } else if (sscanf(opt.arg, "%f,%f,%f,%f", &x1, &y1, &x3, &y3) == 4) {
            gecnd_filter_set_corners(x1, y1, x3, y1, x3, y3, x1, y3);
        }
    }
    if (c == 1503) {
        gamely_daemon_input_remote(opt.arg);
    }
    if (c == 2501) {
        gecnd_plugin_load(gly, opt.arg);
    }
    if (c == 2502) {
        gamely_daemon_input_add_source(opt.arg);
    }
    if (c == 9999) {
        gamely_set_toml(gly, opt.arg, longopts);
    }
}
