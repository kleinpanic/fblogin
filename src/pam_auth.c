#include "pam_auth.h"
#include <security/pam_misc.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct conv_ctx_s {
    const char     *password; /* may be NULL or "" */
    const pam_ui_t *ui;       /* may be NULL */
} conv_ctx_t;

static int conv_fn(int num_msg, const struct pam_message **msg,
                   struct pam_response **resp, void *appdata_ptr) {
    if (num_msg <= 0) return PAM_CONV_ERR;
    struct pam_response *r = calloc((size_t)num_msg, sizeof(*r));
    if (!r) return PAM_BUF_ERR;

    conv_ctx_t *ctx = (conv_ctx_t *)appdata_ptr;
    const char *password = ctx ? ctx->password : NULL;
    const pam_ui_t *ui   = ctx ? ctx->ui : NULL;

    for (int i = 0; i < num_msg; i++) {
        switch (msg[i]->msg_style) {
            case PAM_PROMPT_ECHO_OFF:
            case PAM_PROMPT_ECHO_ON: {
                const char *ans = (password && *password) ? password : "";
                r[i].resp = strdup(ans ? ans : "");
                r[i].resp_retcode = 0;
                break;
            }
            case PAM_TEXT_INFO:
                if (ui && ui->info && msg[i]->msg) ui->info(msg[i]->msg, ui->user_ctx);
                r[i].resp = NULL;
                r[i].resp_retcode = 0;
                break;
            case PAM_ERROR_MSG:
                if (ui && ui->error && msg[i]->msg) ui->error(msg[i]->msg, ui->user_ctx);
                r[i].resp = NULL;
                r[i].resp_retcode = 0;
                break;
            default:
                free(r);
                return PAM_CONV_ERR;
        }
    }
    *resp = r;
    return PAM_SUCCESS;
}

static void set_item_if(pam_handle_t *pamh, int item_type, const char *val) {
    if (val && *val) (void)pam_set_item(pamh, item_type, (const void *)val);
}

static void set_env_if(pam_handle_t *pamh, const char *k, const char *v) {
    if (!pamh || !k || !*k || !v) return;
    char *kv = NULL;
    size_t len = strlen(k) + 1 + strlen(v) + 1;
    kv = malloc(len);
    if (!kv) return;
    snprintf(kv, len, "%s=%s", k, v);
    (void)pam_putenv(pamh, kv);
    free(kv);
}

int pam_begin(const char *service,
              const char *username,
              const char *password,
              const char *tty_opt,
              const char *xdisplay_opt,
              const pam_ui_t *ui,
              int open_session,
              pam_session_t *out) {
    if (!service || !username || !out) return PAM_SYSTEM_ERR;
    out->pamh = NULL;
    out->session_opened = 0;

    conv_ctx_t cctx = { .password = password ? password : "", .ui = ui };
    struct pam_conv conv = { .conv = conv_fn, .appdata_ptr = (void *)&cctx };
    pam_handle_t *pamh = NULL;

    int rc = pam_start(service, username, &conv, &pamh);
    if (rc != PAM_SUCCESS) goto fail;

    set_item_if(pamh, PAM_TTY, tty_opt);
    set_item_if(pamh, PAM_XDISPLAY, xdisplay_opt);

    rc = pam_authenticate(pamh, 0);
    if (rc != PAM_SUCCESS) goto fail;

    rc = pam_acct_mgmt(pamh, 0);
    if (rc == PAM_NEW_AUTHTOK_REQD)
        rc = pam_chauthtok(pamh, PAM_CHANGE_EXPIRED_AUTHTOK);
    if (rc != PAM_SUCCESS) goto fail;

    rc = pam_setcred(pamh, PAM_ESTABLISH_CRED);
    if (rc != PAM_SUCCESS) goto fail;

    if (open_session) {
        rc = pam_open_session(pamh, 0);
        if (rc != PAM_SUCCESS) {
            (void)pam_setcred(pamh, PAM_DELETE_CRED);
            goto fail;
        }
        out->session_opened = 1;
    }

    out->pamh = pamh;
    return PAM_SUCCESS;

fail:
    if (ui && ui->error) ui->error(pam_strerror(pamh, rc), (void*)ui->user_ctx);
    if (pamh) (void)pam_end(pamh, rc);
    return rc ? rc : PAM_SYSTEM_ERR;
}

