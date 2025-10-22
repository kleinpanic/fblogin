# Changelog

All notable changes to fblogin will be documented in this file. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [Unreleased] - 2025-02-26
### Added
- **Core fblogin Architecture:**  
  - Integration of PAM-based authentication with a custom conversation function.
  - fprintd fingerprint authentication support with fallback to traditional password input.
  - Linux framebuffer-based UI rendering with direct pixel manipulation, including an 8×8 bitmap font scaled for improved readability.
  - UI elements such as a Debian spiral (PFP) graphic, outlined (transparent) username and password input boxes, and an optional animated cmatrix background.
- **TTY Enforcement:**  
  - Strict enforcement that fblogin only runs on `/dev/tty1` via a runtime check (using `ttyname(STDIN_FILENO)`) to prevent accidental use on other terminals.
- **Privilege Transition & TTY Permissions:**  
  - Code reordering to adjust tty ownership and permissions (using `chown` and `chmod`) while still running as root, ensuring that subsequent X server startup (e.g. via startx) works correctly.
- **Shell Invocation:**  
  - Improved method for launching the user’s shell as a proper login shell by passing the `--login` flag to ensure that user profiles (e.g., `~/.bash_profile` or `~/.zprofile`) are sourced.
- **System Integration:**  
  - Creation of a minimal PAM configuration file for fblogin (located in `/etc/pam.d/fblogin`).
  - A systemd drop-in override for `getty@tty1.service` to replace the default login process with fblogin.
- **Bash Setup Script:**  
  - A comprehensive setup script (`setup_fblogin.sh`) that checks for required dependencies (installing any missing packages on Debian), performs an OS check (with custom messages for Ubuntu, macOS, Windows, and other Linux distros), verifies that required configuration files exist (creating the systemd override and PAM config if needed), and checks for the installation of the fblogin binary.
  - The script uses colorized output for better user feedback and includes safety checks and interactive prompts for non-Debian systems.
  
### Changed
- **UI Layout Adjustments:**  
  - Lowered the position of text input boxes and ensured that the Debian spiral remains visible during fingerprint reading and welcome screens.
- **Error Handling Improvements:**  
  - Debug output from fingerprint authentication and welcome messages has been commented out to avoid cluttering the on-screen display.
- **Performance & Security:**  
  - Improved ordering of operations so that tty permissions are updated before dropping root privileges, thereby preventing X server permission errors.
  
### Fixed
- **TTY Detection:**  
  - Corrected tty detection logic to ensure that fblogin only executes on `/dev/tty1` (with proper messaging if run on any other terminal).
- **X Server Permissions:**  
  - Resolved issues where startx failed due to incorrect tty ownership and permissions by moving chown/chmod operations before privilege dropping.

## [1.0.0] - 2025-02-25
### Added
- Initial release of fblogin featuring:
  - Basic PAM authentication integration.
  - Linux framebuffer rendering for a minimalistic login UI.
  - Fingerprint authentication via fprintd with fallback to password input.
  - Fundamental privilege and session management to switch from root to the authenticated user.

