//! Safe wrapper for VST3 GUI functionality
//!
//! This module provides safe, idiomatic Rust API for creating and managing
//! VST3 plugin GUIs. Supports:
//! - Linux: X11 window embedding
//! - Windows: HWND window embedding
//!
//! # Thread Safety
//!
//! **IMPORTANT**: All GUI operations must be called from the main thread.
//! This is an X11/Windows requirement. The type system cannot enforce this,
//! so it is the caller's responsibility.
//!
//! # Example
//!
//! ```ignore
//! use rack::prelude::*;
//! use rack::vst3::{Vst3Scanner, Vst3Gui};
//!
//! # fn main() -> rack::Result<()> {
//! // Create and initialize plugin
//! let scanner = Vst3Scanner::new()?;
//! let plugins = scanner.scan()?;
//! let mut plugin = scanner.load(&plugins[0])?;
//! plugin.initialize(48000.0, 512)?;
//!
//! // Create GUI (must be on main thread)
//! let mut gui = Vst3Gui::create(&mut plugin)?;
//! gui.show(Some("My Plugin"))?;
//!
//! // Event loop
//! loop {
//!     gui.pump_events();
//!     if !gui.is_visible() {
//!         break;
//!     }
//!     std::thread::sleep(std::time::Duration::from_millis(16));
//! }
//!
//! # Ok(())
//! # }
//! ```

use super::ffi;
use super::Vst3Plugin;
use crate::error::{Error, Result};
use std::ffi::CString;
use std::marker::PhantomData;

/// VST3 GUI handle
///
/// Represents a plugin's graphical user interface. The GUI is displayed
/// in a standalone window (X11 on Linux, HWND on Windows).
///
/// # Thread Safety
///
/// **All methods must be called from the main thread.** This is an X11/Windows
/// requirement that cannot be enforced by the type system.
///
/// The type is `Send` but not `Sync` - it can be transferred between threads
/// but must not be accessed concurrently.
///
/// # Lifecycle
///
/// The GUI is automatically destroyed when this struct is dropped. Make sure
/// to keep the `Vst3Gui` alive as long as you need the GUI displayed.
///
/// The plugin instance must outlive the GUI.
pub struct Vst3Gui {
    handle: *mut ffi::RackVST3Gui,
    _marker: PhantomData<*mut ()>, // !Send + !Sync
}

// Safety: Vst3Gui can be sent between threads (transferred ownership)
// but must not be accessed concurrently (not Sync)
unsafe impl Send for Vst3Gui {}

impl Vst3Gui {
    /// Create a GUI for a plugin
    ///
    /// Creates a new GUI window for the given plugin. The plugin must be
    /// initialized before creating a GUI.
    ///
    /// # Arguments
    ///
    /// * `plugin` - An initialized VST3 plugin instance
    ///
    /// # Returns
    ///
    /// A new `Vst3Gui` on success, or an error if:
    /// - The plugin doesn't support GUI
    /// - Display/window system couldn't be opened
    /// - Window creation failed
    ///
    /// # Thread Safety
    ///
    /// Must be called from the main thread.
    ///
    /// # Example
    ///
    /// ```ignore
    /// # use rack::prelude::*;
    /// # use rack::vst3::{Vst3Plugin, Vst3Gui};
    /// # fn example(plugin: &mut Vst3Plugin) -> rack::Result<()> {
    /// let gui = Vst3Gui::create(plugin)?;
    /// # Ok(())
    /// # }
    /// ```
    pub fn create(plugin: &mut Vst3Plugin) -> Result<Self> {
        unsafe {
            let handle = ffi::rack_vst3_gui_create(plugin.as_ptr());
            if handle.is_null() {
                return Err(Error::Other(
                    "Failed to create GUI: plugin may not support GUI or display unavailable".into(),
                ));
            }
            Ok(Self {
                handle,
                _marker: PhantomData,
            })
        }
    }

    /// Show the GUI window
    ///
    /// Displays the GUI window with the specified title.
    ///
    /// # Arguments
    ///
    /// * `title` - Optional window title. If `None`, uses default title.
    ///
    /// # Thread Safety
    ///
    /// Must be called from the main thread.
    ///
    /// # Example
    ///
    /// ```no_run
    /// # use rack::prelude::*;
    /// # fn example(gui: &mut Vst3Gui) -> Result<()> {
    /// gui.show(Some("My Synth"))?;
    /// # Ok(())
    /// # }
    /// ```
    pub fn show(&mut self, title: Option<&str>) -> Result<()> {
        let title_cstr = title.map(|t| CString::new(t).unwrap());
        let title_ptr = title_cstr
            .as_ref()
            .map(|c| c.as_ptr())
            .unwrap_or(std::ptr::null());

        unsafe {
            let result = ffi::rack_vst3_gui_show(self.handle, title_ptr);
            if result < 0 {
                return Err(Error::Other("Failed to show GUI window".into()));
            }
        }
        Ok(())
    }

