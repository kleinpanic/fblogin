#include "fb.h"
#include "input.h"
#include "pam_auth.h"
#include "ui.h"
#include "version.h"
#include "config.h"

#include <pwd.h>
#include <grp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <libgen.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <linux/reboot.h>
#include <sys/prctl.h>
#include <termios.h>
#include <time.h>
#include <poll.h>

#if HAVE_SYSTEMD_LOGIN
#include <systemd/sd-login.h>
#endif

/* forward decls */
static void print_help(const char *argv0);
static void print_version(void);

/* ---------- debug logger ---------- */
static FILE *g_log = NULL;
static int debug_enabled = 0;
static char g_log_path[512] = {0};
/* when nonzero, we pin logs to a specific file and never switch to per-user */
static int  g_log_forced = 0;
static char g_log_forced_path[512] = {0};
static void log_reopen_if_forced(void);

static void ts_now(char *buf, size_t n) {
#if FBLOGIN_LOG_TIMESTAMP
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);
    snprintf(buf, n, "%04d-%02d-%02d %02d:%02d:%02d.%06ld",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec/1000);
#else
    (void)snprintf(buf, n, "-");
#endif
}

static void dlog(const char *fmt, ...) {
    if (!debug_enabled || !g_log) return;
    char t[64]; ts_now(t, sizeof(t));
    pid_t pid = getpid(), ppid = getppid();
    pid_t pgid = getpgid(0), sid = getsid(0);
    va_list ap; va_start(ap, fmt);
    fprintf(g_log, "[%s pid=%ld ppid=%ld pgid=%ld sid=%ld] ",
            t, (long)pid, (long)ppid, (long)pgid, (long)sid);
    vfprintf(g_log, fmt, ap);
    fputc('\n', g_log);
    fflush(g_log);
    va_end(ap);
}

static void log_reopen_if_forced(void) {
    if (!debug_enabled || !g_log_forced || !g_log_forced_path[0]) return;
    FILE *old = g_log;
    g_log = NULL;
    if (old) fclose(old);
    int fd = open(g_log_forced_path, O_CREAT|O_APPEND|O_WRONLY, 0640);
    if (fd < 0) { g_log = old; return; }
    FILE *nf = fdopen(fd, "a");
    if (!nf) { close(fd); g_log = old; return; }
    g_log = nf; snprintf(g_log_path, sizeof(g_log_path), "%s", g_log_forced_path);
}

/* open an explicit path for logging (used by forced global log mode) */
static void log_open_path(const char *path) {
    if (!debug_enabled || g_log || !path || !*path) return;
    int fd = open(path, O_CREAT|O_APPEND|O_WRONLY, 0640);
    if (fd < 0) return;
    FILE *nf = fdopen(fd, "a");
    if (!nf) { close(fd); return; }
    g_log = nf;
    snprintf(g_log_path, sizeof(g_log_path), "%s", path);
    dlog("=== fblogin %s (pre-user) pid=%ld (forced log) ===", FBLOGIN_VERSION, (long)getpid());
}

static void log_open_preuser(void) {
    if (!debug_enabled || g_log) return;
    if (g_log_forced && g_log_forced_path[0]) {
        log_open_path(g_log_forced_path);
        if (g_log) return;
        /* fall back to tmp if forced failed */
    }
    snprintf(g_log_path, sizeof(g_log_path), "/tmp/fblogin-%ld.log", (long)getpid());
    g_log = fopen(g_log_path, "a");
    if (g_log) dlog("=== fblogin %s (pre-user) pid=%ld ===", FBLOGIN_VERSION, (long)getpid());
}

