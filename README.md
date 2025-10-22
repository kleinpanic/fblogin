/¯¯¯¯/\¯¯¯¯\·. ''/¯¯¯¯/\¯¯¯¯\·. |¯¯¯¯|`·.'          '/¯¯¯¯/\¯¯¯¯\`·.''/¯¯¯¯/\¯¯¯¯\·.  |¯¯¯¯|`·.'/¯¯¯¯/`·.         
|`·.·´`·|::'¯¯¯¯`·.\|·´`·.·´|:/____/`·.|`·.·´`·|:::'          |·´`·.·´|::|`·.·´`·|:::|`·.·´`·|:::¯¯¯¯`·.\|.·´`·.·|:::|.·´`·.·|\¯¯¯¯\`·. 
|`·.·´`·|\¯¯¯¯\·.¨ '|·´`·.·´|:\¯¯¯¯\·./|`·.·´`·|::|¯¯¯¯|`·.|·´`·.·´|::|`·.·´`·|:::|`·.·´`·|:\¯¯¯¯\·. ¨|.·´`·.·|:::|.·´`·.·|:|`·.·´`·|:::'
|____|::¯¯¯¯`·.\''\____\/____/`·.'\____\/____/`·.''\____\/____/`·.''\____\/____/`·. |____|:::|____|:|____|:::'
'`·.:::::`·.¨ ¨ ¨ ¨ ¨ '  `·.::::::::::::'`·./  `·.::::::::::::'`·./' '`·.:::::::::::::`·./'  `·.::::::::::::'`·./ '`·.:::::`·.'`·.:::::`·`·.::::`·. 

# fblogin — framebuffer login for tty1

Framebuffer greeter that:
- draws directly to **fbdev** (no X/Wayland),
- authenticates via **PAM** (optional fprintd),
- binds a **logind** session on **tty1**,
- execs a **real login shell** on the same VT.

---

## Quick start

```bash
# Debian/Ubuntu deps
sudo apt-get install -y build-essential libpam0g-dev libfprint-dev fprintd pkg-config

# build + install (binary, unit, pam, banner, manpage)
make
sudo make install-all

# logging (global file, debug on)
sudo install -d -m 0755 /var/log/fblogin
sudo install -m 0640 /dev/null /var/log/fblogin/fblogin.log
sudo chown root:adm /var/log/fblogin/fblogin.log

# unit env (drop-in)
sudo systemctl edit fblogin@tty1.service
# [Service]
# Environment=FBLOGIN_DEBUG=1
# Environment=FBLOGIN_LOG_FILE=/var/log/fblogin/fblogin.log

# logrotate (SIGUSR1 reopen)
sudo tee /etc/logrotate.d/fblogin >/dev/null <<'ROT'
/var/log/fblogin/fblogin.log {
  rotate 7
  daily
  missingok
  notifempty
  compress
  create 0640 root adm
  postrotate
    systemctl kill -s SIGUSR1 fblogin@tty1.service 2>/dev/null || true
  endscript
}
ROT

sudo systemctl daemon-reload
sudo systemctl enable --now fblogin@tty1.service
```

Switch to tty1, log in (fingerprint or password). You land in a login shell on tty1; start your session from there (e.g., `startx`).

---

## Contents

- [Scope](#scope)
- [Features](#features)
- [Repository layout](#repository-layout)
- [Build/install targets](#buildinstall-targets)
- [Systemd integration](#systemd-integration)
- [PAM stack](#pam-stack)
- [Authentication flow](#authentication-flow)
- [Session model](#session-model)
- [TTY/signals](#ttysignals)
- [Logging modes](#logging-modes)
- [Xauthority/startx notes](#xauthoritystartx-notes)
- [setup.sh](#setupsh)
- [Man page](#man-page)
- [Security](#security)
- [Troubleshooting](#troubleshooting)
- [Contributing / License / Docs](#contributing--license--docs)

---

## Scope

**Is**
- Greeter for **tty1** using fbdev.
- PAM client that opens a proper **logind** session.
- Hands control to a login shell; session startup remains user-controlled.

**Is not**
- Full display manager (no seat juggling, no VT switching).
- Compositor or X/Wayland launcher.
- Aggressive manager of `.Xauthority` (guidance only).

---

## Features

- **Env-aware logging**
  - `FBLOGIN_DEBUG=1` toggles debug.
  - `FBLOGIN_LOG_FILE=…` pins a global log file (no per-user switch).
  - `SIGUSR1` reopens the forced log (for logrotate).
- **CLI**
  - `--help`, `--version` (from `include/version.h`)
  - `--log-file=/path` (implies debug; pins logging)
  - `--no-fp`, `--fp-max=N` (UI hint; PAM remains authoritative)
  - `--dev` (relax in-session guard), `--debug`
- **Greeter keeper**
  - Greeter PAM session in a child, held open until auth, then closed.
- **sd-login diagnostics** (if linked with libsystemd)
- **Self-pipe signal wakeups** (no SA_RESTART surprises)

---

## Repository layout

```
include/
  config.h  fb.h  input.h  pam_auth.h  ui.h  version.h
