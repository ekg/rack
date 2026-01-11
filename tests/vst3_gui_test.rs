//! Integration tests for VST3 GUI support
//!
//! These tests require:
//! 1. A running X11 display (or Xvfb)
//! 2. At least one VST3 plugin installed
//!
//! To run with Xvfb:
//! ```bash
//! xvfb-run cargo test --test vst3_gui_test
//! ```

#![cfg(all(target_os = "linux", feature = "vst3"))]

use rack::{PluginScanner, PluginType};
use std::time::Duration;

/// Test that we can scan for VST3 plugins
#[test]
fn test_vst3_scanner() {
    use rack::vst3::Vst3Scanner;

    let scanner = Vst3Scanner::new().expect("Failed to create scanner");
    let plugins = scanner.scan().expect("Failed to scan");

    // Just verify scanning works - may or may not find plugins
    println!("Found {} VST3 plugins", plugins.len());
}

/// Test that we can load and initialize a VST3 plugin
#[test]
fn test_vst3_plugin_load() {
    use rack::vst3::Vst3Scanner;
    use rack::PluginInstance;

    let scanner = Vst3Scanner::new().expect("Failed to create scanner");
    let plugins = scanner.scan().expect("Failed to scan");

    if plugins.is_empty() {
        println!("Skipping test - no VST3 plugins found");
        return;
    }

    let mut plugin = scanner.load(&plugins[0]).expect("Failed to load plugin");
    plugin.initialize(48000.0, 512).expect("Failed to initialize");

    assert!(plugin.is_initialized());
    println!("Loaded and initialized: {}", plugins[0].name);
}

/// Test GUI creation (requires X11 display)
///
/// This test will be skipped if no display is available.
#[test]
#[cfg(target_os = "linux")]
fn test_vst3_gui_creation() {
    use rack::vst3::{Vst3Scanner, Vst3Gui};
    use rack::PluginInstance;

    // Check if DISPLAY is set
    if std::env::var("DISPLAY").is_err() {
        println!("Skipping GUI test - no DISPLAY set (run with xvfb-run)");
        return;
    }

    let scanner = Vst3Scanner::new().expect("Failed to create scanner");
    let plugins = scanner.scan().expect("Failed to scan");

    if plugins.is_empty() {
        println!("Skipping GUI test - no VST3 plugins found");
        return;
    }

    // Find a plugin that might have GUI support (prefer instruments or known plugins)
    let plugin_info = plugins
        .iter()
        .find(|p| p.name.contains("Surge"))
        .or_else(|| plugins.iter().find(|p| p.plugin_type == PluginType::Instrument))
        .or_else(|| plugins.first());

    let Some(info) = plugin_info else {
        println!("Skipping GUI test - no suitable plugin found");
        return;
    };

    println!("Testing GUI with: {}", info.name);

    let mut plugin = scanner.load(info).expect("Failed to load plugin");
    plugin.initialize(48000.0, 512).expect("Failed to initialize");

    // Try to create GUI
    match Vst3Gui::create(&mut plugin) {
        Ok(mut gui) => {
            println!("GUI created successfully!");

            // Get size
            if let Ok((width, height)) = gui.get_size() {
                println!("GUI size: {}x{}", width, height);
                assert!(width > 0, "Width should be positive");
                assert!(height > 0, "Height should be positive");
            }

            // Get window ID
            let window_id = gui.get_window_id();
            println!("Window ID: 0x{:x}", window_id);

            // Show the window briefly
            gui.show(Some(&info.name)).expect("Failed to show");
            assert!(gui.is_visible(), "Window should be visible after show()");

            // Pump some events
            for _ in 0..10 {
                gui.pump_events();
                std::thread::sleep(Duration::from_millis(50));
            }

            // Hide the window
            gui.hide().expect("Failed to hide");
            assert!(!gui.is_visible(), "Window should not be visible after hide()");

            println!("GUI test passed!");
        }
        Err(e) => {
            // Some plugins don't support GUI on Linux
            println!("GUI creation failed (may be expected): {}", e);
        }
    }
}

/// Test that GUI properly handles visibility toggle
#[test]
#[cfg(target_os = "linux")]
fn test_vst3_gui_visibility() {
    use rack::vst3::{Vst3Scanner, Vst3Gui};
    use rack::PluginInstance;

    if std::env::var("DISPLAY").is_err() {
        println!("Skipping GUI test - no DISPLAY set");
        return;
    }

    let scanner = Vst3Scanner::new().expect("Failed to create scanner");
    let plugins = scanner.scan().expect("Failed to scan");

    if plugins.is_empty() {
        println!("Skipping test - no plugins");
        return;
    }

    let mut plugin = scanner.load(&plugins[0]).expect("Failed to load");
    plugin.initialize(48000.0, 512).expect("Failed to initialize");

    if let Ok(mut gui) = Vst3Gui::create(&mut plugin) {
        // Initially not visible
        assert!(!gui.is_visible());

        // Show
        gui.show(None).expect("show failed");
        assert!(gui.is_visible());

        // Pump events
        gui.pump_events();

        // Hide
        gui.hide().expect("hide failed");
        assert!(!gui.is_visible());

        // Show again
        gui.show(Some("Test")).expect("show failed");
        assert!(gui.is_visible());

        println!("Visibility toggle test passed!");
    } else {
        println!("Skipping - plugin doesn't support GUI");
    }
}
