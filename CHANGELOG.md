# Changelog

All notable changes to fblogin will be documented in this file. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [Unreleased]
### Added
- Fingerprint authentication via fprintd with fallback to password-based login.
- Command-line flag `--cmatrix` to enable animated background.
- Command-line flag `--version` to display the current fblogin version.
- Transparent input field outlines and adjusted UI layout.
- Basic support for overriding tty1's default login via systemd.

### Fixed
- Improved handling of special keys (Ctrl-D, Ctrl-C, etc.) for prompt restarting.
- Enhanced error handling in fingerprint authentication flow.

## [1.0.0] - 2025-02-26
### Added
- Initial stable release of fblogin.
- Comprehensive integration of PAM authentication, fprintd biometric verification, and Linux framebuffer UI.
- Detailed documentation and man page.