man/
  fblogin.1
src/
  fb.c  input.c  pam_auth.c  ui.c  main.c
system/
  systemd/fblogin@.service
  pamd/fblogin
  issue/issue.fblogin
docs/
  ARCHITECTURE.md  CHANGELOG.md  Contributing.md  TODO.md
Makefile
setup.sh
README.md
```

---

## Build/install targets

Variables:
- `PREFIX=/usr/local` (default)
- `BINDIR=$(PREFIX)/bin`
- `MANPREFIX=$(PREFIX)/share/man`, `MAN1DIR=$(MANPREFIX)/man1`
- `ISSUE_FILE=system/issue/issue.fblogin`

Targets:
```make
make                 # build
make debug|release|asan

sudo make install            # binary (4755)
sudo make install-systemd    # unit
sudo make install-pam        # /etc/pam.d/fblogin
sudo make install-issue      # /etc/issue.fblogin (optional)
sudo make install-man        # manpage
sudo make install-all        # all above

sudo make uninstall
sudo make uninstall-systemd
sudo make uninstall-pam
sudo make uninstall-issue
sudo make uninstall-man
sudo make uninstall-all
```

Uninstall is symmetric with defaults. If you changed `PREFIX`/`MAN*`, pass the same values to uninstall.

---

## Systemd integration

Installed unit (`system/systemd/fblogin@.service`):

```ini
[Unit]
Description=Framebuffer Login on %I
Documentation=man:systemd-logind.service(8) man:logind.conf(5)
After=systemd-user-sessions.service plymouth-quit-wait.service
Conflicts=getty@%i.service
ConditionPathExists=/dev/%I

[Service]
TTYPath=/dev/%I
TTYReset=yes
TTYVHangup=yes
StandardInput=tty
StandardOutput=tty
StandardError=tty

PAMName=fblogin
UtmpIdentifier=%I
UtmpMode=user

ExecStart=/usr/local/bin/fblogin
Type=simple
Restart=always
RestartSec=1

[Install]
WantedBy=multi-user.target
```

Recommended drop-in:

```ini
[Service]
Environment=FBLOGIN_DEBUG=1
Environment=FBLOGIN_LOG_FILE=/var/log/fblogin/fblogin.log
```

Logrotate:

```conf
/var/log/fblogin/fblogin.log {
  rotate 7
  daily
  missingok
  notifempty
  compress
  create 0640 root adm
  postrotate
    systemctl kill -s SIGUSR1 fblogin@tty1.service 2>/dev/null || true
  endscript
}
```

---

## PAM stack

`/etc/pam.d/fblogin`:

```
auth     include   common-auth
account  include   common-account
password include   common-password

