#include "ui.h"
#include "fb.h"
#include <regex.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

#ifndef FONT_SCALE
#define FONT_SCALE 2
#endif

// Global flag for cmatrix animation.
static int ui_use_cmatrix = 0;
void ui_set_cmatrix(int flag) {
    ui_use_cmatrix = flag;
}

/* Internal: Force display update via FBIOPAN_DISPLAY */
static void fb_update_display(framebuffer_t *fb) {
    struct fb_var_screeninfo vinfo;
    if (ioctl(fb->fb_fd, FBIOGET_VSCREENINFO, &vinfo) == 0) {
        vinfo.xoffset = 0;
        vinfo.yoffset = 0;
        ioctl(fb->fb_fd, FBIOPAN_DISPLAY, &vinfo);
    }
}

/* Internal: Draw a moving cmatrix background (if enabled) */
static void ui_draw_cmatrix_background(framebuffer_t *fb) {
    static int offset = 0;
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    int charCount = strlen(charset);
    int step = 8 * FONT_SCALE;
    static int seeded = 0;
    if (!seeded) {
        srand(time(NULL));
        seeded = 1;
    }
    for (int y = 0; y < fb->height; y += step) {
        for (int x = -offset; x < fb->width; x += step) {
            char c = charset[rand() % charCount];
            char buf[2] = { c, '\0' };
            fb_draw_text(fb, x, y, buf, 0x001100);
        }
    }
    offset = (offset + 1) % step;
}

/* Internal: Parse ANSI escape codes and render formatted text */
static void fb_draw_ansi_text(framebuffer_t *fb, int x, int y, const char *text) {
    int default_fg = 0xFFFFFF; // Default white text
    int default_bg = 0x000000; // Default black background
    int fg = default_fg;
    int bg = default_bg;

    char buffer[41];  // Max 40 visible chars + null terminator
    int buf_index = 0;

    for (int i = 0; text[i] != '\0'; i++) {
        if (text[i] == '\033' && text[i + 1] == '[') { // ANSI escape sequence
            i += 2;
            int code;
            while (sscanf(&text[i], "%d", &code) == 1) {
                if (code == 0) {
                    fg = default_fg;
                    bg = default_bg;
                } else if (code >= 30 && code <= 37) {
                    static int colors[] = { 0x000000, 0xAA0000, 0x00AA00, 0xAA5500, 
                                            0x0000AA, 0xAA00AA, 0x00AAAA, 0xAAAAAA };
                    fg = colors[code - 30];
                } else if (code >= 40 && code <= 47) {
                    static int bg_colors[] = { 0x000000, 0xAA0000, 0x00AA00, 0xAA5500, 
                                               0x0000AA, 0xAA00AA, 0x00AAAA, 0xAAAAAA };
                    bg = bg_colors[code - 40];
                }
                while (text[i] && text[i] != 'm') i++; // Move past 'm'
                i++;
            }
            continue;
        }

        if (buf_index < 40) { 
            buffer[buf_index++] = text[i];
        }
    }

    buffer[buf_index] = '\0'; 
    fb_draw_text(fb, x, y, buffer, fg);
}

/* Internal: Draw fblogin logo from a text file with ANSI color support */
static void ui_draw_pfp(framebuffer_t *fb, int x, int y) {
    const char *filename = "/etc/fblogin/fblogin-logo.txt";
    char buffer[256];  
    int line_number = 0;

    FILE *file = fopen(filename, "r");
    if (file) {
        while (line_number < 20 && fgets(buffer, sizeof(buffer), file)) {
            buffer[strcspn(buffer, "\n")] = 0;  // Strip newline
            fb_draw_ansi_text(fb, x, y + line_number * 8 * FONT_SCALE, buffer);
            line_number++;
        }
        fclose(file);
    }

    while (line_number < 20) {
        fb_draw_text(fb, x, y + line_number * 8 * FONT_SCALE, " ", 0xFFFFFF);
        line_number++;
    }
}

/* Internal: Draw "bubble" text with an outline */
static void ui_draw_bubble_text(framebuffer_t *fb, int x, int y, const char *text, uint32_t color, uint32_t outline_color) {
    int offsets[8][2] = {
        {-1, -1}, { 0, -1}, { 1, -1},
        {-1,  0},           { 1,  0},
        {-1,  1}, { 0,  1}, { 1,  1}
    };
    for (int i = 0; i < 8; i++) {
        fb_draw_text(fb, x + offsets[i][0], y + offsets[i][1], text, outline_color);
    }
    fb_draw_text(fb, x, y, text, color);
}