static void log_switch_to_user(const char *user) {
    if (!debug_enabled) return;
    if (g_log_forced) {
        /* keep writing to the forced global log; don't switch into $HOME */
        dlog("log pinned to %s; skipping per-user switch", g_log_path[0] ? g_log_path : "(unset)");
        return;
    }
    if (!user || !*user) return;
    struct passwd *pw = getpwnam(user);
    if (!pw || !pw->pw_dir || !*pw->pw_dir) { dlog("no home for %s", user); return; }

    char path[512];
    snprintf(path, sizeof(path), "%s/.fblogin.log", pw->pw_dir);
    int fd = open(path, O_CREAT|O_APPEND|O_WRONLY, 0600);
    if (fd < 0) {
        dlog("open(%s): %s; fallback to /tmp", path, strerror(errno));
        snprintf(path, sizeof(path), "/tmp/fblogin-%s.log", user);
        fd = open(path, O_CREAT|O_APPEND|O_WRONLY, 0600);
        if (fd < 0) { dlog("fallback open(%s): %s", path, strerror(errno)); return; }
    }
    if (geteuid() == 0) (void)fchown(fd, pw->pw_uid, pw->pw_gid);

    FILE *nf = fdopen(fd, "a");
    if (!nf) { close(fd); dlog("fdopen: %s", strerror(errno)); return; }

    if (g_log) fclose(g_log);
    g_log = nf;
    snprintf(g_log_path, sizeof(g_log_path), "%s", path);
    dlog("=== fblogin %s (user=%s) pid=%ld ===", FBLOGIN_VERSION, user, (long)getpid());
}

/* ---------- signals ---------- */
static volatile sig_atomic_t stop_flag = 0;
/* self-pipe used to wake the poll loop on SIGTERM */
static int s_stop_pipe[2] = { -1, -1 };
static void on_sigterm(int s){
    (void)s;
    stop_flag = 1;
    if (s_stop_pipe[1] >= 0) {
        /* async-signal-safe */
        ssize_t _ = write(s_stop_pipe[1], "x", 1);
        (void)_;
    }
}

static int setup_stop_pipe(void) {
    if (pipe(s_stop_pipe) != 0) return -1;
    (void)fcntl(s_stop_pipe[0], F_SETFL, O_NONBLOCK);
    (void)fcntl(s_stop_pipe[1], F_SETFL, O_NONBLOCK);
    return 0;
}

/* ensure signals interrupt blocking syscalls (no SA_RESTART) */
static void install_signal_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; /* no SA_RESTART */

    /* SIGTERM from systemd -> graceful shutdown via self-pipe */
    sa.sa_handler = on_sigterm;
    sigaction(SIGTERM, &sa, NULL);

    /* user key combos shouldn’t kill or suspend the DM */
    struct sigaction ign;
    memset(&ign, 0, sizeof(ign));
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    sigaction(SIGINT,  &ign, NULL);  /* ^C */
    sigaction(SIGTSTP, &ign, NULL);  /* ^Z */
    sigaction(SIGQUIT, &ign, NULL);  /* ^\ */

    /* SIGUSR1 from logrotate -> reopen forced log file */
    struct sigaction hup;
    memset(&hup, 0, sizeof(hup));
    hup.sa_handler = (void (*)(int))log_reopen_if_forced;
    sigaction(SIGUSR1, &hup, NULL);
}

/* ---------- helpers ---------- */
static const char *best_tty_name(void) {
    const char *t = ttyname(STDIN_FILENO);
    if (t && *t) return t;
    static char buf[L_ctermid];
    if (ctermid(buf) && buf[0]) return buf;
    return "/dev/tty1";
}

static int running_under_systemd_unit(void) {
    const char *inv = getenv("INVOCATION_ID");
    return (inv && *inv) ? 1 : 0;
}

/* return 1 if /proc/self/loginuid is unset (4294967295), 0 otherwise */
static int loginuid_is_unset(void) {
    FILE *f = fopen("/proc/self/loginuid", "re");
    if (!f) return 1;
    unsigned long long v = 0ULL;
    int ok = (fscanf(f, "%llu", &v) == 1);
    fclose(f);
    return ok ? (v == 4294967295ULL) : 1;
}

#if HAVE_SYSTEMD_LOGIN
static void log_sd_login_probe(const char *tty) {
    uid_t uid = 0;
    char *sid = NULL;
    int r = sd_seat_get_active("seat0", &sid, &uid);
    if (r < 0) {
        dlog("sd-login: seat0 active query failed (%d)", r);
        return;
    }
    char *tty_of = NULL, *cls = NULL, *svc = NULL;
    (void)sd_session_get_tty(sid, &tty_of);
    (void)sd_session_get_class(sid, &cls);
    (void)sd_session_get_service(sid, &svc);
    dlog("sd-login: active seat0 session=%s tty=%s class=%s service=%s (our tty=%s)",
         sid, tty_of?tty_of:"?", cls?cls:"?", svc?svc:"?", tty?tty:"?");
    free(tty_of); free(cls); free(svc); free(sid);
}