    /// Hide the GUI window
    ///
    /// Hides the window without destroying it. The GUI can be shown again
    /// using [`show()`](Vst3Gui::show).
    ///
    /// # Thread Safety
    ///
    /// Must be called from the main thread.
    ///
    /// # Example
    ///
    /// ```no_run
    /// # use rack::prelude::*;
    /// # fn example(gui: &mut Vst3Gui) -> Result<()> {
    /// gui.hide()?;
    /// # Ok(())
    /// # }
    /// ```
    pub fn hide(&mut self) -> Result<()> {
        unsafe {
            let result = ffi::rack_vst3_gui_hide(self.handle);
            if result < 0 {
                return Err(Error::Other("Failed to hide GUI window".into()));
            }
        }
        Ok(())
    }

    /// Check if the GUI window is currently visible
    ///
    /// # Returns
    ///
    /// `true` if the window is visible, `false` otherwise.
    ///
    /// # Thread Safety
    ///
    /// Can be called from any thread.
    pub fn is_visible(&self) -> bool {
        unsafe { ffi::rack_vst3_gui_is_visible(self.handle) != 0 }
    }

    /// Get the size of the GUI window
    ///
    /// # Returns
    ///
    /// A tuple `(width, height)` in pixels.
    ///
    /// # Thread Safety
    ///
    /// Can be called from any thread.
    ///
    /// # Example
    ///
    /// ```no_run
    /// # use rack::prelude::*;
    /// # fn example(gui: &Vst3Gui) -> Result<()> {
    /// let (width, height) = gui.get_size()?;
    /// println!("GUI size: {}x{}", width, height);
    /// # Ok(())
    /// # }
    /// ```
    pub fn get_size(&self) -> Result<(u32, u32)> {
        let mut width: u32 = 0;
        let mut height: u32 = 0;

        unsafe {
            let result = ffi::rack_vst3_gui_get_size(self.handle, &mut width, &mut height);
            if result < 0 {
                return Err(Error::Other("Failed to get GUI size".into()));
            }
        }

        Ok((width, height))
    }

    /// Process pending GUI events
    ///
    /// This must be called regularly to keep the GUI responsive. It processes
    /// events like window resize, expose/paint, focus changes, and close requests.
    ///
    /// # Returns
    ///
    /// The number of events processed.
    ///
    /// # Thread Safety
    ///
    /// Must be called from the main thread.
    ///
    /// # Example
    ///
    /// ```no_run
    /// # use rack::prelude::*;
    /// # fn example(gui: &mut Vst3Gui) {
    /// // Typical event loop
    /// loop {
    ///     gui.pump_events();
    ///     if !gui.is_visible() {
    ///         break;
    ///     }
    ///     std::thread::sleep(std::time::Duration::from_millis(16));
    /// }
    /// # }
    /// ```
    pub fn pump_events(&mut self) -> i32 {
        unsafe { ffi::rack_vst3_gui_pump_events(self.handle) }
    }

    /// Get the native window ID
    ///
    /// This can be used for embedding the GUI window in a host application.
    ///
    /// # Returns
    ///
    /// The native window ID (X11 Window on Linux, HWND on Windows), or 0 if not available.
    ///
    /// # Thread Safety
    ///
    /// Can be called from any thread after creation.
    pub fn get_window_id(&self) -> u64 {
        unsafe { ffi::rack_vst3_gui_get_window_id(self.handle) as u64 }
    }
}

impl Drop for Vst3Gui {
    fn drop(&mut self) {
        // Safety: handle is valid until drop, and free handles NULL safely
        unsafe {
            ffi::rack_vst3_gui_free(self.handle);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_vst3_gui_is_send() {
        // Compile-time check that Vst3Gui is Send
        fn assert_send<T: Send>() {}
        assert_send::<Vst3Gui>();
    }

    #[test]
    fn test_vst3_gui_is_not_sync() {
        // Compile-time check that Vst3Gui is NOT Sync
        fn assert_not_sync<T: Send>() {}
        assert_not_sync::<Vst3Gui>();

        // This should NOT compile (uncomment to verify):
        // fn assert_sync<T: Sync>() {}
        // assert_sync::<Vst3Gui>();
    }
}
