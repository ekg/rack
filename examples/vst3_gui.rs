//! VST3 Plugin GUI Example
//!
//! This example demonstrates how to create and display a VST3 plugin's GUI.
//! Supports both Linux (X11) and Windows (HWND).
//!
//! **IMPORTANT**: This example must be run on the main thread. Both X11 and
//! Windows require the main thread for GUI operations.
//!
//! # Usage
//!
//! ```bash
//! cargo run --example vst3_gui --features vst3
//! ```
//!
//! # Requirements
//!
//! - Linux with X11, or Windows
//! - At least one VST3 plugin installed (e.g., Surge XT)
//! - Linux only: X11 development libraries: `sudo apt-get install libx11-dev`

use rack::prelude::*;
use std::time::Duration;

fn main() -> Result<()> {
    #[cfg(target_os = "linux")]
    println!("VST3 Plugin GUI Example (Linux/X11)");
    #[cfg(target_os = "windows")]
    println!("VST3 Plugin GUI Example (Windows/HWND)");
    println!("======================================\n");

    // Create scanner
    println!("Creating VST3 scanner...");
    let scanner = Scanner::new()?;

    // Scan for plugins
    println!("Scanning for VST3 plugins...");
    let plugins = scanner.scan()?;

    if plugins.is_empty() {
        println!("No VST3 plugins found!");
        println!("\nTo install a test plugin, download Surge XT from:");
        println!("  https://surge-synthesizer.github.io/");
        return Ok(());
    }

    println!("Found {} plugin(s)\n", plugins.len());

    // List available plugins
    println!("Available plugins:");
    for (i, p) in plugins.iter().enumerate().take(10) {
        println!("  [{}] {} by {} ({:?})", i, p.name, p.manufacturer, p.plugin_type);
    }
    if plugins.len() > 10 {
        println!("  ... and {} more", plugins.len() - 10);
    }
    println!();

    // Try to find Surge XT first (known to have good GUI support)
    // Otherwise, find first instrument, then first effect
    let plugin_info = plugins
        .iter()
        .find(|p| p.name.contains("Surge"))
        .or_else(|| plugins.iter().find(|p| p.plugin_type == PluginType::Instrument))
        .or_else(|| plugins.iter().find(|p| p.plugin_type == PluginType::Effect))
        .or_else(|| plugins.first());

    let Some(info) = plugin_info else {
        println!("No suitable plugin found");
        return Ok(());
    };

    println!("Selected plugin:");
    println!("  Name: {}", info.name);
    println!("  Manufacturer: {}", info.manufacturer);
    println!("  Type: {:?}", info.plugin_type);
    println!("  Path: {}", info.path.display());
    println!();

    // Create and initialize plugin
    println!("Creating plugin instance...");
    let mut plugin = scanner.load(info)?;
    println!("  Plugin instance created");

    println!("Initializing plugin (48kHz, 512 samples)...");
    plugin.initialize(48000.0, 512)?;
    println!("  Plugin initialized successfully!\n");

    // Create GUI
    println!("Creating plugin GUI...");
    let mut gui = match Vst3Gui::create(&mut plugin) {
        Ok(gui) => {
            println!("  GUI created successfully!");
            gui
        }
        Err(e) => {
            println!("  Failed to create GUI: {}", e);
            #[cfg(target_os = "linux")]
            {
                println!("\nNote: Some plugins don't support GUI on Linux, or");
                println!("they may require specific X11 libraries. Ensure you have:");
                println!("  libx11-xcb-dev libxcb-util-dev libxcb-cursor-dev");
                println!("  libxcb-xkb-dev libxkbcommon-dev libxkbcommon-x11-dev");
            }
            #[cfg(target_os = "windows")]
            {
                println!("\nNote: Some plugins don't support GUI on Windows.");
            }
            return Err(e);
        }
    };

    // Get and display GUI size
    if let Ok((width, height)) = gui.get_size() {
        println!("  GUI size: {}x{} pixels", width, height);
    }

    // Get native window ID
    let window_id = gui.get_window_id();
    if window_id != 0 {
        #[cfg(target_os = "linux")]
        println!("  X11 window ID: 0x{:x}", window_id);
        #[cfg(target_os = "windows")]
        println!("  HWND: 0x{:x}", window_id);
    }

    // Show the window
    println!("\nShowing plugin window...");
    gui.show(Some(&info.name))?;
    println!("  Window is now visible!");

    println!("\n========================================");
    println!("The plugin GUI window should now be visible.");
    println!("Close the window or press Ctrl+C to exit.");
    println!("========================================\n");

    // Event loop - process GUI events
    println!("Running event loop...");
    loop {
        // Process pending GUI events
        let events = gui.pump_events();
        if events < 0 {
            println!("Error processing events: {}", events);
            break;
        }

        // Check if window was closed
        if !gui.is_visible() {
            println!("\nWindow was closed.");
            break;
        }

        // Small sleep to avoid busy-waiting (~60 FPS)
        std::thread::sleep(Duration::from_millis(16));
    }

    println!("\nCleaning up...");
    // GUI will be dropped here, cleaning up native window resources

    println!("Example complete!");

    Ok(())
}