static void log_sd_self_context(const char *our_tty) {
    char *sid = NULL, *cls = NULL, *seat = NULL, *sess_tty = NULL, *svc = NULL;
    uid_t suid = (uid_t)-1;
    if (sd_pid_get_session(0, &sid) >= 0) {
        (void)sd_session_get_class(sid, &cls);
        (void)sd_session_get_seat (sid, &seat);
        (void)sd_session_get_tty  (sid, &sess_tty);
        (void)sd_session_get_uid  (sid, &suid);
        (void)sd_session_get_service(sid, &svc);
        dlog("sd-login: self sid=%s class=%s seat=%s tty=%s uid=%ld service=%s (our tty=%s)",
             sid?sid:"-", cls?cls:"-", seat?seat:"-", sess_tty?sess_tty:"-",
             (long)suid, svc?svc:"-", our_tty?our_tty:"-");
    } else {
        dlog("sd-login: self not in a logind session (expected for DM greeter)");
    }
    free(sid); free(cls); free(seat); free(sess_tty); free(svc);
}
#endif

static int parse_vtnr(const char *tty) {
    if (!tty) return -1;
    if (strncmp(tty, "/dev/tty", 8) != 0) return -1;
    const char *p = tty + 8;
    if (!*p) return -1;
    char *end = NULL;
    long v = strtol(p, &end, 10);
    return (end && *end == '\0' && v > 0 && v < 1000) ? (int)v : -1;
}

static void kill_pgrp(pid_t pgid) {
    if (pgid <= 1) return;
    kill(-pgid, SIGHUP);
    usleep(200 * 1000);
    kill(-pgid, SIGTERM);
    for (int i = 0; i < 25; ++i) {
        if (kill(-pgid, 0) != 0 && errno == ESRCH) return;
        usleep(100 * 1000);
    }
    kill(-pgid, SIGKILL);
}

/* PAM UI -> framebuffer + log */
static void pam_ui_info_cb(const char *msg, void *user) {
    framebuffer_t *fb = (framebuffer_t *)user;
    dlog("PAM_INFO: %s", msg ? msg : "");
    if (fb) ui_draw_message(fb, (msg && *msg) ? msg : "");
}
static void pam_ui_err_cb(const char *msg, void *user) {
    framebuffer_t *fb = (framebuffer_t *)user;
    dlog("PAM_ERROR: %s", msg ? msg : "");
    if (fb) ui_draw_error(fb, (msg && *msg) ? msg : "authentication error");
}

/* Ensure controlling TTY in child; dup to stdio; drop to user; exec login shell */
static int run_login_shell_on_tty(const char *user, const char *tty_path, pam_session_t *ps) {
    struct passwd *pw = getpwnam(user);
    if (!pw) return -1;

    pam_export_env(ps);

    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        (void)prctl(PR_SET_PDEATHSIG, SIGTERM);
        setsid();
        int tfd = open(tty_path ? tty_path : "/dev/tty1", O_RDWR|O_CLOEXEC);
        if (tfd >= 0) {
            (void)ioctl(tfd, TIOCSCTTY, 0);
            (void)dup2(tfd, STDIN_FILENO);
            (void)dup2(tfd, STDOUT_FILENO);
            (void)dup2(tfd, STDERR_FILENO);
            if (tfd > 2) close(tfd);
        }

        if (initgroups(pw->pw_name, pw->pw_gid) != 0) _exit(125);
        if (setgid(pw->pw_gid) != 0) _exit(126);
        if (setuid(pw->pw_uid) != 0) _exit(127);

        const char *shell = (pw->pw_shell && *pw->pw_shell) ? pw->pw_shell : "/bin/sh";
        setenv("HOME", pw->pw_dir, 1);
        setenv("SHELL", shell, 1);
        setenv("USER", pw->pw_name, 1);
        setenv("LOGNAME", pw->pw_name, 1);
        if (!getenv("PATH")) setenv("PATH", "/usr/local/bin:/usr/bin:/bin", 1);

        char *bn = basename((char *)shell);
        size_t n = strlen(bn) + 2;
        char *arg0 = (char *)malloc(n);
        if (!arg0) _exit(129);
        arg0[0] = '-';
        memcpy(arg0 + 1, bn, n - 1);

        execl(shell, arg0, "-l", (char*)NULL);
        free(arg0);
        _exit(128);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) continue;
        break;
    }
    pid_t pg = getpgid(pid);
    if (pg > 1) kill_pgrp(pg);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static void draw_and_pause(framebuffer_t *fb, const char *msg, unsigned ms) {
    dlog("UI: %s", msg ? msg : "");
    ui_draw_error(fb, msg);
    usleep(ms * 1000);
}

