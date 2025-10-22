#ifndef UI_H
#define UI_H

#include <stdint.h>
#include "fb.h"

#ifndef FONT_SCALE
#define FONT_SCALE 2
#endif

/* Enable/disable cmatrix background animation. */
void ui_set_cmatrix(int flag);

/* Draw login screen. If focus_on_password!=0, password box is highlighted. */
void ui_draw_login(framebuffer_t *fb,
                   const char *username,
                   const char *password,
                   int focus_on_password);

void ui_draw_error(framebuffer_t *fb, const char *message);
void ui_draw_welcome(framebuffer_t *fb, const char *username);
void ui_draw_message(framebuffer_t *fb, const char *msg);

#endif /* UI_H */

