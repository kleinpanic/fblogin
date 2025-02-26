#include "fb.h"
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "font8x8_basic.h"

#ifndef FONT_SCALE
#define FONT_SCALE 2
#endif

int fb_init(framebuffer_t *fb, const char *fb_device) {
    fb->fb_fd = open(fb_device, O_RDWR);
    if (fb->fb_fd < 0) {
        perror("open framebuffer");
        return -1;
    }
    struct fb_var_screeninfo vinfo;
    if (ioctl(fb->fb_fd, FBIOGET_VSCREENINFO, &vinfo)) {
        perror("ioctl FBIOGET_VSCREENINFO");
        close(fb->fb_fd);
        return -1;
    }
    fb->width = vinfo.xres;
    fb->height = vinfo.yres;
    fb->bpp = vinfo.bits_per_pixel;
    size_t screensize = vinfo.yres_virtual * vinfo.xres_virtual * (vinfo.bits_per_pixel / 8);
    fb->fb_ptr = (uint8_t *)mmap(NULL, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fb_fd, 0);
    if (fb->fb_ptr == MAP_FAILED) {
        perror("mmap framebuffer");
        close(fb->fb_fd);
        return -1;
    }
    fb->fb_size = screensize;
    return 0;
}

void fb_close(framebuffer_t *fb) {
    munmap(fb->fb_ptr, fb->fb_size);
    close(fb->fb_fd);
}

void fb_clear(framebuffer_t *fb, uint32_t color) {
    if (fb->bpp != 32) {
        fprintf(stderr, "Only 32 bpp supported in fb_clear\n");
        return;
    }
    uint32_t *ptr = (uint32_t *)fb->fb_ptr;
    size_t pixels = fb->width * fb->height;
    for (size_t i = 0; i < pixels; i++) {
        ptr[i] = color;
    }
}

void fb_draw_pixel(framebuffer_t *fb, int x, int y, uint32_t color) {
    if (x < 0 || x >= fb->width || y < 0 || y >= fb->height)
        return;
    if (fb->bpp != 32)
        return;
    uint32_t *ptr = (uint32_t *)fb->fb_ptr;
    ptr[y * fb->width + x] = color;
}

void fb_draw_rect(framebuffer_t *fb, int x, int y, int w, int h, uint32_t color) {
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            fb_draw_pixel(fb, i, j, color);
        }
    }
}

/* Draw only an outline (transparent box) */
void fb_draw_rect_outline(framebuffer_t *fb, int x, int y, int w, int h, uint32_t color) {
    for (int i = x; i < x + w; i++) {
        fb_draw_pixel(fb, i, y, color);
        fb_draw_pixel(fb, i, y + h - 1, color);
    }
    for (int j = y; j < y + h; j++) {
        fb_draw_pixel(fb, x, j, color);
        fb_draw_pixel(fb, x + w - 1, j, color);
    }
}

/* Draw text using an 8x8 bitmap scaled by FONT_SCALE */
void fb_draw_text(framebuffer_t *fb, int x, int y, const char *text, uint32_t color) {
    while (*text) {
        unsigned char uc = (unsigned char)*text;
        if (uc > 127)
            uc = '?';
        for (int row = 0; row < 8; row++) {
            uint8_t row_data = font8x8_basic[(int)uc][row];
            for (int col = 0; col < 8; col++) {
                if (row_data & (1 << col)) {
                    for (int dy = 0; dy < FONT_SCALE; dy++) {
                        for (int dx = 0; dx < FONT_SCALE; dx++) {
                            fb_draw_pixel(fb, x + col * FONT_SCALE + dx, y + row * FONT_SCALE + dy, color);
                        }
                    }
                }
            }
        }
        x += 8 * FONT_SCALE;
        text++;
    }
}

