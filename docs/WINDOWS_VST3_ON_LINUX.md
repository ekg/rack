# Running Windows VST3 Plugins on Linux

Load Windows VST3 plugins in Linux hosts using **yabridge**.

**Target:** Ubuntu 24.04+ (PipeWire is default)

## How It Works

```
Windows VST3 plugin
        ↓
    [Wine process]
        ↓ (shared memory)
    [yabridge .so wrapper]
        ↓
    Linux DAW / rack
        ↓
    PipeWire (audio output)
```

Audio stays on the Linux side. Wine only runs the plugin logic.

## Quick Install (Ubuntu 24.04+)

### Step 1: Install Wine

```bash
sudo dpkg --add-architecture i386
sudo apt update
sudo apt install -y wine wine64 winetricks
```

### Step 2: Install Yabridge

```bash
# Download latest release
YABRIDGE_VERSION="5.1.1"
curl -LO "https://github.com/robbert-vdh/yabridge/releases/download/${YABRIDGE_VERSION}/yabridge-${YABRIDGE_VERSION}.tar.gz"

# Install to ~/.local/share/yabridge
mkdir -p ~/.local/share/yabridge
tar -xzf "yabridge-${YABRIDGE_VERSION}.tar.gz" -C ~/.local/share/yabridge --strip-components=1

# Add to PATH
echo 'export PATH="$HOME/.local/share/yabridge:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

### Step 3: Setup Wine Prefix

```bash
export WINEPREFIX=~/.wine-vst3
export WINEARCH=win64

# Initialize Wine
wineboot --init

# Install Visual C++ runtime (required by most plugins)
winetricks -q vcrun2019

# Create VST3 directory
mkdir -p "$WINEPREFIX/drive_c/Program Files/Common Files/VST3"
```

### Step 4: Configure Yabridge

```bash
# Tell yabridge where Windows VST3s are
yabridgectl add "$WINEPREFIX/drive_c/Program Files/Common Files/VST3"

# Create Linux wrappers
yabridgectl sync
```

## Automated Setup

```bash
./scripts/setup-wine-vst3.sh
```

## Installing Windows VST3 Plugins

```bash
# Copy .vst3 file/folder to Wine prefix
cp -r /path/to/plugin.vst3 ~/.wine-vst3/drive_c/Program\ Files/Common\ Files/VST3/

# Or run Windows installer
wine64 /path/to/plugin-installer.exe

# Regenerate yabridge wrappers
yabridgectl sync
```

Yabridge creates Linux `.so` wrappers in `~/.vst3/yabridge/`. These can be loaded by rack or any Linux DAW.

## Testing

```bash
# List yabridge-wrapped plugins
ls ~/.vst3/yabridge/

# Check yabridge status
yabridgectl status
```

## Troubleshooting

### Plugin doesn't appear after sync

```bash
# Check for errors
yabridgectl status

# Verify plugin is in the right place
ls ~/.wine-vst3/drive_c/Program\ Files/Common\ Files/VST3/
```

### Plugin crashes on load

Some plugins need additional Windows components:
```bash
# .NET Framework
winetricks -q dotnet48

# DirectX
winetricks -q dxvk
```

### GUI rendering issues

```bash
# Try software rendering
winetricks renderer=gdi
```

## Integration with Rack

Rack can scan yabridge-wrapped plugins from the standard Linux VST3 path:

```bash
export VST3_PATH="$HOME/.vst3:$VST3_PATH"
```

The wrapped plugins appear as native Linux VST3s.

## Performance

| Metric | Native Linux VST3 | Yabridge Windows VST3 |
|--------|-------------------|----------------------|
| Load time | ~100ms | ~500ms (Wine startup) |
| Audio latency | 0 | <1ms (shared memory) |
| Memory | Plugin only | +Wine process (~50MB) |
| CPU | Plugin only | +~5% IPC overhead |

## Resources

- [Yabridge GitHub](https://github.com/robbert-vdh/yabridge)
- [Yabridge Releases](https://github.com/robbert-vdh/yabridge/releases)
- [Wine HQ](https://www.winehq.org/)
