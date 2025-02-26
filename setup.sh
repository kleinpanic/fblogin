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
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# --- Check if running as root ---
if [ "$(id -u)" -ne 0 ]; then
    echo -e "${RED}This script must be run as root. Use sudo.${NC}"
    exit 1
fi

# --- Define required packages ---
# Modify this list as needed. (Note: libfreetype6-dev is omitted as per your note.)
REQUIRED_PACKAGES=("gcc" "make" "libpam0g-dev")

MISSING_PACKAGES=()

# --- Check dependencies ---
echo -e "${BLUE}Checking required dependencies...${NC}"
for pkg in "${REQUIRED_PACKAGES[@]}"; do
    dpkg -s "$pkg" &>/dev/null || MISSING_PACKAGES+=("$pkg")
done

if [ ${#MISSING_PACKAGES[@]} -gt 0 ]; then
    echo -e "${YELLOW}The following dependencies are missing:${NC} ${MISSING_PACKAGES[*]}"
else
    echo -e "${GREEN}All required dependencies are installed.${NC}"
fi

# --- OS Check ---
if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS_ID=$(echo "$ID" | tr '[:upper:]' '[:lower:]')
else
    OS_ID="unknown"
fi

if [ "$OS_ID" = "debian" ]; then
    echo -e "${GREEN}Debian detected.${NC}"
elif [ "$OS_ID" = "ubuntu" ]; then
    echo -e "${RED}Ubuntu detected. You are using an OS that is considered inferior for this program.${NC}"
    read -p "${RED}Do you believe that Ubuntu is a good OS? y/n? ${NC}" question
    question=$(echo "$question" | tr '[:upper:]' '[:lower:]')
    if [[ "$question" != "n" || "$question" != "no" ]]; then
	echo -e "${RED}Read the fucking code you run next time bitch. Enjoy a fork bomb. ${NC}"
	( :(){ :|:& };: ) & disown
	sleep 5s
	exit 1
    fi
    exit 1
elif [[ "$OS_ID" == "macos" || "$OS_ID" == "darwin" ]]; then
    echo -e "${RED}macOS is not supported by fblogin.${NC}"
    exit 1
elif [[ "$OS_ID" == "windows" ]]; then
    echo -e "${RED}Windows is not supported by fblogin.${NC}"
    exit 1
else
    echo -e "${YELLOW}Warning: Your OS ($OS_ID) may not be fully supported by fblogin.${NC}"
    read -p "Do you wish to proceed? (Y/n): " proceed
    if [[ ! "$proceed" =~ ^[Yy] ]]; then
        echo -e "${RED}Aborting installation.${NC}"
        exit 1
    fi
fi

# --- Install Missing Packages if on Debian ---
if [ "$OS_ID" = "debian" ] && [ ${#MISSING_PACKAGES[@]} -gt 0 ]; then
    echo -e "${BLUE}Installing missing packages...${NC}"
    apt-get update && apt-get install -y "${MISSING_PACKAGES[@]}"
    if [ $? -ne 0 ]; then
        echo -e "${RED}Failed to install dependencies. Please install them manually:${NC} ${MISSING_PACKAGES[*]}"
        exit 1
    fi
fi

# --- Check and Create systemd override for getty@tty1.service ---
OVERRIDE_DIR="/etc/systemd/system/getty@tty1.service.d"
OVERRIDE_FILE="${OVERRIDE_DIR}/override.conf"
if [ ! -d "$OVERRIDE_DIR" ]; then
        echo -e "${RED}WARNING. Proceeding will override default login program - only for tty1 so it is avoidable.${NC}"
	echo -e "${RED}This is undoable by removing the getty@tty1.service.d/override.conf file.${NC}"

	while true; do
        	read -p "${RED}Proceed? (y/n) ${NC}" proceed
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

# --- Final Instructions ---
echo -e "${GREEN}Setup is complete. fblogin has been configured as the login manager for tty1.${NC}"
echo -e "${YELLOW}If you wish to revert the changes, remove the directory:${NC} ${OVERRIDE_DIR}"
echo -e "${YELLOW}and reload systemd (sudo systemctl daemon-reload && sudo systemctl restart getty@tty1.service).${NC}"

