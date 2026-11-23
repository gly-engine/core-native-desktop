#include <stdio.h>
#include <string.h>

#include "gecnd.h"
#include "geopengl.h"

#if defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>

static uint32_t *fb;
static size_t fb_len;
static uint32_t fb_stride;
static uint16_t fb_width = 1, fb_height = 1;
static uint8_t shift_r, shift_g, shift_b, shift_a;

static void on_overlay_path(const char *key, void *value, void *usr) {
    (void)key; (void)usr;
    const char *path = (const char *)value;
    if (fb || !path || !path[0]) return;
    int fd = open(path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "[overlay] cannot open %s\n", path);
        return;
    }
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    bool ok = ioctl(fd, FBIOGET_VSCREENINFO, &var) == 0 &&
              ioctl(fd, FBIOGET_FSCREENINFO, &fix) == 0 &&
              var.bits_per_pixel == 32 && fix.smem_len;
    void *mem = ok ? mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0) : MAP_FAILED;
    close(fd);
    if (mem == MAP_FAILED) {
        fprintf(stderr, "[overlay] %s is not a 32bpp framebuffer\n", path);
        return;
    }
    fb = mem;
    fb_len = fix.smem_len;
    fb_stride = fix.line_length / 4;
    fb_width = (uint16_t)var.xres;
    fb_height = (uint16_t)var.yres;
    shift_r = (uint8_t)var.red.offset;
    shift_g = (uint8_t)var.green.offset;
    shift_b = (uint8_t)var.blue.offset;
    shift_a = (uint8_t)var.transp.offset;
}

void ge_overlay_begin(bool entering) {
    if (fb && entering) memset(fb, 0, fb_len);
    ge_pipeline_overlay_start(fb_width, fb_height);
}

void ge_overlay_end(void) {
    uint16_t r[4];
    const uint8_t *rgba = ge_pipeline_overlay_flush(r);
    if (!fb || !rgba) return;
    for (uint32_t y = r[1]; y < (uint32_t)r[1] + r[3]; y++) {
        uint32_t *row = fb + y * fb_stride;
        for (uint32_t x = r[0]; x < (uint32_t)r[0] + r[2]; x++, rgba += 4) {
            row[x] = (uint32_t)rgba[0] << shift_r | (uint32_t)rgba[1] << shift_g |
                     (uint32_t)rgba[2] << shift_b | (uint32_t)rgba[3] << shift_a;
        }
    }
}

__attribute__((constructor))
static void init(void) {
    gecnd_registry("hook", "path:overlay", (void *)on_overlay_path, NULL);
}
#else
void ge_overlay_begin(bool entering) {
    (void)entering;
    ge_pipeline_overlay_start(1, 1);
}

void ge_overlay_end(void) {
    uint16_t r[4];
    ge_pipeline_overlay_flush(r);
}
#endif
