# Windows VST3 on Linux Integration

## Summary

Running Windows VST3 plugins on Linux requires Wine. There is **no way** to directly load Windows PE binaries on Linux. This document outlines integration strategies for rack.

## Verified Test

We downloaded and verified a Windows VST3 binary:
```
File: peakeater.vst3
Type: PE32+ executable (DLL) (GUI) x86-64, for MS Windows
```

## Integration Options

### Option 1: Yabridge Integration (Recommended)

**Yabridge** is the production-proven solution used by professional audio producers.

**Architecture:**
- Host loads Linux `.so` wrapper (appears as native VST3)
- Wrapper communicates with Wine process via Unix sockets
- Audio buffers use shared memory (v3.4+) for minimal latency
- Expected overhead: 0.1-1ms

**Integration for Rack:**
```rust
// In rack/src/vst3/yabridge.rs
pub struct YabridgePlugin {
    // Wrapper around yabridge's Linux .so
    inner: Vst3Plugin,  // Load the .so wrapper
}

impl YabridgePlugin {
    pub fn from_windows_vst3(path: &Path) -> Result<Self> {
        // 1. Check if yabridge is installed
        // 2. Run yabridgectl to create wrapper if needed
        // 3. Load the generated .so as normal VST3
    }
}
```

**User Setup Required:**
```bash
# Install Wine and Yabridge
sudo apt install wine wine64
# Download yabridge from GitHub releases
# Run: yabridgectl add /path/to/windows/vst3s
# Run: yabridgectl sync
```

**Pros:**
- Production-proven, widely used
- Transparent to DAW (appears as native plugin)
- Best latency (shared memory for audio)
- Active development

**Cons:**
- External dependency (yabridge must be installed)
- Requires Wine setup by user

### Option 2: LinVst3 Integration

Similar to Yabridge but simpler architecture.

**Pros:**
- Simpler codebase
- Easier to understand

**Cons:**
- Less maintained
- No shared memory optimization
- Project compatibility issues

### Option 3: Direct Wine Integration (Not Recommended)

Build Wine loading directly into rack.

**Why Not:**
- Massive complexity (Wine is 30+ years of development)
- Would need to maintain Wine compatibility
- Yabridge already solved this problem

### Option 4: Use Steam Proton

**Proton** is Valve's Wine fork optimized for gaming.

**Audio-Relevant Proton Features:**
- `WINEFSYNC=1` for lower latency
- Regular updates and testing
- Good DirectX support (for plugin GUIs)

**Why Not Ideal for VST3:**
- Optimized for gaming (100ms+ latency acceptable)
- Not designed for <10ms audio latency
- Would still need IPC bridge like Yabridge

## Recommended Implementation Plan

### Phase 1: Yabridge Detection & Auto-Scan

```rust
// src/vst3/yabridge.rs

/// Check if yabridge is installed
pub fn yabridge_available() -> bool {
    which::which("yabridgectl").is_ok()
}

/// Scan for yabridge-wrapped Windows VST3s
pub fn scan_yabridge_plugins() -> Vec<PluginInfo> {
    // Yabridge creates .so wrappers in ~/.vst3/yabridge/
    // These can be loaded as normal Linux VST3s
}
```

### Phase 2: Automatic Wrapper Creation

```rust
/// Convert Windows VST3 to yabridge wrapper
pub fn wrap_windows_vst3(windows_path: &Path) -> Result<PathBuf> {
    // 1. Copy to Wine prefix
    // 2. Run yabridgectl add
    // 3. Run yabridgectl sync
    // 4. Return path to generated .so
}
```

### Phase 3: Wine Prefix Management

```rust
/// Manage Wine prefixes for VST3 plugins
pub struct WinePrefix {
    path: PathBuf,
    // vcrun2019 installed?
    has_vcruntime: bool,
}

impl WinePrefix {
    pub fn create() -> Result<Self> { ... }
    pub fn install_vcruntime(&mut self) -> Result<()> { ... }
}
```

## Testing Windows VST3s

Downloaded test plugin:
- **PeakEater** v0.8.2 - Wave shaper
- Path: `/tmp/win_vst3_test/peakeater_artefacts/Release/VST3/peakeater.vst3/`

To test with yabridge:
```bash
# Install Wine
sudo apt install wine wine64

# Install yabridge
# (download from https://github.com/robbert-vdh/yabridge/releases)

# Setup Wine prefix
export WINEPREFIX=~/.wine-vst
winecfg
winetricks vcrun2019

# Copy Windows VST3
mkdir -p "$WINEPREFIX/drive_c/Program Files/Common Files/VST3"
cp -r /tmp/win_vst3_test/peakeater_artefacts/Release/VST3/peakeater.vst3 \
      "$WINEPREFIX/drive_c/Program Files/Common Files/VST3/"

# Create yabridge wrapper
yabridgectl add "$WINEPREFIX/drive_c/Program Files/Common Files/VST3"
yabridgectl sync

# The wrapped plugin appears in ~/.vst3/yabridge/
# It can now be loaded by rack as a normal Linux VST3
```

## Performance Considerations

| Metric | Native Linux VST3 | Yabridge Windows VST3 |
|--------|-------------------|----------------------|
| Load time | ~100ms | ~500ms (Wine startup) |
| Audio latency | 0 | 0.1-1ms |
| Memory | Plugin only | +Wine process (~50MB) |
| CPU | Plugin only | +~5% (IPC overhead) |

## Limitations

1. **Project files not portable** - VST3 state saved on Linux won't load on Windows
2. **Not all plugins work** - Some require Windows APIs Wine doesn't support
3. **GUI may have issues** - Direct2D/DirectX rendering can be problematic
4. **Setup complexity** - Users must install Wine + yabridge

## Resources

- [Yabridge GitHub](https://github.com/robbert-vdh/yabridge)
- [Yabridge Architecture](https://github.com/robbert-vdh/yabridge/blob/master/docs/architecture.md)
- [LinVst3 GitHub](https://github.com/osxmidi/LinVst3)
- [Wine Audio](https://wiki.winehq.org/Audio)
- [WineASIO](https://github.com/wineasio/wineasio)
