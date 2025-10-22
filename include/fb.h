#ifndef FB_H
#define FB_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    int fb_fd;
    unsigned char *fb_ptr; /* mmap'ed */
    size_t fb_size;
    int width;
    int height;
    int bpp;    /* bits per pixel: typically 16 or 32 */
    int pitch;  /* bytes per scanline */
} framebuffer_t;

/* Open /dev/fb0 (or fbdev if non-NULL), mmap, and fill fields. */
int  fb_init(framebuffer_t *fb, const char *fbdev);
/* Unmap/close and zero out struct. */
void fb_close(framebuffer_t *fb);

/* Utility draws */
void fb_clear(framebuffer_t *fb, uint32_t argb);
void fb_draw_pixel(framebuffer_t *fb, int x, int y, uint32_t argb);
void fb_fill_rect(framebuffer_t *fb, int x, int y, int w, int h, uint32_t argb);

/* 8x8 glyph blit at native scale (1x). */
void fb_draw_text8x8(framebuffer_t *fb, int x, int y, const char *s, uint32_t argb);

/* Scaled text using FONT_SCALE (default 2). Matches your UI’s expectations. */
void fb_draw_text(framebuffer_t *fb, int x, int y, const char *s, uint32_t argb);

/* 1px rectangle outline. */
void fb_draw_rect_outline(framebuffer_t *fb, int x, int y, int w, int h, uint32_t argb);

#endif /* FB_H */

