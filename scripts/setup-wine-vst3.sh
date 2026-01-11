#!/bin/bash
# Wine-GE + PipeWine Setup for Windows VST3 on Linux
# Target: Ubuntu 24.04+ (PipeWire default)
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
echo " Wine-GE + PipeWine Setup for VST3"
echo " Target: Ubuntu 24.04+"
echo "==========================================="
echo ""

# Check not root
if [ "$EUID" -eq 0 ]; then
    log_error "Don't run as root. Script will use sudo when needed."
    exit 1
fi

# Detect distro and version
if [ -f /etc/os-release ]; then
    . /etc/os-release
    DISTRO=$ID
    VERSION=$VERSION_ID
else
    log_error "Cannot detect distribution"
    exit 1
fi

log_info "Detected: $DISTRO $VERSION"

# Check for Ubuntu 24.04+
if [ "$DISTRO" = "ubuntu" ]; then
    MAJOR_VERSION=$(echo "$VERSION" | cut -d. -f1)
    if [ "$MAJOR_VERSION" -lt 24 ]; then
        log_warn "Ubuntu $VERSION detected. This script is optimized for 24.04+"
        log_warn "Older versions may need additional PipeWire setup."
    fi
fi

# Install dependencies based on distro
log_info "[1/7] Installing dependencies..."
case $DISTRO in
    ubuntu|pop)
        # Ubuntu 24.04+ has PipeWire by default
        sudo apt update
        sudo apt install -y \
            wine64 \
            wine32 \
            winetricks \
            libpipewire-0.3-dev \
            build-essential \
            git \
            flatpak
        ;;
    debian)
        sudo apt update
        sudo apt install -y \
            wine64 \
            wine32 \
            winetricks \
            pipewire \
            libpipewire-0.3-dev \
            build-essential \
            git \
            flatpak
        ;;
    fedora)
        sudo dnf install -y \
            wine \
            winetricks \
            pipewire-devel \
            @development-tools \
            git \
            flatpak
        ;;
    arch|manjaro)
        sudo pacman -Sy --noconfirm \
            wine \
            winetricks \
            pipewire \
            lib32-pipewire \
            base-devel \
            git \
            flatpak
        ;;
    *)
        log_warn "Unknown distro '$DISTRO'. Install manually: wine winetricks libpipewire-dev git"
        ;;
esac

# Setup realtime permissions
log_info "[2/7] Configuring realtime audio permissions..."
sudo usermod -aG audio $USER 2>/dev/null || true

# Optional realtime group for lower latency
sudo groupadd -f realtime 2>/dev/null || true
sudo usermod -aG realtime $USER 2>/dev/null || true

if [ ! -f /etc/security/limits.d/99-realtime.conf ]; then
    echo '@realtime - rtprio 99
@realtime - memlock unlimited' | sudo tee /etc/security/limits.d/99-realtime.conf
fi

# Install Flatpak and ProtonUp-Qt
log_info "[3/7] Installing ProtonUp-Qt..."
flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo 2>/dev/null || true
flatpak install -y flathub net.davidotek.pupgui2 2>/dev/null || log_warn "ProtonUp-Qt may already be installed"

# Build and install PipeWine
log_info "[4/7] Building PipeWine..."
PIPEWINE_DIR="/tmp/pipewine-$$"
git clone https://github.com/alex190291/pipewine.git "$PIPEWINE_DIR"
cd "$PIPEWINE_DIR"
make 64

log_info "[5/7] Installing PipeWine..."
sudo ./install-pipewine.sh || log_warn "PipeWine install script had issues"

# Setup Wine prefix
log_info "[6/7] Creating Wine prefix..."
export WINEPREFIX=~/.wine-vst3
export WINEARCH=win64

# Initialize Wine prefix (suppress GUI if no display)
if [ -z "$DISPLAY" ]; then
    wineboot --init 2>/dev/null || true
else
    wineboot --init
fi

# Install VC++ runtime
log_info "[7/7] Installing Visual C++ runtime..."
winetricks -q vcrun2019 2>/dev/null || log_warn "vcrun2019 may need manual install"

# Create VST3 directory
mkdir -p "$WINEPREFIX/drive_c/Program Files/Common Files/VST3"

# Register ASIO driver
cd "$PIPEWINE_DIR"
wine64 regsvr32 wineasio.dll 2>/dev/null || log_warn "ASIO registration may need manual step"

# Cleanup
rm -rf "$PIPEWINE_DIR"

echo ""
echo "==========================================="
echo -e "${GREEN} Setup Complete!${NC}"
echo "==========================================="
echo ""
echo "IMPORTANT: Log out and back in for group changes!"
echo ""
echo "Wine prefix:  ~/.wine-vst3"
echo "VST3 path:    ~/.wine-vst3/drive_c/Program Files/Common Files/VST3/"
echo ""
echo "To install Windows VST3 plugins:"
echo "  cp -r /path/to/plugin.vst3 ~/.wine-vst3/drive_c/Program\\ Files/Common\\ Files/VST3/"
echo ""
echo "To install Wine-GE (recommended for better compatibility):"
echo "  flatpak run net.davidotek.pupgui2"
echo "  -> Select 'Lutris' -> Add version -> Wine-GE -> Install"
echo ""
