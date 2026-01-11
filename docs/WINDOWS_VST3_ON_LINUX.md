# Running Windows VST3 Plugins on Linux

This guide covers setting up Wine-GE and PipeWine to run Windows VST3 plugins on Linux with low latency.

**Target:** Ubuntu 24.04+ (PipeWire is default, best integration)

## Overview

| Component | Purpose |
|-----------|---------|
| **Wine-GE** | Enhanced Wine build optimized by GloriousEggroll (Valve contractor) |
| **PipeWine** | ASIO driver bridging Wine audio to PipeWire |
| **ProtonUp-Qt** | Easy installer for Wine-GE/Proton-GE |

Expected latency: **3-11ms** (professional quality)

## Quick Install (Ubuntu 24.04+)

### Step 1: Install Dependencies

Ubuntu 24.04 ships with PipeWire as the default audio system.

```bash
# Enable 32-bit architecture (needed for some VST3 plugins)
sudo dpkg --add-architecture i386

# Core dependencies (PipeWire already installed on 24.04)
sudo apt update
sudo apt install -y \
    wine \
    wine64 \
    libwine:i386 \
    winetricks \
    libpipewire-0.3-dev \
    libjack-jackd2-dev \
    build-essential \
    git \
    flatpak

# Realtime audio permissions (for low-latency)
sudo usermod -aG audio $USER

# Optional: Configure realtime limits for even lower latency
sudo groupadd -f realtime
sudo usermod -aG realtime $USER
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

See `scripts/setup-wine-vst3.sh` in the repository, or run:

```bash
./scripts/setup-wine-vst3.sh
```

The script automatically:
1. Installs Wine and build dependencies
2. Configures realtime audio permissions
3. Installs ProtonUp-Qt via Flatpak
4. Builds and installs PipeWine from source
5. Creates a dedicated Wine prefix for VST3 plugins
6. Installs Visual C++ runtime (required by most plugins)
7. Registers the PipeWine ASIO driver

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
