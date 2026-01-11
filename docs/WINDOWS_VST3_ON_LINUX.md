# Running Windows VST3 Plugins on Linux

This guide covers setting up Wine-GE and PipeWine to run Windows VST3 plugins on Linux with low latency.

## Overview

| Component | Purpose |
|-----------|---------|
| **Wine-GE** | Enhanced Wine build optimized by GloriousEggroll (Valve contractor) |
| **PipeWine** | ASIO driver bridging Wine audio to PipeWire |
| **ProtonUp-Qt** | Easy installer for Wine-GE/Proton-GE |

Expected latency: **3-11ms** (professional quality)

## Quick Install (Ubuntu 22.04+, Debian 12+)

### Step 1: Install Dependencies

```bash
# Core dependencies
sudo apt update
sudo apt install -y \
    wine64 \
    wine32 \
    libwine \
    winetricks \
    pipewire \
    pipewire-audio \
    libpipewire-0.3-dev \
    wine-dev \
    build-essential \
    git \
    flatpak

# Realtime audio permissions
sudo usermod -aG audio,pipewire $USER

# Create realtime group if it doesn't exist
sudo groupadd -f realtime
sudo usermod -aG realtime $USER

# Configure realtime limits
echo '@realtime - rtprio 99
@realtime - memlock unlimited' | sudo tee /etc/security/limits.d/99-realtime.conf
```

### Step 2: Install Wine-GE via ProtonUp-Qt

```bash
# Install ProtonUp-Qt (Flatpak - easiest)
flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak install -y flathub net.davidotek.pupgui2

# Run it
flatpak run net.davidotek.pupgui2

# In ProtonUp-Qt GUI:
# 1. Select "Lutris" from dropdown (or "Steam" for Steam games)
# 2. Click "Add version"
# 3. Select "Wine-GE" and latest version
# 4. Click "Install"
```

### Step 3: Install PipeWine (Low-Latency Audio)

```bash
# Clone and build PipeWine
cd /tmp
git clone https://github.com/alex190291/pipewine.git
cd pipewine
make 64

# Install (copies to Wine directory)
sudo ./install-pipewine.sh

# Verify installation
ls -la ~/.wine/drive_c/windows/system32/wineasio.dll 2>/dev/null || echo "Check install path"
```

### Step 4: Configure Wine Prefix for VST3

```bash
# Create dedicated Wine prefix for VST plugins
export WINEPREFIX=~/.wine-vst3
export WINEARCH=win64

# Initialize prefix
wineboot --init

# Install Visual C++ runtime (required by most plugins)
winetricks -q vcrun2019

# Create VST3 directory
mkdir -p "$WINEPREFIX/drive_c/Program Files/Common Files/VST3"

# Register PipeWine ASIO driver in this prefix
cd /tmp/pipewine
wine64 regsvr32 wineasio.dll
```

### Step 5: Install Windows VST3 Plugins

```bash
# Copy your Windows .vst3 files/folders to:
cp -r /path/to/plugin.vst3 "$WINEPREFIX/drive_c/Program Files/Common Files/VST3/"

# Or run Windows installers:
wine64 /path/to/plugin-installer.exe
```

## One-Line Automated Setup

```bash
curl -fsSL https://raw.githubusercontent.com/ekg/rack/main/scripts/setup-wine-vst3.sh | bash
```

## Full Automated Setup Script

Save as `scripts/setup-wine-vst3.sh`:

```bash
#!/bin/bash
# Wine-GE + PipeWine Setup for Windows VST3 on Linux
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
else
    log_error "Cannot detect distribution"
    exit 1
fi

log_info "Detected: $DISTRO"

# Install dependencies based on distro
log_info "[1/7] Installing dependencies..."
case $DISTRO in
    ubuntu|debian|linuxmint|pop)
        sudo apt update
        sudo apt install -y wine64 wine32 winetricks pipewire \
            libpipewire-0.3-dev wine-dev build-essential git flatpak
        ;;
    fedora)
        sudo dnf install -y wine winetricks pipewire-devel wine-devel \
            @development-tools git flatpak
        ;;
    arch|manjaro)
        sudo pacman -Sy --noconfirm wine winetricks pipewire lib32-pipewire \
            base-devel git flatpak
        ;;
    *)
        log_warn "Unknown distro. Install manually: wine winetricks pipewire git"
        ;;
esac

# Setup realtime permissions
log_info "[2/7] Configuring realtime audio permissions..."
sudo usermod -aG audio $USER 2>/dev/null || true
sudo usermod -aG pipewire $USER 2>/dev/null || true
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

# Initialize Wine prefix (suppress GUI)
DISPLAY="" wineboot --init 2>/dev/null || wineboot --init

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
echo "To run ProtonUp-Qt (install Wine-GE):"
echo "  flatpak run net.davidotek.pupgui2"
echo ""
```

## Testing

### Verify PipeWire Audio

```bash
# Check PipeWire is running
pw-cli info

# Check buffer size (quantum)
pw-metadata -n settings

# For low latency, set quantum to 256 or 128:
pw-metadata -n settings 0 clock.force-quantum 256
```

### List Installed VST3s

```bash
# Show Windows VST3 plugins
ls -la ~/.wine-vst3/drive_c/Program\ Files/Common\ Files/VST3/
```

## Troubleshooting

### Audio Crackling/Dropouts

```bash
# Increase PipeWire buffer
pw-metadata -n settings 0 clock.force-quantum 512

# Check for xruns
journalctl --user -u pipewire -f
```

### Plugin GUI Issues (Direct2D)

Some plugins have GUI rendering issues. Try:
```bash
winetricks renderer=gdi
```

### Wine Version Issues

Use ProtonUp-Qt to install Wine-GE, which has better compatibility:
```bash
flatpak run net.davidotek.pupgui2
```

## Integration with Rack

Rack can load Windows VST3 plugins from the Wine prefix. Set environment:

```bash
export WINEPREFIX=~/.wine-vst3
export VST3_PATH="$WINEPREFIX/drive_c/Program Files/Common Files/VST3:$VST3_PATH"
```

## Resources

- [Wine-GE Releases](https://github.com/GloriousEggroll/wine-ge-custom/releases)
- [Proton-GE Releases](https://github.com/GloriousEggroll/proton-ge-custom/releases)
- [ProtonUp-Qt](https://github.com/DavidoTek/ProtonUp-Qt)
- [PipeWine](https://github.com/alex190291/pipewine)
- [WineASIO](https://github.com/wineasio/wineasio) (for JACK instead of PipeWire)
