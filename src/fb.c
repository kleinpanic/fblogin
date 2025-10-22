#include "fb.h"

#include <sys/ioctl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#ifndef FONT_SCALE
#define FONT_SCALE 2
#endif

/* 8x8 font: do not modify per your requirement */
#include "font8x8_basic.h"

static uint32_t to_native(uint32_t argb, int bpp) {
    uint8_t r = (argb >> 16) & 0xFF;
    uint8_t g = (argb >>  8) & 0xFF;
    uint8_t b = (argb      ) & 0xFF;

    if (bpp == 32) {
        return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    } else if (bpp == 16) {
        return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }
    return 0;
}

int fb_init(framebuffer_t *fb, const char *fbdev) {
    if (!fb) return -1;
    memset(fb, 0, sizeof(*fb));

    const char *dev = fbdev ? fbdev : "/dev/fb0";
    fb->fb_fd = open(dev, O_RDWR);
    if (fb->fb_fd < 0) return -1;

    struct fb_fix_screeninfo finfo;
    struct fb_var_screeninfo vinfo;
    if (ioctl(fb->fb_fd, FBIOGET_FSCREENINFO, &finfo) < 0) goto fail;
    if (ioctl(fb->fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0) goto fail;

    fb->width  = (int)vinfo.xres;
    fb->height = (int)vinfo.yres;
    fb->bpp    = (int)vinfo.bits_per_pixel;
    fb->pitch  = (int)finfo.line_length;
    fb->fb_size = (size_t)(fb->pitch * fb->height);

    void *p = mmap(NULL, fb->fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fb_fd, 0);
    if (p == MAP_FAILED) goto fail;
    fb->fb_ptr = (unsigned char *)p;

    return 0;

fail:
    if (fb->fb_ptr && fb->fb_ptr != MAP_FAILED) munmap(fb->fb_ptr, fb->fb_size);
    if (fb->fb_fd >= 0) close(fb->fb_fd);
    memset(fb, 0, sizeof(*fb));
    return -1;
}

void fb_close(framebuffer_t *fb) {
    if (!fb) return;
    if (fb->fb_ptr && fb->fb_ptr != MAP_FAILED) munmap(fb->fb_ptr, fb->fb_size);
    if (fb->fb_fd >= 0) close(fb->fb_fd);
    memset(fb, 0, sizeof(*fb));
}

void fb_clear(framebuffer_t *fb, uint32_t argb) {
    if (!fb || !fb->fb_ptr) return;

    if (fb->bpp == 32) {
        uint32_t pix = to_native(argb, 32);
        uint32_t *p = (uint32_t *)fb->fb_ptr;
        size_t n = fb->fb_size / 4;
        for (size_t i = 0; i < n; ++i) p[i] = pix;
    } else if (fb->bpp == 16) {
        uint16_t pix = (uint16_t)to_native(argb, 16);
        uint16_t *p = (uint16_t *)fb->fb_ptr;
        size_t n = fb->fb_size / 2;
        for (size_t i = 0; i < n; ++i) p[i] = pix;
    }
}

void fb_draw_pixel(framebuffer_t *fb, int x, int y, uint32_t argb) {
    if (!fb || !fb->fb_ptr) return;
    if ((unsigned)x >= (unsigned)fb->width || (unsigned)y >= (unsigned)fb->height) return;

    uint8_t *base = fb->fb_ptr + y * fb->pitch + (x * fb->bpp / 8);
    if (fb->bpp == 32) {
        *(uint32_t*)base = to_native(argb, 32);
    } else if (fb->bpp == 16) {
        *(uint16_t*)base = (uint16_t)to_native(argb, 16);
    }
}

void fb_fill_rect(framebuffer_t *fb, int x, int y, int w, int h, uint32_t argb) {
    if (!fb || w <= 0 || h <= 0) return;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
            fb_draw_pixel(fb, x + i, y + j, argb);
}

void fb_draw_rect_outline(framebuffer_t *fb, int x, int y, int w, int h, uint32_t argb) {
    if (!fb || w <= 0 || h <= 0) return;
    for (int i = 0; i < w; ++i) {
        fb_draw_pixel(fb, x + i, y, argb);
        fb_draw_pixel(fb, x + i, y + h - 1, argb);
    }
    for (int j = 0; j < h; ++j) {
        fb_draw_pixel(fb, x, y + j, argb);
        fb_draw_pixel(fb, x + w - 1, y + j, argb);
    }
}

void fb_draw_text8x8(framebuffer_t *fb, int x, int y, const char *s, uint32_t argb) {
    if (!fb || !fb->fb_ptr || !s) return;

    int dx = x;
    for (; *s; ++s) {
        unsigned char uc = (unsigned char)*s;
        const unsigned char *glyph = (const unsigned char*)(const void*)font8x8_basic[uc];

        for (int row = 0; row < 8; ++row) {
            unsigned char bits = glyph[row];
            for (int col = 0; col < 8; ++col) {
                if (bits & (1u << col)) {
                    fb_draw_pixel(fb, dx + col, y + row, argb);
                }
            }
        }
        dx += 8;
    }
}

void fb_draw_text(framebuffer_t *fb, int x, int y, const char *s, uint32_t argb) {
    if (!fb || !s) return;

    const int scale = (FONT_SCALE <= 0) ? 1 : FONT_SCALE;
    int dx = x;

    for (; *s; ++s) {
        unsigned char uc = (unsigned char)*s;
        const unsigned char *glyph = (const unsigned char*)(const void*)font8x8_basic[uc];

        for (int row = 0; row < 8; ++row) {
            unsigned char bits = glyph[row];
            for (int col = 0; col < 8; ++col) {
                if (bits & (1u << col)) {
                    for (int sy = 0; sy < scale; ++sy)
                        for (int sx = 0; sx < scale; ++sx)
                            fb_draw_pixel(fb, dx + col*scale + sx, y + row*scale + sy, argb);
                }
            }
        }
        dx += 8 * scale;
    }
}

