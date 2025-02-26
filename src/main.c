#include "fb.h"
#include "input.h"
#include "pam_auth.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <grp.h>
#include <sys/mman.h>
#include "version.h"

#define MAX_INPUT 256

static framebuffer_t fb;
volatile sig_atomic_t restart_requested = 0;

int is_fprintd_available() {
    return (access("/usr/bin/fprintd-list", X_OK) == 0);
}

void restore_and_exit(int exit_status) {
    printf("\e[?25h");
    fflush(stdout);
    input_restore();
    fb_clear(&fb, 0x000000);
    fb_close(&fb);
    exit(exit_status);
}

void restart_handler(int signum) {
    (void)signum;
    restart_requested = 1;
}

void ignore_handler(int signum) {
    (void)signum;
}

/* Attempt fingerprint authentication.
   Debug prints have been commented out. */
int try_fingerprint(const char *username) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "/usr/bin/fprintd-list %s 2>&1", username);
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        // Debug print commented out.
        // perror("popen");
        return -1;
    }
    char buffer[1024];
    int enrolled = 0;
    char finger[64] = {0};
    while (fgets(buffer, sizeof(buffer), fp)) {
        if (strstr(buffer, "- #")) {
            char *colon = strchr(buffer, ':');
            if (colon) {
                colon++;
                while (*colon == ' ')
                    colon++;
                strncpy(finger, colon, sizeof(finger) - 1);
                finger[sizeof(finger) - 1] = '\0';
                char *newline = strchr(finger, '\n');
                if (newline)
                    *newline = '\0';
            }
            enrolled = 1;
            break;
        }
    }
    pclose(fp);
    if (!enrolled) {
        return -1;
    }
    ui_draw_message(&fb, "Place your finger on the sensor");
    sleep(1);
    pid_t pid = fork();
    if (pid < 0) {
        // Debug print commented out.
        // perror("fork");
        return -1;
    } else if (pid == 0) {
        execl("/usr/bin/fprintd-verify", "fprintd-verify", "-f", finger, username, (char*)NULL);
        // Debug print commented out.
        // perror("execl fprintd-verify");
        exit(1);
    } else {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status))
            return WEXITSTATUS(status);
        return -1;
    }
}

int main(int argc, char **argv) {
    int use_cmatrix = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--cmatrix") == 0) {
            use_cmatrix = 1;
	} else if (strcmp(argv[i], "--version") == 0) {
            printf("fblogin version %s\n", FBLOGIN_VERSION);
            return 0;
        }
    }
    ui_set_cmatrix(use_cmatrix);
    
    /* Optionally restrict to tty1:
    char *tty = ttyname(STDIN_FILENO);
    if (!tty || strstr(tty, "tty1") == NULL) {
        fprintf(stderr, "This login program must be run on tty1\n");
        exit(EXIT_FAILURE);
    }
    */
    
    if(getuid() != 0) {
        fprintf(stderr, "This program must be run as root\n");
        exit(EXIT_FAILURE);
    }
    
    if (fb_init(&fb, "/dev/fb0") < 0) {
        fprintf(stderr, "Failed to initialize framebuffer\n");
        exit(EXIT_FAILURE);
    }
    
    if (input_init() < 0) {
        fprintf(stderr, "Failed to initialize input\n");
        fb_close(&fb);
        exit(EXIT_FAILURE);
    }
    
    printf("\e[?25l");
    fflush(stdout);
    
    signal(SIGINT, restart_handler);
    signal(SIGQUIT, restart_handler);
    signal(SIGTSTP, ignore_handler);
    
