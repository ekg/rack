# Handoff: Add VST3 GUI Support to rack (Linux/X11)

## Repository

Fork: https://github.com/ekg/rack
Upstream: https://github.com/sinkingsugar/rack

## Goal

Add VST3 plugin GUI support for Linux using X11, following the existing AU GUI pattern.

## Why This Matters

Phonon uses rack for VST3 hosting. We can process audio and modulate parameters, but users can't see or interact with plugin GUIs. This blocks the full vision of visual patch editing.

## Technical Background

### VST3 GUI Architecture

A VST3 plugin has:
- `IAudioProcessor` - audio processing
- `IEditController` - parameters AND GUI via `createView()`

The GUI and audio share the same instance. When you tweak a knob in the GUI, it affects audio processing directly.

### Linux/X11 Requirements

From the [VST3 SDK editorhost](https://github.com/steinbergmedia/vst3_public_sdk/blob/master/samples/vst-hosting/editorhost/source/platform/linux/window.cpp):

1. Get `IEditController` from plugin
2. Call `createView(ViewType::kEditor)` to get `IPlugView`
3. Create X11 window
4. Call `plugView->attached(window_id, "X11EmbedWindowID")`
5. Implement `IPlugFrame` for resize callbacks
6. Handle X11 events (ConfigureNotify, FocusIn/Out, etc.)

### Dependencies

```bash
sudo apt-get install libx11-xcb-dev libxcb-util-dev libxcb-cursor-dev \
  libxcb-xkb-dev libxkbcommon-dev libxkbcommon-x11-dev libcairo2-dev
```

## Files to Create/Modify

### 1. `rack-sys/include/rack_vst3.h` - Add GUI declarations

Add after the MIDI API section:

```c
// ============================================================================
// GUI API (Linux/X11)
// ============================================================================

// GUI callback for async operations
typedef void (*RackVST3GuiCallback)(void* user_data, RackVST3Gui* gui, int error_code);

// Create GUI for a plugin
// plugin: initialized plugin instance
// Returns GUI handle or NULL on error
RackVST3Gui* rack_vst3_gui_create(RackVST3Plugin* plugin);

// Free GUI
void rack_vst3_gui_free(RackVST3Gui* gui);

// Show GUI window
// title: window title (can be NULL for default)
// Returns 0 on success, negative error code on failure
int rack_vst3_gui_show(RackVST3Gui* gui, const char* title);

// Hide GUI window
int rack_vst3_gui_hide(RackVST3Gui* gui);

// Check if GUI is visible
int rack_vst3_gui_is_visible(RackVST3Gui* gui);

// Get GUI size
int rack_vst3_gui_get_size(RackVST3Gui* gui, uint32_t* width, uint32_t* height);

// Process pending GUI events (call regularly from main thread)
// Returns number of events processed, or negative error code
int rack_vst3_gui_pump_events(RackVST3Gui* gui);

// Get X11 window ID (for embedding in host UI)
// Returns window ID or 0 if not available
unsigned long rack_vst3_gui_get_window_id(RackVST3Gui* gui);
```

### 2. `rack-sys/src/vst3_gui.cpp` - New file

Create this file with X11 implementation. Key structure:

```cpp
#include "rack_vst3.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>

using namespace Steinberg;

// IPlugFrame implementation for resize callbacks
class PlugFrame : public IPlugFrame {
public:
    // ... implement resizeView(), etc.
    DECLARE_FUNKNOWN_METHODS
};

struct RackVST3Gui {
    IPlugView* view;
    Display* display;
    Window window;
    PlugFrame* frame;
    bool visible;
    uint32_t width;
    uint32_t height;
    char error_message[256];
};

extern "C" {

RackVST3Gui* rack_vst3_gui_create(RackVST3Plugin* plugin) {
    // 1. Get IEditController from plugin (need to expose this)
    // 2. Call controller->createView(ViewType::kEditor)
    // 3. Open X11 display
    // 4. Create window
    // 5. Call view->attached(window, "X11EmbedWindowID")
    // 6. Set up IPlugFrame
}

void rack_vst3_gui_show(RackVST3Gui* gui, const char* title) {
    XMapWindow(gui->display, gui->window);
    XFlush(gui->display);
    gui->visible = true;
}

int rack_vst3_gui_pump_events(RackVST3Gui* gui) {
    // Process X11 events
    XEvent event;
    int count = 0;
    while (XPending(gui->display)) {
        XNextEvent(gui->display, &event);
        // Handle ConfigureNotify, Expose, KeyPress, etc.
        count++;
    }
    return count;
}

// ... rest of implementation

}
```

### 3. `rack-sys/src/vst3_instance.cpp` - Expose IEditController

Need to add a function to get IEditController from plugin:

```cpp
// Add to RackVST3PluginImpl struct:
IPtr<IEditController> edit_controller;

// Add new FFI function:
extern "C" void* rack_vst3_plugin_get_edit_controller(RackVST3Plugin* plugin) {
    auto* impl = reinterpret_cast<RackVST3PluginImpl*>(plugin);
    if (impl && impl->edit_controller) {
        impl->edit_controller->addRef();
        return impl->edit_controller.get();
    }
    return nullptr;
}
```

### 4. `rack-sys/CMakeLists.txt` - Add new source

```cmake
# Add vst3_gui.cpp to sources (Linux only)
if(UNIX AND NOT APPLE)
    list(APPEND RACK_SYS_SOURCES src/vst3_gui.cpp)
    find_package(X11 REQUIRED)
    target_link_libraries(rack_sys PRIVATE ${X11_LIBRARIES})
endif()
```

### 5. `src/vst3/ffi.rs` - Add FFI bindings

```rust
#[repr(C)]
pub struct RackVST3Gui {
    _private: [u8; 0],
}

extern "C" {
    pub fn rack_vst3_gui_create(plugin: *mut RackVST3Plugin) -> *mut RackVST3Gui;
    pub fn rack_vst3_gui_free(gui: *mut RackVST3Gui);
    pub fn rack_vst3_gui_show(gui: *mut RackVST3Gui, title: *const c_char) -> c_int;
    pub fn rack_vst3_gui_hide(gui: *mut RackVST3Gui) -> c_int;
    pub fn rack_vst3_gui_is_visible(gui: *mut RackVST3Gui) -> c_int;
    pub fn rack_vst3_gui_get_size(
        gui: *mut RackVST3Gui,
        width: *mut u32,
        height: *mut u32,
    ) -> c_int;
    pub fn rack_vst3_gui_pump_events(gui: *mut RackVST3Gui) -> c_int;
    pub fn rack_vst3_gui_get_window_id(gui: *mut RackVST3Gui) -> c_ulong;
}
```

### 6. `src/vst3/gui.rs` - New file, safe Rust wrapper

```rust
use super::ffi;
use crate::{Error, Result, Vst3Plugin};
use std::ffi::CString;

pub struct Vst3Gui {
    handle: *mut ffi::RackVST3Gui,
}

unsafe impl Send for Vst3Gui {}

impl Vst3Gui {
    pub fn create(plugin: &mut Vst3Plugin) -> Result<Self> {
        unsafe {
            let handle = ffi::rack_vst3_gui_create(plugin.as_ptr());
            if handle.is_null() {
                return Err(Error::Other("Failed to create GUI".into()));
            }
            Ok(Self { handle })
        }
    }

    pub fn show(&mut self, title: Option<&str>) -> Result<()> {
        let title_cstr = title.map(|t| CString::new(t).unwrap());
        let title_ptr = title_cstr.as_ref().map(|c| c.as_ptr()).unwrap_or(std::ptr::null());
        unsafe {
            let result = ffi::rack_vst3_gui_show(self.handle, title_ptr);
            if result < 0 {
                return Err(Error::Other("Failed to show GUI".into()));
            }
        }
        Ok(())
    }

    pub fn hide(&mut self) -> Result<()> {
        unsafe {
            ffi::rack_vst3_gui_hide(self.handle);
        }
        Ok(())
    }

    pub fn pump_events(&mut self) -> i32 {
        unsafe { ffi::rack_vst3_gui_pump_events(self.handle) }
    }

    pub fn is_visible(&self) -> bool {
        unsafe { ffi::rack_vst3_gui_is_visible(self.handle) != 0 }
    }
}

impl Drop for Vst3Gui {
    fn drop(&mut self) {
        unsafe {
            ffi::rack_vst3_gui_free(self.handle);
        }
    }
}
```

### 7. `src/vst3/mod.rs` - Export gui module

```rust
#[cfg(all(feature = "vst3", target_os = "linux"))]
pub mod gui;

#[cfg(all(feature = "vst3", target_os = "linux"))]
pub use gui::Vst3Gui;
```

## Reference Materials

1. **VST3 SDK editorhost** (official example):
   - https://github.com/steinbergmedia/vst3_public_sdk/blob/master/samples/vst-hosting/editorhost/source/platform/linux/window.cpp
   - https://github.com/steinbergmedia/vst3_public_sdk/blob/master/samples/vst-hosting/editorhost/source/platform/linux/platform.cpp

2. **IPlugView documentation**:
   - https://steinbergmedia.github.io/vst3_doc/base/classSteinberg_1_1IPlugView.html

3. **Existing AU GUI in rack** (pattern to follow):
   - `rack-sys/src/au_gui.mm`

4. **yabridge** (mature Linux VST host with GUI):
   - https://github.com/robbert-vdh/yabridge

## Testing

1. Build rack with VST3 feature:
   ```bash
   cd ~/rack
   cargo build --features vst3
   ```

2. Create test example `examples/vst3_gui.rs`:
   ```rust
   use rack::prelude::*;

   fn main() -> Result<()> {
       let scanner = Vst3Scanner::new()?;
       let plugins = scanner.scan()?;

       // Find Surge XT
       let surge = plugins.iter()
           .find(|p| p.name.contains("Surge"))
           .expect("Surge XT not found");

       let mut plugin = scanner.load(surge)?;
       plugin.initialize(48000.0, 512)?;

       let mut gui = Vst3Gui::create(&mut plugin)?;
       gui.show(Some("Surge XT"))?;

       // Event loop
       loop {
           gui.pump_events();
           std::thread::sleep(std::time::Duration::from_millis(16));
       }
   }
   ```

3. Run:
   ```bash
   cargo run --example vst3_gui --features vst3
   ```

## Success Criteria

- Surge XT GUI window opens and displays correctly
- Knob/slider changes in GUI affect audio output
- Window resizes properly
- Focus handling works
- No crashes on close

## Notes

- Start simple: get a window to appear first, then refine
- The XEMBED protocol can be complex; basic X11 embedding may work initially
- GTK is optional; raw X11 is simpler to start
- Thread safety: GUI must run on main thread

## After Implementation

Once rack has GUI support:
1. Update phonon's Cargo.toml to use the fork
2. Add GUI integration to phonon-edit
3. Contribute back to upstream rack
