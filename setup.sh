#!/bin/bash
# setup_fblogin.sh
# This script sets up fblogin by ensuring that all required dependencies,
# system files, and configuration overrides are in place.
#
# MUST be run as root (sudo)
#
# Author: (your name omitted)
# Date: (current date)
# Usage: sudo ./setup_fblogin.sh

# ANSI color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[1;34m'
NC='\033[0m' # No Color

# --- Check if running as root ---
if [ "$(id -u)" -ne 0 ]; then
    echo -e "${RED}This script must be run as root. Use sudo.${NC}"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# --- OS Check ---
if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS_ID=$(echo "$ID" | tr '[:upper:]' '[:lower:]')
else
    OS_ID="unknown"
fi

LOGO_FILE="fblogin-logo.txt"

echo -e "${YELLOW}Your OS ($OS_ID).${NC}"

# Define a function to handle OS-specific logic
handle_os() {
    local os_id=$1
    case $os_id in
        debian|ubuntu|raspbian|linuxmint|pop)
            echo -e "${GREEN}${os_id^} detected.${NC}"
            LOGO_FILE="fblogin-${os_id}-logo.txt"
            ;;
        arch|archlinux|manjaro)
            echo -e "${GREEN}Arch Linux detected.${NC}"
            LOGO_FILE="fblogin-arch-logo.txt"
            ;;
        fedora|centos|opensuse|alpine|gentoo|slackware|void|nixos|clearlinux)
            echo -e "${GREEN}${os_id^} detected.${NC}"
            LOGO_FILE="fblogin-${os_id}-logo.txt"
            ;;
        macos|darwin)
            echo -e "${RED}macOS is not supported by fblogin.${NC}"
            exit 1
            ;;
        windows)
            echo -e "${RED}Windows is not supported by fblogin.${NC}"
            exit 1
            ;;
        *)
            echo -e "${YELLOW}Warning: Your OS ($OS_ID) may not be fully supported by fblogin.${NC}"
            echo -e -n "Do you wish to proceed? (Y/n): " 
            read proceed
            if [[ ! "$proceed" =~ ^[Yy] ]]; then
                echo -e "${RED}Aborting installation.${NC}"
                exit 1
            fi
            ;;
    esac
}

# Call the function with the detected OS ID
handle_os "$OS_ID"

# Check if the logo file exists
if [ ! -f "$SCRIPT_DIR/etc/fblogini/$LOGO_FILE" ]; then
    echo -e "${RED}Logo file $LOGO_FILE not found.${NC}"
    exit 1
fi

# --- Detect Package Manager ---
if command -v apt-get &>/dev/null; then
    PKG_MANAGER="apt-get"
    UPDATE_CMD="apt-get update"
    INSTALL_CMD="apt-get install -y"
elif command -v dnf &>/dev/null; then
    PKG_MANAGER="dnf"
    UPDATE_CMD="dnf check-update"
    INSTALL_CMD="dnf install -y"
elif command -v yum &>/dev/null; then
    PKG_MANAGER="yum"
    UPDATE_CMD="yum check-update"
    INSTALL_CMD="yum install -y"
elif command -v zypper &>/dev/null; then
    PKG_MANAGER="zypper"
    UPDATE_CMD="zypper refresh"
    INSTALL_CMD="zypper install -y"
elif command -v apk &>/dev/null; then
    PKG_MANAGER="apk"
    UPDATE_CMD="apk update"
    INSTALL_CMD="apk add"
elif command -v pacman &>/dev/null; then
    PKG_MANAGER="pacman"
    UPDATE_CMD="pacman -Sy"
    INSTALL_CMD="pacman -S --noconfirm"
elif command -v xbps-install &>/dev/null; then
    PKG_MANAGER="xbps-install"
    UPDATE_CMD="xbps-install -S"
    INSTALL_CMD="xbps-install -y"
elif command -v emerge &>/dev/null; then
    PKG_MANAGER="emerge"
    UPDATE_CMD=""
    INSTALL_CMD="emerge"
elif command -v nix-env &>/dev/null; then
    PKG_MANAGER="nix-env"
    UPDATE_CMD=""
    INSTALL_CMD="nix-env -i"
elif command -v swupd &>/dev/null; then
    PKG_MANAGER="swupd"
    UPDATE_CMD="swupd update"
    INSTALL_CMD="swupd bundle-add"
else
    echo -e "${RED}Unsupported package manager. Install dependencies manually.${NC}"
    exit 1
fi

