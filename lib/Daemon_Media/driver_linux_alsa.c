#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <dlfcn.h>

#include "gecnd.h"

typedef void* snd_pcm_handle_t;

typedef int  (*PFN_snd_pcm_open)      (snd_pcm_handle_t *, const char *, int, int);
typedef int  (*PFN_snd_pcm_set_params)(snd_pcm_handle_t, int, int, unsigned, unsigned, int, unsigned);
typedef long (*PFN_snd_pcm_writei)    (snd_pcm_handle_t, const void *, unsigned long);
typedef int  (*PFN_snd_pcm_recover)   (snd_pcm_handle_t, int, int);
typedef int  (*PFN_snd_pcm_close)     (snd_pcm_handle_t);

static void              *g_lib = NULL;
static snd_pcm_handle_t   g_pcm = NULL;
static unsigned           g_pcm_rate = 0;
static unsigned           g_pcm_ch   = 0;

static const int S16_LE = 2;
static const int PLAYBACK = 0;
static const int RW_INTERLEAVED = 3;

static PFN_snd_pcm_open       fn_snd_pcm_open       = NULL;
static PFN_snd_pcm_set_params fn_snd_pcm_set_params = NULL;
static PFN_snd_pcm_writei     fn_snd_pcm_writei     = NULL;
static PFN_snd_pcm_recover    fn_snd_pcm_recover    = NULL;
static PFN_snd_pcm_close      fn_snd_pcm_close      = NULL;

static void on_audio(const int16_t *data, size_t frames,
                     unsigned rate, unsigned channels, void *usr)
{
    (void)usr;
    if (!data || !frames) return;

    if (!g_pcm || rate != g_pcm_rate || channels != g_pcm_ch) {
        if (g_pcm) { fn_snd_pcm_close(g_pcm); g_pcm = NULL; }
        if (fn_snd_pcm_open(&g_pcm, "default", PLAYBACK, 0) < 0) {
            g_pcm = NULL; return;
        }
        if (fn_snd_pcm_set_params(g_pcm, S16_LE, RW_INTERLEAVED, channels, rate, 1, 40000) < 0) {
            fn_snd_pcm_close(g_pcm); g_pcm = NULL; return;
        }
        g_pcm_rate = rate;
        g_pcm_ch   = channels;
    }

    long n = fn_snd_pcm_writei(g_pcm, data, (unsigned long)frames);
    if (n < 0) fn_snd_pcm_recover(g_pcm, (int)n, 0);
}

void coreopen_alsa_gecnd()
{
    do {
        g_lib = dlopen("libasound.so.2", RTLD_LAZY);
        if (!g_lib) break;

        fn_snd_pcm_open       = dlsym(g_lib, "snd_pcm_open");
        fn_snd_pcm_set_params = dlsym(g_lib, "snd_pcm_set_params");
        fn_snd_pcm_writei     = dlsym(g_lib, "snd_pcm_writei");
        fn_snd_pcm_recover    = dlsym(g_lib, "snd_pcm_recover");
        fn_snd_pcm_close      = dlsym(g_lib, "snd_pcm_close");

        if (!fn_snd_pcm_open || !fn_snd_pcm_set_params ||
            !fn_snd_pcm_writei || !fn_snd_pcm_recover || !fn_snd_pcm_close) {
            dlclose(g_lib);
            g_lib = NULL;
            break;
        }

        gamely_daemon_media_audio_subscribe(on_audio, NULL);
    }
    while(0);
}