/* Greeter PAM session keeper: open greeter session in a child, hold until pipe closes */
static pid_t greeter_keeper_pid = -1;
static int   greeter_keeper_wfd = -1;

static int start_greeter_keeper(const char *service, const char *tty, pam_ui_t *pui) {
    int pfd[2];
    if (pipe(pfd) != 0) { dlog("pipe(): %s", strerror(errno)); return -1; }

    pid_t pid = fork();
    if (pid < 0) { dlog("fork(): %s", strerror(errno)); close(pfd[0]); close(pfd[1]); return -1; }
    if (pid == 0) {
        close(pfd[1]);
        pam_session_t gs = {0};
        int rc = pam_open_greeter_session(service, tty, ":0", pui, &gs);
        dlog("greeter_session open rc=%d (%s), tty=%s", rc, pam_errstr(rc), tty);
        if (rc != PAM_SUCCESS) _exit(10);

        char buf[1];
        while (1) {
            ssize_t r = read(pfd[0], buf, 1);
            if (r == 0) break;
            if (r < 0 && errno == EINTR) continue;
            if (r < 0) break;
        }
        pam_end_session(&gs);
        dlog("greeter_session closed");
        _exit(0);
    }
    close(pfd[0]);
    greeter_keeper_pid = pid;
    greeter_keeper_wfd = pfd[1];
    dlog("greeter_keeper pid=%ld started", (long)pid);
    return 0;
}

static void stop_greeter_keeper(void) {
    if (greeter_keeper_wfd >= 0) {
        close(greeter_keeper_wfd);
        greeter_keeper_wfd = -1;
    }
    if (greeter_keeper_pid > 0) {
        int st = 0;
        (void)waitpid(greeter_keeper_pid, &st, 0);
        dlog("greeter_keeper pid=%ld exit st=%d", (long)greeter_keeper_pid, st);
        greeter_keeper_pid = -1;
    }
}

/* ---------- CLI helpers ---------- */
static void print_version(void) {
    /* FBLOGIN_VERSION comes from version.h */
    printf("fblogin %s\n", FBLOGIN_VERSION);
}

static void print_help(const char *argv0) {
    const char *prog = argv0 ? argv0 : "fblogin";
    printf(
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  -h, --help            Show this help and exit\n"
        "  -V, --version         Show version and exit\n"
        "  -d, --debug           Enable debug logging (can also set FBLOGIN_DEBUG=1)\n"
        "      --dev             Developer mode (relaxes some context checks)\n"
        "      --no-fp           Disable fingerprint path\n"
        "      --fp-max=N        Cosmetic max retries shown for fingerprint (1..9)\n"
        "      --log-file=PATH   Force all logs to PATH (implies --debug)\n"
        "\n"
        "Environment:\n"
        "  FBLOGIN_DEBUG=1          enable debug logs\n"
        "  FBLOGIN_LOG_FILE=/path   force global log file (pair with DEBUG=1)\n"
        "  FBLOGIN_FINGERPRINT=0    disable fingerprint\n"
        "  FBLOGIN_DEV=1            enable developer mode\n"
        "  FBLOGIN_CMATRIX=1        enable cmatrix background\n"
        "\n", prog);
}

