#!/bin/bash
# Yabridge Setup for Windows VST3 on Linux
# Target: Ubuntu 24.04+
# https://github.com/ekg/rack
set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

echo "==========================================="
echo " Yabridge Setup for Windows VST3"
echo " Target: Ubuntu 24.04+"
echo "==========================================="
echo ""

# Check not root
if [ "$EUID" -eq 0 ]; then
    log_error "Don't run as root. Script will use sudo when needed."
    exit 1
fi

# Detect distro
if [ -f /etc/os-release ]; then
    . /etc/os-release
    DISTRO=$ID
    VERSION=$VERSION_ID
else
    log_error "Cannot detect distribution"
    exit 1
fi

log_info "Detected: $DISTRO $VERSION"

# Step 1: Install Wine
log_info "[1/4] Installing Wine..."
case $DISTRO in
    ubuntu|pop)
        sudo dpkg --add-architecture i386
        sudo apt update
        sudo apt install -y wine wine64 winetricks
        ;;
    debian)
        sudo dpkg --add-architecture i386
        sudo apt update
        sudo apt install -y wine wine64 winetricks
        ;;
    fedora)
        sudo dnf install -y wine winetricks
        ;;
    arch|manjaro)
        sudo pacman -Sy --noconfirm wine winetricks
        ;;
    *)
        log_warn "Unknown distro. Install wine and winetricks manually."
        ;;
esac

# Step 2: Install yabridge
log_info "[2/4] Installing yabridge..."
YABRIDGE_VERSION="5.1.1"
YABRIDGE_URL="https://github.com/robbert-vdh/yabridge/releases/download/${YABRIDGE_VERSION}/yabridge-${YABRIDGE_VERSION}.tar.gz"

mkdir -p ~/.local/share/yabridge
cd /tmp
curl -LO "$YABRIDGE_URL"
tar -xzf "yabridge-${YABRIDGE_VERSION}.tar.gz" -C ~/.local/share/yabridge --strip-components=1
rm "yabridge-${YABRIDGE_VERSION}.tar.gz"

# Add to PATH
if ! grep -q 'yabridge' ~/.bashrc 2>/dev/null; then
    echo 'export PATH="$HOME/.local/share/yabridge:$PATH"' >> ~/.bashrc
fi
export PATH="$HOME/.local/share/yabridge:$PATH"

# Step 3: Setup Wine prefix
log_info "[3/4] Setting up Wine prefix..."
export WINEPREFIX=~/.wine-vst3
export WINEARCH=win64

wineboot --init 2>/dev/null || wineboot --init
winetricks -q vcrun2019 2>/dev/null || log_warn "vcrun2019 may need manual install"

# Create VST3 directory
mkdir -p "$WINEPREFIX/drive_c/Program Files/Common Files/VST3"

# Step 4: Configure yabridge
log_info "[4/4] Configuring yabridge..."
yabridgectl add "$WINEPREFIX/drive_c/Program Files/Common Files/VST3"
yabridgectl sync

echo ""
echo "==========================================="
echo -e "${GREEN} Setup Complete!${NC}"
echo "==========================================="
echo ""
echo "Wine prefix:  ~/.wine-vst3"
echo "VST3 path:    ~/.wine-vst3/drive_c/Program Files/Common Files/VST3/"
echo ""
echo "To install Windows VST3 plugins:"
echo "  1. Copy plugin: cp -r plugin.vst3 ~/.wine-vst3/drive_c/Program\\ Files/Common\\ Files/VST3/"
echo "  2. Sync yabridge: yabridgectl sync"
echo ""
echo "Yabridge creates Linux wrappers in: ~/.vst3/yabridge/"
echo "These can be loaded by any Linux DAW or rack."
echo ""