# --- Define Required Packages Based on OS ---
case "$OS_ID" in
    debian|ubuntu|raspbian|linuxmint|pop)
        REQUIRED_PACKAGES=("gcc" "make" "libpam0g-dev")
        ;;
    arch|manjaro|archlinux)
        REQUIRED_PACKAGES=("gcc" "make" "pam")  # `pam` already includes headers
        ;;
    fedora|centos)
        REQUIRED_PACKAGES=("gcc" "make" "pam-devel")
        ;;
    opensuse)
        REQUIRED_PACKAGES=("gcc" "make" "pam-devel")
        ;;
    alpine)
        REQUIRED_PACKAGES=("gcc" "make" "linux-pam-dev")
        ;;
    gentoo)
        REQUIRED_PACKAGES=("gcc" "make" "sys-libs/pam")
        ;;
    void)
        REQUIRED_PACKAGES=("gcc" "make" "pam-devel")
        ;;
    slackware)
        REQUIRED_PACKAGES=("gcc" "make" "pam")  # Might be in `aaa_elflibs`
        ;;
    nixos)
        REQUIRED_PACKAGES=("gcc" "make" "pam")
        ;;
    clearlinux)
        REQUIRED_PACKAGES=("gcc" "make" "libpam-dev")
        ;;
    *)
        echo -e "${YELLOW}Unknown or unsupported OS ($OS_ID). Proceeding with generic package names.${NC}"
        REQUIRED_PACKAGES=("gcc" "make" "pam")  # Safe default
        ;;
esac

# --- Check for Missing Packages ---
MISSING_PACKAGES=()
echo -e "${BLUE}Checking required dependencies...${NC}"
for pkg in "${REQUIRED_PACKAGES[@]}"; do
    if ! command -v "$pkg" &>/dev/null && ! dpkg -s "$pkg" &>/dev/null 2>/dev/null; then
        MISSING_PACKAGES+=("$pkg")
    fi
done

