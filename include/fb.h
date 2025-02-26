#ifndef FB_H
#define FB_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

typedef struct {
    int fb_fd;
    uint8_t *fb_ptr;
    size_t fb_size;
    int width;
    int height;
    int bpp;
} framebuffer_t;

int fb_init(framebuffer_t *fb, const char *fb_device);
void fb_close(framebuffer_t *fb);
void fb_clear(framebuffer_t *fb, uint32_t color);
void fb_draw_pixel(framebuffer_t *fb, int x, int y, uint32_t color);
void fb_draw_rect(framebuffer_t *fb, int x, int y, int w, int h, uint32_t color);
void fb_draw_rect_outline(framebuffer_t *fb, int x, int y, int w, int h, uint32_t color);
void fb_draw_text(framebuffer_t *fb, int x, int y, const char *text, uint32_t color);

#endif