/* ---------- main ---------- */
int main(int argc, char **argv) {
    int dev_mode = 0;
    int fp_enabled = FBLOGIN_WITH_FPRINTD;
    int fp_max = 3; /* cosmetic; PAM module controls real retries */
    int show_help = 0, show_version = 0;

    /* allow --log-file=/path to also imply debug on */
    /* (systemd env FBLOGIN_DEBUG=1 still supported/used below) */
    const char *arg_log_file = NULL;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--debug")) debug_enabled = 1;
        else if (!strcmp(argv[i], "--dev")) dev_mode = 1;
        else if (!strcmp(argv[i], "--no-fp")) fp_enabled = 0;
        else if (!strncmp(argv[i], "--fp-max=", 9)) {
            int v = atoi(argv[i] + 9);
            if (v > 0 && v < 10) fp_max = v;
        }
        else if (!strncmp(argv[i], "--log-file=", 11)) {
            arg_log_file = argv[i] + 11;
            if (arg_log_file && *arg_log_file) {
                strncpy(g_log_forced_path, arg_log_file, sizeof(g_log_forced_path)-1);
                g_log_forced_path[sizeof(g_log_forced_path)-1] = '\0';
                g_log_forced = 1;
                debug_enabled = 1; /* make --log-file alone sufficient */
            }
        }
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            show_help = 1;
        }
        else if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-V")) {
            show_version = 1;
        }
    }
    if (show_help)    { print_help(argv[0]);    return 0; }
    if (show_version) { print_version();        return 0; }

    if (!debug_enabled) {
        const char *env = getenv("FBLOGIN_DEBUG");
        if (env && *env == '1') debug_enabled = 1;
    }
    {
        const char *envfp = getenv("FBLOGIN_FINGERPRINT");
        if (envfp && *envfp == '0') fp_enabled = 0;
    }
    /* environment override for global log path */
    if (!g_log_forced) {
        const char *env_log = getenv("FBLOGIN_LOG_FILE");
        if (env_log && *env_log) {
            strncpy(g_log_forced_path, env_log, sizeof(g_log_forced_path)-1);
            g_log_forced_path[sizeof(g_log_forced_path)-1] = '\0';
            g_log_forced = 1;
            /* do not force debug here; we expect FBLOGIN_DEBUG=1 in service */
        }
    }
    if (!dev_mode) {
        const char *envd = getenv("FBLOGIN_DEV");
        if (envd && *envd == '1') dev_mode = 1;
    }
    if (running_under_systemd_unit()) dev_mode = 0;

    if (debug_enabled) log_open_preuser();

    (void)setup_stop_pipe(); /* best-effort; still fine if it fails */
    /* install handlers after pipe so SIGTERM can wake poll() */
    install_signal_handlers();

    if (!isatty(STDIN_FILENO)) {
        fprintf(stderr, "fblogin: stdin is not a TTY\n");
        return 1;
    }
    const char *tty = best_tty_name();
    int vtnr = parse_vtnr(tty);

    if (geteuid() != 0) {
        fprintf(stderr, "fblogin must be setuid root. Run `sudo make install`.\n");
        return 1;
    }

    dlog("Start fblogin %s on TTY=%s (vtnr=%d), EUID=%d, mode=%s, fp_max=%d",
         FBLOGIN_VERSION, tty, vtnr, (int)geteuid(), dev_mode ? "dev" : "prod", fp_max);

#if HAVE_SYSTEMD_LOGIN
    log_sd_login_probe(tty);
    log_sd_self_context(tty);
#endif
    dlog("loginuid unset? %d", loginuid_is_unset());

    framebuffer_t fb;
    if (fb_init(&fb, FBLOGIN_DEFAULT_FBDEV) != 0) {
        fprintf(stderr, "fblogin: fb init failed\n");
        return 1;
    }
    if (input_init() != 0) {
        fb_close(&fb);
        fprintf(stderr, "fblogin: input init failed\n");
        return 1;
    }

    const char *env_cmx = getenv("FBLOGIN_CMATRIX");
    if (env_cmx ? (*env_cmx == '1') : FBLOGIN_DEFAULT_CMATRIX) ui_set_cmatrix(1);

    pam_ui_t pui = (pam_ui_t){ .info = pam_ui_info_cb, .error = pam_ui_err_cb, .user_ctx = &fb };

    /* Context guard: don’t behave like a DM inside a user session. */
    int enable_greeter = 1;