if [ ${#MISSING_PACKAGES[@]} -gt 0 ]; then
    echo -e "${YELLOW}The following dependencies are missing:${NC} ${MISSING_PACKAGES[*]}${NC}"
    
    # --- Install Missing Packages ---
    echo -e -n "Do you want to install them now? (Y/n): " 
    read install_now
    install_now=${install_now:-Y} # Default to Yes

    if [[ "$install_now" =~ ^[Yy]$ ]]; then
        echo -e "${BLUE}Installing missing packages...${NC}"
        $UPDATE_CMD
        $INSTALL_CMD "${MISSING_PACKAGES[@]}"
        if [ $? -ne 0 ]; then
            echo -e "${RED}Failed to install dependencies. Please install them manually:${NC} ${MISSING_PACKAGES[*]}"
            exit 1
        fi
    else
        echo -e "${RED}Skipping dependency installation. Some features may not work.${NC}"
    fi
else
    echo -e "${GREEN}All required dependencies are installed.${NC}"
fi

echo -e "${GREEN}Dependency check completed.${NC}"
echo -e "${BLUE}Setting up fblogin...${NC}"

# --- Ensure /etc/fblogin directory exists ---
LOGO_DIR="/etc/fblogin"
SOURCE_LOGO_FILE="${SCRIPT_DIR}/etc/fblogin/${LOGO_FILE}"
DEST_LOGO_FILE="$LOGO_DIR/$LOGO_FILE"

echo -e "${BLUE}Setting up fblogin logo file...${NC}"

# Create the directory if it doesn't exist
if [ ! -d "$LOGO_DIR" ]; then
    echo -e "${YELLOW}Creating $LOGO_DIR...${NC}"
    mkdir -p "$LOGO_DIR"
fi

# Copy the logo file
if [ -f "$SOURCE_LOGO_FILE" ]; then
    cp "$SOURCE_LOGO_FILE" "$DEST_LOGO_FILE"
    chmod 644 "$DEST_LOGO_FILE"
    chown root:root "$DEST_LOGO_FILE"
    echo -e "${GREEN}Copied fblogin logo to $DEST_LOGO_FILE and set correct permissions.${NC}"
else
    echo -e "${RED}Warning: $LOGO_FILE not found in $SOURCE_LOGO_FILE! Skipping copy.${NC}"
fi

# --- Check and Create systemd override for getty@tty1.service ---
OVERRIDE_DIR="/etc/systemd/system/getty@tty1.service.d"
OVERRIDE_FILE="${OVERRIDE_DIR}/override.conf"
if [ ! -d "$OVERRIDE_DIR" ]; then
        echo -e "${RED}WARNING. Proceeding will override default login program - only for tty1 so it is avoidable.${NC}"
	echo -e "${RED}This is undoable by removing the getty@tty1.service.d/override.conf file.${NC}"

	while true; do
        echo -e -n "Proceed? (y/n) " 
		read proceed
        	proceed=$(echo "$proceed" | tr '[:upper:]' '[:lower:]')  # Convert to lowercase

        	if [[ "$proceed" == "y" || "$proceed" == "yes" ]]; then
            		echo -e "${BLUE}Creating directory for systemd override: ${OVERRIDE_DIR}${NC}"
            		mkdir -p "$OVERRIDE_DIR" || { echo -e "${RED}Failed to create directory.${NC}"; exit 1; }
            		break  # Exit loop and proceed
        	elif [[ "$proceed" == "n" || "$proceed" == "no" ]]; then
            		echo -e "${BLUE}Exiting. Do it manually or test it later.${NC}"
            		exit 0  # Stop script
        	else
            		echo -e "${RED}Stop fucking around and put in a valid answer (y/n).${NC}"
        	fi
    	done
fi

if [ ! -f "$OVERRIDE_FILE" ]; then
    echo -e "${BLUE}Creating systemd override file at ${OVERRIDE_FILE}${NC}"
    cat <<EOF > "$OVERRIDE_FILE"
[Service]
# Clear the default ExecStart setting.
ExecStart=
# Set your custom login command.
ExecStart=-/usr/local/bin/fblogin --cmatrix
Type=simple
EOF
    if [ $? -ne 0 ]; then
        echo -e "${RED}Failed to create override file.${NC}"
        exit 1
    fi
else
    echo -e "${GREEN}Systemd override file already exists at ${OVERRIDE_FILE}.${NC}"
fi

# --- Check and Create PAM configuration file for fblogin ---
PAM_FILE="/etc/pam.d/fblogin"
if [ ! -f "$PAM_FILE" ]; then
    echo -e "${BLUE}Creating PAM configuration file for fblogin at ${PAM_FILE}${NC}"
    cat <<EOF > "$PAM_FILE"
# Minimal PAM configuration for fblogin
# This configuration uses only the essential modules and the "nodelay" option
# to disable the artificial delay that pam_unix may introduce on failures.

# Authentication: Use pam_unix with no delay
auth    required   pam_unix.so nullok nodelay

# Account management: Basic Unix account checks
account required   pam_unix.so

# Session management: Basic session setup
session required   pam_unix.so
EOF
    if [ $? -ne 0 ]; then
        echo -e "${RED}Failed to create PAM configuration file.${NC}"
        exit 1
    fi
else
    echo -e "${GREEN}PAM configuration file for fblogin already exists at ${PAM_FILE}.${NC}"
fi

# --- Check if fblogin binary is installed in /usr/local/bin ---
if [ ! -x "/usr/local/bin/fblogin" ]; then
    echo -e "${RED}fblogin binary not found at /usr/local/bin/fblogin.${NC}"
    echo -e "${YELLOW}Please run 'make install' in the project directory to install fblogin properly.${NC}"
    exit 1
else
    echo -e "${GREEN}fblogin binary found at /usr/local/bin/fblogin.${NC}"
fi

# Where you want the man page installed
MAN_INSTALL_DIR="/usr/local/share/man/man1"

# Create the target directory if it does not exist
if [ ! -d "$MAN_INSTALL_DIR" ]; then
  echo "Creating directory $MAN_INSTALL_DIR..."
  mkdir -p "$MAN_INSTALL_DIR"
fi

# Copy or install the fblogin.1 man page into the man1 directory
echo "Installing fblogin.1 from ${SCRIPT_DIR} to ${MAN_INSTALL_DIR}..."

# Option 2 (preferred): Using install for proper permissions (e.g. 644)
install -m 644 "${SCRIPT_DIR}/fblogin.1" "$MAN_INSTALL_DIR"

echo "Installation complete!"


# --- Final Instructions ---
echo -e "${GREEN}Setup is complete. fblogin has been configured as the login manager for tty1.${NC}"
echo -e "${YELLOW}If you wish to revert the changes, remove the directory:${NC} ${OVERRIDE_DIR}"
echo -e "${YELLOW}and reload systemd (sudo systemctl daemon-reload && sudo systemctl restart getty@tty1.service).${NC}"