session  required  pam_env.so
session  required  pam_loginuid.so
session  include   common-session
session  required  pam_systemd.so
```

- `pam_loginuid.so` sets `/proc/self/loginuid`.
- `pam_systemd.so` registers the session with logind.

---

## Authentication flow

- Username field first; `Tab` toggles focus.
- If password is empty and fingerprints are enabled, try the **fingerprint** path via PAM.
- On success: welcome screen → stop greeter keeper → exec user’s login shell.
- On failure: prompt for password and proceed via PAM.

Disable fingerprints:
- `--no-fp` or `FBLOGIN_FINGERPRINT=0`.

---

## Session model

- Runs on **tty1** (`TTYPath` + `UtmpMode=user`).
- Opens the user **PAM** session; logind sees `CLASS=user TYPE=tty`.
- Drops to user’s uid/gid, sets `HOME/SHELL/USER/LOGNAME` (+ fallback `PATH`), execs the login shell with `-l`.
- Start the graphical session from that shell (e.g., `startx`).

---

## TTY/signals

- `SIGTERM`: graceful shutdown via self-pipe to break `poll(2)`.
- `SIGUSR1`: reopen forced log file.
- `SIGINT`, `SIGQUIT`, `SIGTSTP`: ignored.

Keys:
- `Tab` toggle focus, `Ctrl-U` clear focused field, `Ctrl-R` reset both, `Ctrl-D` restart greeter UI.

---

## Logging modes

- **Default**: pre-user logs to `/tmp/fblogin-$pid.log`; after username known, switch to `$HOME/.fblogin.log` (600, chowned).
- **Forced global**: set `FBLOGIN_LOG_FILE=/var/log/fblogin/fblogin.log` or pass `--log-file=/path`. Remains on that file and never switches. `SIGUSR1` triggers reopen. Requires `FBLOGIN_DEBUG=1` or `--debug` (implied by `--log-file`).

---

## Xauthority/startx notes

Do not export `XAUTHORITY` globally in `~/.zshenv`.

If keeping Xauthority under `XDG_RUNTIME_DIR` **only on tty1**, add to `~/.config/zsh/.zlogin`:

```zsh
if [[ -z "$DISPLAY" && -z "$SSH_TTY" && "$(tty)" == "/dev/tty1" ]]; then
  if [[ -n "$XDG_RUNTIME_DIR" && -O "$XDG_RUNTIME_DIR" ]]; then
    AUTH="${XDG_RUNTIME_DIR}/x11/Xauthority"
    install -d -m 700 -- "${AUTH:h}"
    if [[ ! -L "$HOME/.Xauthority" || "$(readlink -f "$HOME/.Xauthority" 2>/dev/null)" != "$AUTH" ]]; then
      rm -f -- "$HOME/.Xauthority"
      ln -s -- "$AUTH" "$HOME/.Xauthority"
    fi
    # exec startx    # optional
  fi
fi
```

`~/.xinitrc` should not hard-set `XAUTHORITY`; let `xinit/xauth` manage cookies. It can export env to user services if needed.

---

## setup.sh

Automates:
- PAM file write,
- unit install,
- removal of conflicting `getty@tty1` override,
- disabling getty on tty1/tty2,
- enabling `fblogin@tty1`,
- basic verification.

Run with root: `sudo ./setup.sh`.

---

## Man page

Install via `make install-man`, then `man fblogin`.

---

## Security

- Binary is **setuid root** (4755).
- PAM handles credentials; we don’t store them.
- Greeter session isolated; user session opened/closed explicitly.
- Minimal signal surface; blocking calls interrupted safely.

---

## Troubleshooting

- **stdin is not a TTY**: start via `fblogin@tty1.service` on a VT.
- **PAM session failed (TTY busy / inside a session)**: use a free VT; don’t launch inside an existing session unless `--dev`.
- **polkit prompts missing**: ensure DE/WM started from the tty-backed session (`loginctl` should show `TYPE=tty`).
- **fingerprint never matches**: enroll with `fprintd-enroll`, confirm sensor support; disable with `--no-fp` if needed.
- **logs break after rotation**: use the provided logrotate and `SIGUSR1` reopen; ensure forced global logging is configured.
- **no /dev/fb0**: fbdev not exposed; current code targets fbdev, not DRM/KMS.

---

## Contributing / License / Docs

- Contribution guide: `docs/Contributing.md`
- Architecture notes: `docs/ARCHITECTURE.md`
- Changelog: `docs/CHANGELOG.md`
- License: `LICENSE`