#if HAVE_SYSTEMD_LOGIN
    {
        char *sid=NULL, *cls=NULL, *sess_tty=NULL;
        if (sd_pid_get_session(0, &sid) >= 0) {
            (void)sd_session_get_class(sid, &cls);
            (void)sd_session_get_tty(sid, &sess_tty);
            dlog("Context guard: in-session sid=%s class=%s tty=%s", sid?sid:"?", cls?cls:"?", sess_tty?sess_tty:"?");
            if (!dev_mode) {
                ui_draw_error(&fb, "Already running inside a user session.\nStart fblogin via systemd on a free VT (e.g., tty1).");
                usleep(1200 * 1000);
                input_restore();
                fb_close(&fb);
                if (g_log) fclose(g_log);
                free(sid); free(cls); free(sess_tty);
                return 2;
            } else {
                enable_greeter = 0;
                dlog("Dev-mode: disabling greeter; running without greeter session.");
            }
        }
        free(sid); free(cls); free(sess_tty);
    }
#endif

    if (enable_greeter) (void)start_greeter_keeper("fblogin", tty, &pui);

    char user[64] = {0};
    char pass[128] = {0};
    size_t ulen = 0, plen = 0;
    enum { FOCUS_USER = 0, FOCUS_PASS = 1 } focus = FOCUS_USER;

    while (!stop_flag) {
        ui_draw_login(&fb, user, pass, (focus == FOCUS_PASS));

        /* Wait for either keyboard input or a stop request */
        int ch = -1;
        struct pollfd fds[2];
        fds[0].fd = STDIN_FILENO;     fds[0].events = POLLIN;
        fds[1].fd = s_stop_pipe[0];   fds[1].events = POLLIN;

        int pr = poll(fds, (s_stop_pipe[0] >= 0) ? 2 : 1, -1);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (s_stop_pipe[0] >= 0 && (fds[1].revents & POLLIN)) {
            /* drain pipe, then exit loop */
            char buf[64];
            while (read(s_stop_pipe[0], buf, sizeof(buf)) > 0) {}
            break;
        }
        if (fds[0].revents & POLLIN) {
            ch = input_getchar();
        } else {
            /* nothing useful; re-check stop_flag */
            continue;
        }
        if (ch < 0) continue;

        if (ch == '\t') { focus = (focus == FOCUS_USER) ? FOCUS_PASS : FOCUS_USER; dlog("TAB -> focus=%s", focus==FOCUS_PASS?"PASS":"USER"); continue; }
        if (ch == 0x15) { if (focus==FOCUS_PASS){ memset(pass,0,sizeof(pass)); plen=0; dlog("Ctrl-U: clear password"); } else { memset(user,0,sizeof(user)); ulen=0; dlog("Ctrl-U: clear username"); } continue; }
        if (ch == 0x12) { memset(user,0,sizeof(user)); ulen=0; memset(pass,0,sizeof(pass)); plen=0; focus=FOCUS_USER; dlog("Ctrl-R: reset all"); continue; }

        if (ch == 0x04) { /* Ctrl-D -> restart greeter (do NOT reboot) */
            dlog("Ctrl-D: restart greeter");
            memset(user,0,sizeof(user)); ulen=0; memset(pass,0,sizeof(pass)); plen=0; focus=FOCUS_USER;
            ui_draw_login(&fb, user, pass, 0);
            continue;
        }

        if (ch == '\n' || ch == '\r') {
            dlog("Enter on focus=%s, ulen=%zu, plen=%zu", focus==FOCUS_PASS?"PASS":"USER", ulen, plen);

            if (focus == FOCUS_USER) {
                if (ulen == 0) { draw_and_pause(&fb, "enter username", 800); continue; }
                if (plen == 0) {
                    if (debug_enabled) log_switch_to_user(user);

#if FBLOGIN_WITH_FPRINTD
                    if (fp_enabled) {
                        dlog("Attempt fingerprint-first for user=%s (empty password)", user);
                        pam_session_t ps = (pam_session_t){0};
                        int rc = pam_begin("fblogin", user, "", tty, ":0", &pui, 1, &ps);
                        if (rc == PAM_SUCCESS) {
                            dlog("Fingerprint path OK");
                            ui_draw_welcome(&fb, user);

                            if (enable_greeter) stop_greeter_keeper();

                            int rcsh = run_login_shell_on_tty(user, tty, &ps);
                            pam_end_session(&ps);
                            dlog("Shell exit rc=%d", rcsh);

                            if (enable_greeter) (void)start_greeter_keeper("fblogin", tty, &pui);

                            memset(user, 0, sizeof(user)); ulen = 0; focus = FOCUS_USER;
                            draw_and_pause(&fb, "session ended; returning to greeter", 800);
                            continue;
                        }

                        dlog("Fingerprint path FAILED (%s)", pam_errstr(rc));
                        draw_and_pause(&fb, "fingerprint failed; enter password", 900);
                        focus = FOCUS_PASS;
                        continue;
                    }
#endif
                    dlog("Fingerprint disabled; focusing password");
                    focus = FOCUS_PASS;
                    draw_and_pause(&fb, "enter password", 300);
                    continue;
                }
            }

            if (focus == FOCUS_PASS) {
                if (ulen == 0) { draw_and_pause(&fb, "enter username first", 600); focus = FOCUS_USER; continue; }
                if (plen == 0) { draw_and_pause(&fb, "enter password or use fingerprint", 700); continue; }
                if (debug_enabled) log_switch_to_user(user);
            }

            dlog("Password auth attempt for user=%s", user);
            pam_session_t ps = (pam_session_t){0};
            int rc = pam_begin("fblogin", user, pass, tty, ":0", &pui, 1, &ps);
            memset(pass, 0, sizeof(pass)); plen = 0;

            if (rc != PAM_SUCCESS) {
#if !FBLOGIN_STRICT_SESSION
                if (rc == PAM_SESSION_ERR || rc == PAM_PERM_DENIED) {
                    dlog("Session open failed (%s). Dev-mode fallback AUTH-ONLY.", pam_errstr(rc));
                    pam_session_t ps2 = (pam_session_t){0};
                    rc = pam_begin("fblogin", user, "", tty, ":0", &pui, 0, &ps2);
                    if (rc == PAM_SUCCESS) {
                        dlog("AUTH-ONLY OK");
                        ui_draw_welcome(&fb, user);
                        if (enable_greeter) stop_greeter_keeper();
                        int rcsh = run_login_shell_on_tty(user, tty, &ps2);
                        pam_end_session(&ps2);
                        dlog("Shell exit rc=%d", rcsh);
                        if (enable_greeter) (void)start_greeter_keeper("fblogin", tty, &pui);
                        memset(user,0,sizeof(user)); ulen=0; focus=FOCUS_USER;
                        draw_and_pause(&fb, "session ended; returning to greeter", 800);
                        continue;
                    }
                }
#endif
                dlog("Password path FAILED (%s)", pam_errstr(rc));
                if (rc == PAM_SESSION_ERR)
                    draw_and_pause(&fb, "PAM session failed (TTY busy or started inside a session).", 1200);
                else
                    draw_and_pause(&fb, "authentication failed", 1000);
                focus = FOCUS_PASS;
                continue;
            }

            dlog("Password path OK");
            ui_draw_welcome(&fb, user);

            if (enable_greeter) stop_greeter_keeper();

            int rcsh = run_login_shell_on_tty(user, tty, &ps);
            pam_end_session(&ps);
            dlog("Shell exit rc=%d", rcsh);

            if (enable_greeter) (void)start_greeter_keeper("fblogin", tty, &pui);

            memset(user, 0, sizeof(user)); ulen = 0; focus = FOCUS_USER;
            draw_and_pause(&fb, "session ended; returning to greeter", 800);
            continue;
        }

        if (ch == 0x7F || ch == 0x08) { if (focus==FOCUS_PASS){ if (plen) pass[--plen]=0; } else { if (ulen) user[--ulen]=0; } continue; }
        if (ch >= 0x20 && ch <= 0x7E) { if (focus==FOCUS_PASS){ if (plen+1<sizeof(pass)){ pass[plen++]=(char)ch; pass[plen]=0; } } else { if (ulen+1<sizeof(user)){ user[ulen++]=(char)ch; user[ulen]=0; } } }
    }

    (void)input_restore();
    fb_close(&fb);
    if (enable_greeter) stop_greeter_keeper();
    if (s_stop_pipe[0] >= 0) { close(s_stop_pipe[0]); s_stop_pipe[0] = -1; }
    if (s_stop_pipe[1] >= 0) { close(s_stop_pipe[1]); s_stop_pipe[1] = -1; }
    if (g_log) fclose(g_log);
    return 0;
}

