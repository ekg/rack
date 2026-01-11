//! Test Wine VST3 host integration
//!
//! This example demonstrates loading Windows VST3 plugins via Wine.
//! It requires:
//! 1. Wine installed
//! 2. The rack-wine-host.exe binary built and available
//! 3. Windows VST3 plugins installed in ~/.wine/drive_c/Program Files/Common Files/VST3
//!
//! Run with: cargo run --example wine_vst3 -- /path/to/rack-wine-host.exe [/optional/path/to/plugin.vst3]

#[cfg(target_os = "linux")]
use rack::wine_host::{WineVst3Plugin, WineVst3Scanner};
#[cfg(target_os = "linux")]
use rack::{PluginInstance, PluginScanner, Result};

#[cfg(target_os = "linux")]
fn main() -> Result<()> {
    let args: Vec<String> = std::env::args().collect();

    if args.len() < 2 {
        eprintln!("Usage: {} <path-to-rack-wine-host.exe> [plugin.vst3]", args[0]);
        eprintln!();
        eprintln!("Example:");
        eprintln!("  {} ./rack-wine-host/build/rack-wine-host.exe", args[0]);
        eprintln!("  {} ./rack-wine-host.exe ~/.wine/drive_c/Program\\ Files/Common\\ Files/VST3/MyPlugin.vst3", args[0]);
        std::process::exit(1);
    }

    let host_exe = &args[1];

    println!("Wine VST3 Host Test");
    println!("==================");
    println!("Host executable: {}", host_exe);

    // If a specific plugin path is given, load it directly
    if args.len() >= 3 {
        let plugin_path = &args[2];
        println!("Loading plugin: {}", plugin_path);
        println!();

        let mut plugin = WineVst3Plugin::new(
            std::path::Path::new(host_exe),
            std::path::Path::new(plugin_path),
            None,
        )?;

        println!("Plugin loaded successfully!");
        println!("  Name: {}", plugin.info().name);
        println!("  Manufacturer: {}", plugin.info().manufacturer);
        println!("  UID: {}", plugin.info().unique_id);
        println!("  Parameters: {}", plugin.parameter_count());
        println!();

        // Initialize
        println!("Initializing with 48kHz sample rate, 512 block size...");
        plugin.initialize(48000.0, 512)?;
        println!("Initialized successfully!");
        println!();

        // Process a test block
        println!("Processing test audio block...");
        let inputs = [vec![0.0f32; 512], vec![0.0f32; 512]];
        let mut outputs = [vec![0.0f32; 512], vec![0.0f32; 512]];

        let input_refs: Vec<&[f32]> = inputs.iter().map(|v| v.as_slice()).collect();
        let mut output_refs: Vec<&mut [f32]> = outputs.iter_mut().map(|v| v.as_mut_slice()).collect();

        plugin.process(&input_refs, &mut output_refs, 512)?;

        // Check for non-zero output
        let max_output: f32 = outputs.iter()
            .flat_map(|ch| ch.iter())
            .map(|s| s.abs())
            .fold(0.0, f32::max);

        println!("Max output level: {:.6}", max_output);
        println!();

        // Try opening editor
        println!("Opening plugin editor...");
        match plugin.open_editor() {
            Ok((window_id, width, height)) => {
                println!("Editor opened successfully!");
                println!("  X11 Window ID: 0x{:x}", window_id);
                println!("  Size: {}x{}", width, height);
                println!();

                // Keep editor open for a moment
                println!("Editor will close in 3 seconds...");
                std::thread::sleep(std::time::Duration::from_secs(3));

                plugin.close_editor()?;
                println!("Editor closed.");
            }
            Err(e) => {
                println!("Could not open editor: {}", e);
            }
        }

        return Ok(());
    }

    // Otherwise, scan for plugins
    println!();
    println!("Scanning for Windows VST3 plugins...");

    let scanner = WineVst3Scanner::new(host_exe);
    let plugins = scanner.scan()?;

    if plugins.is_empty() {
        println!("No Windows VST3 plugins found.");
        println!();
        println!("Make sure you have Windows VST3 plugins installed in:");
        println!("  ~/.wine/drive_c/Program Files/Common Files/VST3");
        println!("  ~/.wine/drive_c/Program Files (x86)/Common Files/VST3");
        return Ok(());
    }

    println!("Found {} Windows VST3 plugin(s):", plugins.len());
    println!();

    for plugin in &plugins {
        println!("{}", plugin.name);
        println!("  Path: {}", plugin.path.display());
        println!();
    }

    println!("To load a specific plugin, run:");
    println!("  {} {} <plugin-path>", args[0], host_exe);

    Ok(())
}

#[cfg(not(target_os = "linux"))]
fn main() {
    println!("Wine VST3 host is only available on Linux.");
}