restart:
    {
        char username[MAX_INPUT] = {0};
        char password[MAX_INPUT] = {0};
        int pos_username = 0;
        int pos_password = 0;
        int auth_success = 0;
        int in_password_phase = 0;
        
        /* Username Input Phase */
        while (1) {
            if (restart_requested) {
                memset(username, 0, sizeof(username));
                pos_username = 0;
                restart_requested = 0;
                ui_draw_error(&fb, "Restarting login prompt...");
                sleep(1);
            }
            ui_draw_login(&fb, username, "");
            int c = input_getchar();
            if (c == 4) {  // Ctrl-D: clear username
                memset(username, 0, sizeof(username));
                pos_username = 0;
                ui_draw_error(&fb, "Username cleared");
                sleep(1);
                continue;
            }
            if (c == '\n' || c == '\r') {
                if (pos_username > 0)
                    break;
            } else if (c == 127 || c == 8) {
                if (pos_username > 0)
                    username[--pos_username] = '\0';
            } else if (c >= 32 && c <= 126) {
                if (pos_username < MAX_INPUT - 1) {
                    username[pos_username++] = (char)c;
                    username[pos_username] = '\0';
                }
            }
        }
        
        /* Fingerprint Authentication Phase */
        if (is_fprintd_available()) {
            int fp_ret = try_fingerprint(username);
            if (fp_ret == 0) {
                auth_success = 1;
            } else {
                ui_draw_error(&fb, "Fingerprint auth failed, fallback to password");
                sleep(2);
                in_password_phase = 1;
            }
        } else {
            in_password_phase = 1;
        }
        
        /* Password Input Phase (Fallback) */
        while (in_password_phase && !auth_success) {
            ui_draw_login(&fb, username, password);
            int c = input_getchar();
            if (c == 4) {  // Ctrl-D: restart (clear both)
                memset(username, 0, sizeof(username));
                memset(password, 0, sizeof(password));
                pos_username = pos_password = 0;
                ui_draw_error(&fb, "Restarting login prompt...");
                sleep(1);
                goto restart;
            }
            if (c == '\n' || c == '\r') {
                if (pos_password > 0) {
                    int ret = authenticate_user(username, password);
                    if (ret == 0) {
                        auth_success = 1;
                        break;
                    } else {
                        ui_draw_error(&fb, "Authentication failed. Try again.");
                        sleep(2);
                        memset(password, 0, sizeof(password));
                        pos_password = 0;
                    }
                }
            } else if (c == 127 || c == 8) {
                if (pos_password > 0)
                    password[--pos_password] = '\0';
            } else if (c >= 32 && c <= 126) {
                if (pos_password < MAX_INPUT - 1) {
                    password[pos_password++] = (char)c;
                    password[pos_password] = '\0';
                }
            }
        }
        
        if (!auth_success) {
            ui_draw_error(&fb, "Authentication failed.");
            sleep(2);
            restore_and_exit(EXIT_FAILURE);
        }
        
        struct passwd *pw = getpwnam(username);
        if (!pw) {
            // Debug print commented out.
            // fprintf(stderr, "User not found: %s\n", username);
            restore_and_exit(EXIT_FAILURE);
        }
        
        ui_draw_welcome(&fb, username);
        sleep(2);
        
        fb_clear(&fb, 0x000000);
        printf("\e[?25h");
        fflush(stdout);
        input_restore();
        fb_close(&fb);
        
        setenv("HOME", pw->pw_dir, 1);
        setenv("USER", pw->pw_name, 1);
        setenv("LOGNAME", pw->pw_name, 1);
        setenv("SHELL", pw->pw_shell, 1);
        
        if(chdir(pw->pw_dir) < 0)
            perror("chdir");
        
        if(initgroups(username, pw->pw_gid) < 0) {
            perror("initgroups");
            exit(EXIT_FAILURE);
        }
        if(setgid(pw->pw_gid) < 0) {
            perror("setgid");
            exit(EXIT_FAILURE);
        }
        if(setuid(pw->pw_uid) < 0) {
            perror("setuid");
            exit(EXIT_FAILURE);
        }
        
        setsid();
        
        char *shell = pw->pw_shell;
        if(!shell || shell[0] == '\0')
            shell = "/bin/sh";
        
        char shell_name[256] = {0};
        const char *base = strrchr(shell, '/');
        if(base)
            base++;
        else
            base = shell;
        snprintf(shell_name, sizeof(shell_name), "-%s", base);
        
        char *const args[] = { shell, shell_name, NULL };
        execv(shell, args);
        
        perror("execv");
        exit(EXIT_FAILURE);
    }
}

