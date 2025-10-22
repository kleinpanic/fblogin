#ifndef FBLOGIN_CONFIG_H
#define FBLOGIN_CONFIG_H

/* Build-time feature toggles (can be overridden via -D in CFLAGS) */

/* Enable fingerprint-first path via pam_fprintd (runtime can still disable with FBLOGIN_FINGERPRINT=0) */
#ifndef FBLOGIN_WITH_FPRINTD
#define FBLOGIN_WITH_FPRINTD 1
#endif

/* Enforce that user sessions must always succeed pam_open_session in production. */
#ifndef FBLOGIN_STRICT_SESSION
#define FBLOGIN_STRICT_SESSION 1
#endif

/* Auto-enable sd-login diagnostics if libsystemd is available (Makefile sets -DHAVE_SYSTEMD_LOGIN=1) */
#ifndef HAVE_SYSTEMD_LOGIN
#define HAVE_SYSTEMD_LOGIN 0
#endif

/* Default framebuffer device path */
#ifndef FBLOGIN_DEFAULT_FBDEV
#define FBLOGIN_DEFAULT_FBDEV "/dev/fb0"
#endif

/* Default: draw “cmatrix” background only if FBLOGIN_CMATRIX=1 is present */
#ifndef FBLOGIN_DEFAULT_CMATRIX
#define FBLOGIN_DEFAULT_CMATRIX 0
#endif

/* Greeter session env/class */
#ifndef FBLOGIN_GREETER_USER
#define FBLOGIN_GREETER_USER "root"   /* override to a dedicated system user if you create one */
#endif

/* Log timestamp format: 1 = realtime (us), 0 = none */
#ifndef FBLOGIN_LOG_TIMESTAMP
#define FBLOGIN_LOG_TIMESTAMP 1
#endif

#endif /* FBLOGIN_CONFIG_H */

