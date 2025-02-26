# fblogin: Framebuffer-Based Graphical Login for Debian

## **Introduction**

fblogin is a **lightweight, framebuffer-based graphical login manager** designed for Debian systems. It replaces the standard tty1 login prompt with a visually enhanced interface utilizing the **Linux framebuffer** for graphics, **PAM** for authentication, and optional **fingerprint verification via fprintd**. The system supports:

- **Direct framebuffer rendering** for a graphical login screen without X11/Wayland.
- **Pluggable Authentication Modules (PAM)** for authentication.
- **Fingerprint authentication** using `fprintd`.
- **Raw terminal input processing** for an interactive TUI-style experience.
- **Minimal system overhead**, replacing `getty` via `systemd` overrides.

This document provides an exhaustive technical breakdown of `fblogin`, covering **system dependencies, low-level memory operations, device interactions, authentication flow, and graphical rendering.**

## Note
> If you use ubuntu run at ur own risk.

---

## **System Dependencies**

### **1. Required Header Files**

`fblogin` requires both **standard C libraries** and **Linux-specific headers** for framebuffer and authentication control.

#### **Standard Libraries:**
- `<stddef.h>` – Defines `size_t` and other fundamental types.
- `<stdint.h>` – Fixed-width integer types.
- `<stdio.h>` – Input/output functions.
- `<stdlib.h>` – Memory allocation, system functions.
- `<string.h>` – String handling utilities.
- `<time.h>` – Timing functions.

#### **System-Level Libraries:**
- `<unistd.h>` – Low-level system calls.
- `<sys/ioctl.h>` – Device communication.
- `<sys/mman.h>` – Memory mapping (`mmap`).
- `<sys/types.h>` – Defines `pid_t`, `uid_t`, etc.
- `<sys/wait.h>` – Process synchronization (`waitpid`).

#### **Framebuffer Graphics:**
- `<linux/fb.h>` – Framebuffer device control.
- `"fb.h"` – Custom framebuffer manipulation.
- `"font8x8_basic.h"` – Bitmap font rendering.

#### **Authentication and User Management:**
- `<security/pam_appl.h>` – PAM API for authentication.
- `"pam_auth.h"` – Custom PAM authentication handler.
- `<pwd.h>` – User account management.
- `<grp.h>` – Group management.

#### **Terminal Input Handling:**
- `<termios.h>` – Raw keyboard input control.
- `"input.h"` – Custom keyboard processing.

#### **Process and Signal Management:**
- `<signal.h>` – Signal handling.
- `"ui.h"` – User interface logic.

### **2. System Calls Used**

The core functionality of `fblogin` relies on direct **syscalls** for process control, user authentication, and framebuffer manipulation.

#### **File and Memory Operations:**
- `open()` – Opens `/dev/fb0`.
- `read()` – Reads user input.
- `close()` – Closes descriptors.
- `mmap()` – Maps framebuffer memory.
- `munmap()` – Unmaps memory.

#### **Process Control:**
- `fork()` – Creates login subprocess.
- `waitpid()` – Waits for completion.
- `setsid()` – Creates a new session (daemon-like behavior).

#### **User and Permission Management:**
- `setuid()` – Changes user ID.
- `setgid()` – Changes group ID.
- `getuid()` – Retrieves user ID.

#### **Framebuffer Operations:**
- `ioctl()` – Direct framebuffer control.

#### **Authentication & Fingerprint Handling:**
- `pam_*` – PAM API functions.
- `fprintd-verify` – Fingerprint authentication via `fprintd`.

---

## **System Architecture**

### **1. Authentication Flow (PAM + fprintd)**

1. The program **initializes PAM** with `pam_start("fblogin", username, &conv, &pamh)`.
2. If **fingerprint authentication is enabled**, `fprintd-list` checks for stored fingerprints.
3. If **a fingerprint is found**, `fprintd-verify` is called and its output determines success.
4. If **fingerprint authentication fails**, the system **falls back to password login**.
5. Upon successful authentication, `pam_acct_mgmt()` verifies **account validity**.
6. If approved, `pam_open_session()` starts a session and `execv()` spawns the user's shell.

### **2. Framebuffer Rendering Pipeline**

1. The framebuffer device `/dev/fb0` is **opened with `open()`**.
2. `ioctl(FBIOGET_VSCREENINFO)` retrieves **display dimensions**.
3. `mmap()` maps framebuffer memory for **direct pixel access**.
4. The screen is **cleared to black** using `memset()`.
5. An **8×8 bitmap font** is rendered with `font8x8_basic.h` and **scaling factors**.
6. The **Debian logo (PFP) is drawn at the center** using per-pixel transformations.
7. The **cmatrix animation (if enabled) updates the background** dynamically.

### **3. Terminal Input Handling**

1. `termios` switches tty **to raw mode** (`ECHO` and `ICANON` disabled).
2. Keypresses are read **one character at a time** using `read()`.
3. **Backspace is handled manually** by clearing screen regions.
4. On `Enter`, the input is **validated and passed to PAM**.
5. If **Ctrl+C is pressed**, `fblogin` exits immediately.

### **4. System Integration (Replacing getty via systemd)**

1. `fblogin` replaces `getty` using a **systemd override**:
   ```ini
   [Service]
   ExecStart=/usr/local/bin/fblogin
   ```
2. `setsid()` ensures `fblogin` runs as a **session leader**.
3. After login, `execv()` **spawns the user shell** (e.g., `/bin/bash`).

---

## **Security Considerations**

- **Direct framebuffer access** is only possible for root; `fblogin` runs with **setuid-root**.
- **PAM sessions are properly closed** via `pam_close_session()`.
- **No passwords are stored**; authentication is handled via PAM modules.
- **SIGINT/SIGTERM handling** ensures graceful exit.

---

## **Future Improvements**

- **Dynamic resolution scaling** (currently, framebuffer dimensions are fixed at launch).
- **DRM/KMS support** to replace **legacy fbdev**.
- **Enhanced cmatrix animations** with smoother transitions.

---

## **Conclusion**

`fblogin` is a **high-performance framebuffer-based login manager** that integrates with PAM authentication and fingerprint recognition. It is a lightweight alternative to graphical login managers, optimized for minimalism and direct hardware interaction.


