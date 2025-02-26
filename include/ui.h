#ifndef UI_H
#define UI_H

#include "fb.h"

void ui_draw_login(framebuffer_t *fb, const char *username, const char *password);
void ui_draw_error(framebuffer_t *fb, const char *message);
void ui_draw_welcome(framebuffer_t *fb, const char *username);
void ui_draw_message(framebuffer_t *fb, const char *msg);
void ui_set_cmatrix(int flag);

#endif