int pam_open_greeter_session(const char *service,
                             const char *tty_opt,
                             const char *xdisplay_opt,
                             const pam_ui_t *ui,
                             pam_session_t *out) {
    if (!service || !out) return PAM_SYSTEM_ERR;
    out->pamh = NULL;
    out->session_opened = 0;

    /* Greeter: no auth conv needed, but PAM wants a conv. Provide a no-op one. */
    conv_ctx_t cctx = { .password = "", .ui = ui };
    struct pam_conv conv = { .conv = conv_fn, .appdata_ptr = (void *)&cctx };
    pam_handle_t *pamh = NULL;

    /* Use the current euid's name; typical DM greeter runs as root. */
    uid_t e = geteuid();
    struct passwd *pw = getpwuid(e);
    const char *uname = pw && pw->pw_name ? pw->pw_name : "root";

    int rc = pam_start(service, uname, &conv, &pamh);
    if (rc != PAM_SUCCESS) goto fail;

    set_item_if(pamh, PAM_TTY, tty_opt);
    set_item_if(pamh, PAM_XDISPLAY, xdisplay_opt);

    /* Set greeter class + seat/vtnr via PAM env so pam_systemd can see it. */
    set_env_if(pamh, "XDG_SESSION_CLASS", "greeter");
    /* Seat and VT can be inferred by pam_systemd, but pass when known. */
    if (tty_opt && !strncmp(tty_opt, "/dev/tty", 8)) {
        const char *n = tty_opt + 8;
        if (*n) set_env_if(pamh, "XDG_VTNR", n);
    }
    set_env_if(pamh, "XDG_SEAT", "seat0");

    rc = pam_open_session(pamh, 0);
    if (rc != PAM_SUCCESS) goto fail;

    out->pamh = pamh;
    out->session_opened = 1;
    return PAM_SUCCESS;

fail:
    if (ui && ui->error) ui->error(pam_strerror(pamh, rc), (void*)ui->user_ctx);
    if (pamh) (void)pam_end(pamh, rc);
    return rc ? rc : PAM_SYSTEM_ERR;
}

void pam_export_env(pam_session_t *ps) {
    if (!ps || !ps->pamh) return;
    char **envlist = pam_getenvlist(ps->pamh);
    if (!envlist) return;
    for (char **p = envlist; *p; ++p) {
        putenv(*p); /* intentionally leak into process env */
    }
}

void pam_end_session(pam_session_t *ps) {
    if (!ps || !ps->pamh) return;
    if (ps->session_opened) (void)pam_close_session(ps->pamh, 0);
    (void)pam_setcred(ps->pamh, PAM_DELETE_CRED);
    (void)pam_end(ps->pamh, PAM_SUCCESS);
    ps->pamh = NULL;
    ps->session_opened = 0;
}

const char *pam_errstr(int code) {
    switch (code) {
        case PAM_SUCCESS: return "PAM_SUCCESS";
        case PAM_OPEN_ERR: return "PAM_OPEN_ERR";
        case PAM_SYMBOL_ERR: return "PAM_SYMBOL_ERR";
        case PAM_SERVICE_ERR: return "PAM_SERVICE_ERR";
        case PAM_SYSTEM_ERR: return "PAM_SYSTEM_ERR";
        case PAM_BUF_ERR: return "PAM_BUF_ERR";
        case PAM_PERM_DENIED: return "PAM_PERM_DENIED";
        case PAM_AUTH_ERR: return "PAM_AUTH_ERR";
        case PAM_CRED_INSUFFICIENT: return "PAM_CRED_INSUFFICIENT";
        case PAM_AUTHINFO_UNAVAIL: return "PAM_AUTHINFO_UNAVAIL";
        case PAM_USER_UNKNOWN: return "PAM_USER_UNKNOWN";
        case PAM_MAXTRIES: return "PAM_MAXTRIES";
        case PAM_NEW_AUTHTOK_REQD: return "PAM_NEW_AUTHTOK_REQD";
        case PAM_ACCT_EXPIRED: return "PAM_ACCT_EXPIRED";
        case PAM_SESSION_ERR: return "PAM_SESSION_ERR";
        default: return "PAM_OTHER";
    }
}

