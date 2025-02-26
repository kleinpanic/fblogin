#include "input.h"
#include <termios.h>
#include <unistd.h>
#include <stdio.h>

static struct termios orig_termios;

int input_init() {
    if (tcgetattr(STDIN_FILENO, &orig_termios) < 0) {
        perror("tcgetattr");
        return -1;
    }
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO); // disable canonical mode and echo
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) < 0) {
        perror("tcsetattr");
        return -1;
    }
    return 0;
}

int input_restore() {
    if (tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios) < 0) {
        perror("tcsetattr restore");
        return -1;
    }
    return 0;
}

int input_getchar() {
    char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n < 0) {
        perror("read");
        return -1;
    }
    return c;
}

