#!/usr/bin/env bash
# setup.sh — install & configure fblogin on tty1 with PAM/logind + logging
# Usage: ./setup.sh  (will sudo when needed)   or   sudo ./setup.sh
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ME="${SUDO_USER:-${USER}}"

BOX() {
  local msg="$1"
  local w=${#msg}
  local line
  line="$(printf '═%.0s' $(seq 1 $((w+2))))"
  echo -e "╔${line}╗\n║ ${msg} ║\n╚${line}╝"
}

LOGO() {
  cat <<'ASCII'
'/¯¯¯¯/\¯¯¯¯\·. ''/¯¯¯¯/\¯¯¯¯\·. |¯¯¯¯|`·.'          '/¯¯¯¯/\¯¯¯¯\`·.''/¯¯¯¯/\¯¯¯¯\·.  |¯¯¯¯|`·.'/¯¯¯¯/`·.         
|`·.·´`·|::'¯¯¯¯`·.\|·´`·.·´|:/____/`·.|`·.·´`·|:::'          |·´`·.·´|::|`·.·´`·|:::|`·.·´`·|:::¯¯¯¯`·.\|.·´`·.·|:::|.·´`·.·|\¯¯¯¯\`·. 
|`·.·´`·|\¯¯¯¯\·.¨ '|·´`·.·´|:\¯¯¯¯\·./|`·.·´`·|::|¯¯¯¯|`·.|·´`·.·´|::|`·.·´`·|:::|`·.·´`·|:\¯¯¯¯\·. ¨|.·´`·.·|:::|.·´`·.·|:|`·.·´`·|:::'
|____|::¯¯¯¯`·.\''\____\/____/`·.'\____\/____/`·.''\____\/____/`·.''\____\/____/`·. |____|:::|____|:|____|:::'
'`·.:::::`·.¨ ¨ ¨ ¨ ¨ '  `·.::::::::::::'`·./  `·.::::::::::::'`·./' '`·.:::::::::::::`·./'  `·.::::::::::::'`·./ '`·.:::::`·.'`·.:::::`·`·.::::`·. 
ASCII
}

need_root() {
  if [[ $EUID -ne 0 ]]; then
    echo "Re-running with sudo..."
    exec sudo -E "$0" "$@"
  fi
}

detect_pkgmgr() {
  if command -v apt-get >/dev/null 2>&1; then echo apt; return; fi
  if command -v dnf >/dev/null 2>&1; then echo dnf; return; fi
  if command -v pacman >/dev/null 2>&1; then echo pacman; return; fi
  echo none
}

install_deps() {
  local pm; pm="$(detect_pkgmgr)"
  case "$pm" in
    apt)
      local pkgs=(build-essential pkg-config libpam0g-dev libsystemd-dev)
      BOX "Installing deps via apt: ${pkgs[*]}"
      DEBIAN_FRONTEND=noninteractive apt-get update -y
      DEBIAN_FRONTEND=noninteractive apt-get install -y "${pkgs[@]}"
      ;;
    dnf)
      local pkgs=(gcc make pkgconf pam-devel systemd-devel)
      BOX "Installing deps via dnf: ${pkgs[*]}"
      dnf -y install "${pkgs[@]}"
      ;;
    pacman)
      local pkgs=(base-devel pam systemd pkgconf)
      BOX "Installing deps via pacman: ${pkgs[*]}"
      pacman --noconfirm -S --needed "${pkgs[@]}"
      ;;
    *)
      echo "Package manager not detected—please install build tools, PAM headers, and systemd headers manually."
      ;;
  esac
}

build_as_user() {
  # Build as $ME to keep artifacts owned by developer account
  if [[ -n "${SUDO_USER:-}" ]]; then
    BOX "Building as $ME"
    su - "$ME" -c "cd '$PROJECT_ROOT' && make -j$(nproc)"
  else
    BOX "Building as $USER"
    make -C "$PROJECT_ROOT" -j"$(nproc)"
  fi
}

install_all() {
  BOX "Installing (setuid root + systemd + PAM + issue)"
  make -C "$PROJECT_ROOT" install-all
  systemctl daemon-reload
}

setup_logging() {
  BOX "Configuring /var/log/fblogin and logrotate"
  install -d -m 0755 /var/log/fblogin
  touch /var/log/fblogin/fblogin.log
  chown root:adm /var/log/fblogin/fblogin.log
  chmod 0640 /var/log/fblogin/fblogin.log

  cat >/etc/logrotate.d/fblogin <<'ROT'
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
}

setup_systemd_dropin() {
  BOX "Adding systemd drop-in for debug + log file"
  install -d -m 0755 /etc/systemd/system/fblogin@.service.d
  cat >/etc/systemd/system/fblogin@.service.d/override.conf <<'OVR'
[Service]
Environment=FBLOGIN_DEBUG=1
Environment=FBLOGIN_LOG_FILE=/var/log/fblogin/fblogin.log
OVR
  systemctl daemon-reload
}

disable_gettys() {
  BOX "Disabling conflicting gettys (tty1/tty2)"
  systemctl disable --now getty@tty1.service 2>/dev/null || true
  systemctl disable --now getty@tty2.service 2>/dev/null || true
}

enable_fblogin() {
  BOX "Enabling fblogin@tty1"
  systemctl enable --now fblogin@tty1.service
}

show_status() {
  echo
  BOX "Status"
  systemctl status fblogin@tty1.service --no-pager || true
  echo
  echo "loginctl:"
  loginctl || true
  echo
  [[ -f /etc/issue.fblogin ]] && { BOX "/etc/issue.fblogin"; sed -n '1,80p' /etc/issue.fblogin; }
  echo
  echo "Logs: /var/log/fblogin/fblogin.log"
}

main() {
  LOGO
  need_root "$@"
  install_deps
  build_as_user
  install_all
  setup_logging
  setup_systemd_dropin
  disable_gettys
  enable_fblogin
  show_status
  BOX "Done. Switch to tty1 and test a login. Exiting."
}
main "$@"

