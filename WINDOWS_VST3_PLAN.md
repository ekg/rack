# Windows VST3 GUI Implementation Plan

## Research Summary

Based on comprehensive research, here are the key findings:

### Current State
- **Rack already has Windows VST3 support** - scanning, loading, and processing work
- **Missing: Windows GUI support** - HWND embedding not yet implemented
- **Linux GUI works** - X11 embedding via IPlugView is functional

### Approaches Evaluated

| Approach | Verdict |
|----------|---------|
| **Wine/Proton** | Not suitable for real-time audio (~50ms+ latency) |
| **Yabridge** | Excellent for Linux users wanting Windows plugins |
| **Native Windows** | Best approach - already partially implemented |

## Implementation Plan for Windows GUI

### Phase 1: Windows GUI Support (Priority)

Create `rack-sys/src/vst3_gui_win.cpp` with:

```cpp
#include <Windows.h>
#include "pluginterfaces/gui/iplugview.h"

struct RackVST3Gui {
    IPlugView* view;
    HWND hwnd_parent;
    HWND hwnd_child;
    PlugFrame* frame;
    bool visible;
    UINT width, height;
};

// Key functions:
RackVST3Gui* rack_vst3_gui_create(RackVST3Plugin* plugin) {
    // 1. Get IEditController from plugin
    // 2. Call controller->createView(ViewType::kEditor)
    // 3. Check isPlatformTypeSupported(kPlatformTypeHWND)
    // 4. Create parent HWND window
    // 5. Call view->attached(hwnd, kPlatformTypeHWND)
    // 6. Set up IPlugFrame for resize callbacks
}
```

### CMakeLists.txt Changes

```cmake
if(WIN32)
    list(APPEND RACK_VST3_SOURCES src/vst3_gui_win.cpp)
endif()
```

### Rust FFI (already in place)

The FFI bindings in `src/vst3/ffi.rs` need Windows-specific implementations but the interface is the same:
- `rack_vst3_gui_create()`
- `rack_vst3_gui_free()`
- `rack_vst3_gui_show()`
- `rack_vst3_gui_hide()`
- `rack_vst3_gui_pump_events()` - Windows message pump

### Phase 2: Windows Build Verification

1. Cross-compile test from Linux using MinGW (limited, may have ABI issues)
2. Native Windows build with MSVC (recommended)
3. CI pipeline for Windows builds

### Phase 3: Yabridge Integration (Optional, Linux-only)

For Linux users wanting Windows VST3 plugins:

```rust
#[cfg(target_os = "linux")]
pub mod yabridge {
    // Detect yabridge shims in ~/.vst3/yabridge/
    // Load via existing VST3 infrastructure
    // Transparent to user code
}
```

## Technical Details

### VST3 Platform Types

```cpp
enum PlatformType {
    kPlatformTypeHWND,              // Windows
    kPlatformTypeX11EmbedWindowID,  // Linux X11
    kPlatformTypeNSView,            // macOS (Cocoa)
};
```

### Windows Event Loop Integration

```cpp
int rack_vst3_gui_pump_events(RackVST3Gui* gui) {
    MSG msg;
    int count = 0;
    while (PeekMessage(&msg, gui->hwnd_parent, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        count++;
    }
    return count;
}
```

### IPlugFrame for Windows

```cpp
class PlugFrame : public IPlugFrame {
    HWND hwnd;

    tresult resizeView(IPlugView* view, ViewRect* newSize) override {
        SetWindowPos(hwnd, NULL, 0, 0,
                     newSize->getWidth(),
                     newSize->getHeight(),
                     SWP_NOMOVE | SWP_NOZORDER);
        return view->onSize(newSize);
    }
};
```

## Performance Considerations

- Native Windows: <1ms overhead (ideal)
- Yabridge on Linux: ~10-20ms (acceptable for mixing)
- Zero-copy audio processing maintained

## Testing Strategy

1. Build on Windows with MSVC
2. Test with common plugins (Surge XT, Vital, Dexed)
3. Verify GUI creation, visibility, resize
4. Profile latency overhead

## References

- [VST3 SDK IPlugView](https://steinbergmedia.github.io/vst3_doc/base/classSteinberg_1_1IPlugView.html)
- [Yabridge Architecture](https://github.com/robbert-vdh/yabridge)
- [Windows VST3 Locations](https://helpcenter.steinberg.de/hc/en-us/articles/115000177084)
