#ifndef PAM_AUTH_H
#define PAM_AUTH_H

#include <security/pam_appl.h>

typedef struct pam_ui_s {
    void (*info)(const char *msg, void *user);
    void (*error)(const char *msg, void *user);
    void *user_ctx;
} pam_ui_t;

typedef struct {
    pam_handle_t *pamh;
    int session_opened; /* 1 if pam_open_session was called successfully */
} pam_session_t;

/* Full login path: authenticate + acct_mgmt (+chauthtok) + setcred + open_session (if open_session!=0)
   tty_opt/xdisplay_opt are forwarded as PAM items; env is exported via pam_export_env().
   Returns PAM_* code (PAM_SUCCESS on success). */
int pam_begin(const char *service,
              const char *username,
              const char *password,
              const char *tty_opt,
              const char *xdisplay_opt,
              const pam_ui_t *ui,
              int open_session,
              pam_session_t *out);

/* Open a GREETER session only (no auth). Must be called in a process whose loginuid is unset.
   Sets PAM_TTY and populates PAM env (XDG_SESSION_CLASS=greeter, XDG_SEAT, XDG_VTNR) before pam_open_session.
   Returns PAM_* code. */
int pam_open_greeter_session(const char *service,
                             const char *tty_opt,
                             const char *xdisplay_opt,
                             const pam_ui_t *ui,
                             pam_session_t *out);

/* Export PAM env into current process. */
void pam_export_env(pam_session_t *ps);

/* Close session if opened; delete creds; end PAM. Safe to call multiple times. */
void pam_end_session(pam_session_t *ps);

/* Pretty for logs; never NULL. */
const char *pam_errstr(int code);

#endif

