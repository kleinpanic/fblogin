#ifndef INPUT_H
#define INPUT_H

/* Raw TTY input helpers (canonical off, echo off). */
int  input_init(void);
int input_restore(void);
/* Blocking getch from /dev/tty. Returns byte [0..255] or -1 on error. */
int  input_getchar(void);

#endif