/* Internal: Draw the base UI (background, title, and Debian spiral) */
static void ui_draw_base(framebuffer_t *fb, int base_offset_y) {
    if (ui_use_cmatrix) {
        ui_draw_cmatrix_background(fb);
    } else {
        fb_clear(fb, 0x000000);
    }
    char hostname[128] = {0};
    gethostname(hostname, sizeof(hostname));
    char title[256];
    snprintf(title, sizeof(title), "Login for %s", hostname);
    int title_width = strlen(title) * 8 * FONT_SCALE;
    int title_x = (fb->width - title_width) / 2;
    int title_y = base_offset_y; 
    ui_draw_bubble_text(fb, title_x, title_y, title, 0xFFFFFF, 0x000000);
    
    // Draw Debian spiral below title (position adjusted to stay on frame for fingerprint and welcome)
    int spiral_width = 29 * 8 * FONT_SCALE;
    int spiral_x = (fb->width - spiral_width) / 2;
    int spiral_y = title_y + 60;  // moved lower
    ui_draw_pfp(fb, spiral_x, spiral_y);
}

/* Public: Draw login screen with text boxes (moved lower) */
void ui_draw_login(framebuffer_t *fb, const char *username, const char *password) {
    ui_draw_base(fb, 20);
    
    int box_width = 200;
    int box_height = 30 * FONT_SCALE;
    int username_box_x = (fb->width - box_width) / 2;
    int username_box_y = 450;  // moved down a few spaces
    int password_box_x = username_box_x;
    int password_box_y = username_box_y + box_height + 10;
    
    fb_draw_rect_outline(fb, username_box_x - 5, username_box_y - 5, box_width + 10, box_height + 10, 0xFFFFFF);
    fb_draw_rect_outline(fb, password_box_x - 5, password_box_y - 5, box_width + 10, box_height + 10, 0xFFFFFF);
    
    fb_draw_text(fb, username_box_x, username_box_y - 20, "Username:", 0xFFFFFF);
    fb_draw_text(fb, username_box_x, username_box_y + 5, username, 0x00FF00);
    
    fb_draw_text(fb, password_box_x, password_box_y - 20, "Password:", 0xFFFFFF);
    char masked[256] = {0};
    int len = strlen(password);
    for (int i = 0; i < len && i < 255; i++) {
        masked[i] = '*';
    }
    fb_draw_text(fb, password_box_x, password_box_y + 5, masked, 0x00FF00);
    
    msync(fb->fb_ptr, fb->fb_size, MS_SYNC);
    fb_update_display(fb);
}

/* Public: Draw error message screen (with base UI still visible) */
void ui_draw_error(framebuffer_t *fb, const char *message) {
    ui_draw_base(fb, 20);
    fb_draw_text(fb, 10, fb->height - 40, message, 0xFF0000);
    msync(fb->fb_ptr, fb->fb_size, MS_SYNC);
    fb_update_display(fb);
}

/* Public: Draw welcome screen (keep base UI and place message lower) */
void ui_draw_welcome(framebuffer_t *fb, const char *username) {
    ui_draw_base(fb, 20);
    char welcome[256];
    snprintf(welcome, sizeof(welcome), "Welcome, %s!", username);
    int text_width = strlen(welcome) * 8 * FONT_SCALE;
    int x = (fb->width - text_width) / 2;
    int y = 420;  // positioned a few spaces lower
    fb_draw_text(fb, x, y, welcome, 0xFFFFFF);
    msync(fb->fb_ptr, fb->fb_size, MS_SYNC);
    fb_update_display(fb);
}

/* Public: Draw a general message screen (again, base UI remains) */
void ui_draw_message(framebuffer_t *fb, const char *msg) {
    ui_draw_base(fb, 20);
    int text_width = strlen(msg) * 8 * FONT_SCALE;
    int x = (fb->width - text_width) / 2;
    int y = 420;  // message position, lower than before
    fb_draw_text(fb, x, y, msg, 0xFFFFFF);
    msync(fb->fb_ptr, fb->fb_size, MS_SYNC);
    fb_update_display(fb);
}

