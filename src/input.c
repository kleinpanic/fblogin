#include "input.h"
#include <termios.h>
#include <unistd.h>

static struct termios saved;
static int inited = 0;

int input_init(void) {
    if (inited) return 0;
    if (!isatty(STDIN_FILENO)) return -1;
    if (tcgetattr(STDIN_FILENO, &saved) != 0) return -1;

    struct termios t = saved;
    t.c_lflag &= ~(ICANON | ECHO);
    t.c_cc[VMIN]  = 1;
    t.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) != 0) return -1;

    inited = 1;
    return 0;
}

int input_restore(void) {
    if (!inited) return 0;
    int rc = tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved);
    inited = 0;
    return rc;
}

int input_getchar(void) {
    unsigned char c;
    ssize_t r = read(STDIN_FILENO, &c, 1);
    if (r == 1) return (int)c;
    return -1;
}

