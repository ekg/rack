//! Wine host for loading Windows VST3 plugins on Linux
//!
//! This module provides support for loading Windows VST3 plugins via Wine.
//! It spawns a Wine host process that loads the plugin and communicates
//! via TCP socket and shared memory for audio.

mod protocol;

use crate::{Error, MidiEvent, ParameterInfo, PluginInfo, PluginInstance, PluginScanner, PluginType, PresetInfo, Result};
use protocol::*;

use std::io::{Read, Write};
use std::net::TcpStream;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::atomic::{AtomicU32, Ordering};
use std::time::Duration;

/// Path to the Wine host executable (relative to the crate or absolute)
const WINE_HOST_EXE: &str = "rack-wine-host.exe";

/// Global counter for unique shared memory names
static SHM_COUNTER: AtomicU32 = AtomicU32::new(0);

/// IPC client for communicating with the Wine host
struct WineClient {
    stream: TcpStream,
}

impl WineClient {
    /// Connect to Wine host on given port
    fn connect(port: u16) -> Result<Self> {
        let addr = format!("127.0.0.1:{}", port);
        let stream = TcpStream::connect(&addr)
            .map_err(|e| Error::Other(format!("Failed to connect to Wine host: {}", e)))?;
        stream.set_read_timeout(Some(Duration::from_secs(30)))
            .map_err(|e| Error::Other(format!("Failed to set read timeout: {}", e)))?;
        stream.set_write_timeout(Some(Duration::from_secs(30)))
            .map_err(|e| Error::Other(format!("Failed to set write timeout: {}", e)))?;
        Ok(Self { stream })
    }

    /// Send a command with optional payload
    fn send_command(&mut self, cmd: HostCommand, payload: &[u8]) -> Result<()> {
        let header = Header::new(cmd, payload.len() as u32);
        self.stream.write_all(&header.to_bytes())
            .map_err(|e| Error::Other(format!("Failed to send header: {}", e)))?;
        if !payload.is_empty() {
            self.stream.write_all(payload)
                .map_err(|e| Error::Other(format!("Failed to send payload: {}", e)))?;
        }
        Ok(())
    }

    /// Receive a response
    fn recv_response(&mut self) -> Result<(ResponseHeader, Vec<u8>)> {
        let mut header_buf = [0u8; 12];
        self.stream.read_exact(&mut header_buf)
            .map_err(|e| Error::Other(format!("Failed to read response header: {}", e)))?;

        let header = ResponseHeader::from_bytes(&header_buf);
        if header.magic != RACK_WINE_RESPONSE_MAGIC {
            return Err(Error::Other("Invalid response magic".to_string()));
        }

        let mut payload = vec![0u8; header.payload_size as usize];
        if header.payload_size > 0 {
            self.stream.read_exact(&mut payload)
                .map_err(|e| Error::Other(format!("Failed to read response payload: {}", e)))?;
        }

        Ok((header, payload))
    }

    /// Send command and receive response, checking status
    fn request(&mut self, cmd: HostCommand, payload: &[u8]) -> Result<Vec<u8>> {
        self.send_command(cmd, payload)?;
        let (header, payload) = self.recv_response()?;
        match header.status() {
            Status::Ok => Ok(payload),
            Status::NotLoaded => Err(Error::NotInitialized),
            Status::NotInitialized => Err(Error::NotInitialized),
            Status::InvalidParam => Err(Error::InvalidParameter(0)),
            Status::Error => Err(Error::Other("Wine host returned error".to_string())),
        }
    }

    /// Ping the host
    fn ping(&mut self) -> Result<()> {
        self.request(HostCommand::Ping, &[])?;
        Ok(())
    }

    /// Load a plugin
    fn load_plugin(&mut self, path: &str, class_index: u32) -> Result<()> {
        let cmd = CmdLoadPlugin::new(path, class_index);
        self.request(HostCommand::LoadPlugin, &cmd.to_bytes())?;
        Ok(())
    }

    /// Get plugin info
    fn get_info(&mut self) -> Result<RespPluginInfo> {
        let payload = self.request(HostCommand::GetInfo, &[])?;
        RespPluginInfo::from_bytes(&payload)
            .ok_or_else(|| Error::Other("Invalid plugin info response".to_string()))
    }

    /// Get parameter count
    fn get_param_count(&mut self) -> Result<u32> {
        let payload = self.request(HostCommand::GetParamCount, &[])?;
        if payload.len() >= 4 {
            Ok(u32::from_le_bytes([payload[0], payload[1], payload[2], payload[3]]))
        } else {
            Err(Error::Other("Invalid param count response".to_string()))
        }
    }

    /// Get parameter info
    fn get_param_info(&mut self, index: u32) -> Result<RespParamInfo> {
        let payload = self.request(HostCommand::GetParamInfo, &index.to_le_bytes())?;
        RespParamInfo::from_bytes(&payload)
            .ok_or_else(|| Error::Other("Invalid param info response".to_string()))
    }

    /// Get parameter value
    fn get_param(&mut self, param_id: u32) -> Result<f64> {
        let cmd = CmdParam::new(param_id, 0.0);
        let payload = self.request(HostCommand::GetParam, &cmd.to_bytes())?;
        if payload.len() >= 8 {
            Ok(f64::from_le_bytes([
                payload[0], payload[1], payload[2], payload[3],
                payload[4], payload[5], payload[6], payload[7],
            ]))
        } else {
            Err(Error::Other("Invalid param value response".to_string()))
        }
    }

    /// Set parameter value
    fn set_param(&mut self, param_id: u32, value: f64) -> Result<()> {
        let cmd = CmdParam::new(param_id, value);
        self.request(HostCommand::SetParam, &cmd.to_bytes())?;
        Ok(())
    }

    /// Initialize audio
    fn init_audio(&mut self, sample_rate: u32, block_size: u32, num_inputs: u32, num_outputs: u32, shm_name: &str) -> Result<()> {
        let cmd = CmdInitAudio::new(sample_rate, block_size, num_inputs, num_outputs, shm_name);
        self.request(HostCommand::InitAudio, &cmd.to_bytes())?;
        Ok(())
    }

    /// Process audio
    fn process_audio(&mut self, num_samples: u32) -> Result<()> {
        let cmd = CmdProcessAudio::new(num_samples);
        self.request(HostCommand::ProcessAudio, &cmd.to_bytes())?;
        Ok(())
    }

    /// Send MIDI events
    fn send_midi(&mut self, events: &[protocol::MidiEvent]) -> Result<()> {
        let mut payload = Vec::with_capacity(4 + events.len() * 8);
        let header = CmdMidi { num_events: events.len() as u32 };
        payload.extend_from_slice(&header.to_bytes());
        for event in events {
            payload.extend_from_slice(&event.to_bytes());
        }
        self.request(HostCommand::SendMidi, &payload)?;
        Ok(())
    }

    /// Open editor
    fn open_editor(&mut self) -> Result<RespEditorInfo> {
        let payload = self.request(HostCommand::OpenEditor, &[])?;
        RespEditorInfo::from_bytes(&payload)
            .ok_or_else(|| Error::Other("Invalid editor info response".to_string()))
    }

    /// Close editor
    fn close_editor(&mut self) -> Result<()> {
        self.request(HostCommand::CloseEditor, &[])?;
        Ok(())
    }

    /// Get parameter changes from GUI
    fn get_param_changes(&mut self) -> Result<Vec<protocol::ParamChangeEvent>> {
        let payload = self.request(HostCommand::GetParamChanges, &[])?;
        if payload.len() < 4 {
            return Ok(Vec::new());
        }

        let num_changes = u32::from_le_bytes([payload[0], payload[1], payload[2], payload[3]]) as usize;
        let mut changes = Vec::with_capacity(num_changes);

        // Each change is 12 bytes (4 bytes param_id + 8 bytes value)
        let mut offset = 4;
        for _ in 0..num_changes {
            if offset + 12 > payload.len() {
                break;
            }
            if let Some(change) = protocol::ParamChangeEvent::from_bytes(&payload[offset..]) {
                changes.push(change);
            }
            offset += 12;
        }

        Ok(changes)
    }

    /// Shutdown the host
    fn shutdown(&mut self) -> Result<()> {
        self.request(HostCommand::Shutdown, &[])?;
        Ok(())
    }
}

/// Scanner for Windows VST3 plugins via Wine
pub struct WineVst3Scanner {
    /// Path to Wine host executable
    host_exe: PathBuf,
    /// Optional Wine prefix
    wine_prefix: Option<PathBuf>,
}

impl WineVst3Scanner {
    /// Create a new scanner
    ///
    /// # Arguments
    /// * `host_exe` - Path to rack-wine-host.exe
    pub fn new(host_exe: impl AsRef<Path>) -> Self {
        Self {
            host_exe: host_exe.as_ref().to_path_buf(),
            wine_prefix: None,
        }
    }

    /// Set the Wine prefix (WINEPREFIX environment variable)
    pub fn with_wine_prefix(mut self, prefix: impl AsRef<Path>) -> Self {
        self.wine_prefix = Some(prefix.as_ref().to_path_buf());
        self
    }

    /// Find VST3 plugins in a directory
    fn find_vst3_in_dir(&self, dir: &Path, results: &mut Vec<PathBuf>) {
        if let Ok(entries) = std::fs::read_dir(dir) {
            for entry in entries.flatten() {
                let path = entry.path();
                if path.is_dir() {
                    if path.extension().map_or(false, |ext| ext == "vst3") {
                        results.push(path);
                    } else {
                        // Recurse into subdirectories
                        self.find_vst3_in_dir(&path, results);
                    }
                }
            }
        }
    }

    /// Spawn Wine host and connect
    fn spawn_host(&self) -> Result<(Child, WineClient)> {
        // Build wine command
        let mut cmd = Command::new("wine");
        cmd.arg(&self.host_exe);
        cmd.stdout(Stdio::piped());
        cmd.stderr(Stdio::piped());

        if let Some(ref prefix) = self.wine_prefix {
            cmd.env("WINEPREFIX", prefix);
        }

        // Spawn the host
        let child = cmd.spawn()
            .map_err(|e| Error::Other(format!("Failed to spawn Wine host: {}", e)))?;

        // Wait a bit for the host to start
        std::thread::sleep(Duration::from_millis(2000));

        // Try to connect to ports in range
        for port in RACK_WINE_PORT_BASE..=RACK_WINE_PORT_MAX {
            if let Ok(client) = WineClient::connect(port) {
                return Ok((child, client));
            }
        }

        Err(Error::Other("Failed to connect to Wine host".to_string()))
    }
}

impl PluginScanner for WineVst3Scanner {
    type Plugin = WineVst3Plugin;

    fn scan(&self) -> Result<Vec<PluginInfo>> {
        // Default Windows VST3 locations (via Wine Z: drive)
        let home = std::env::var("HOME").unwrap_or_default();
        let default_paths = vec![
            PathBuf::from(format!("{}/.wine/drive_c/Program Files/Common Files/VST3", home)),
            PathBuf::from(format!("{}/.wine/drive_c/Program Files (x86)/Common Files/VST3", home)),
        ];

        let mut results = Vec::new();
        for path in default_paths {
            if path.exists() {
                if let Ok(plugins) = self.scan_path(&path) {
                    results.extend(plugins);
                }
            }
        }
        Ok(results)
    }

    fn scan_path(&self, path: &Path) -> Result<Vec<PluginInfo>> {
        let mut vst3_paths = Vec::new();
        self.find_vst3_in_dir(path, &mut vst3_paths);

        let mut results = Vec::new();
        for vst3_path in vst3_paths {
            // For now, just create a basic PluginInfo from the path
            // Full scanning would require loading each plugin
            let name = vst3_path
                .file_stem()
                .and_then(|s| s.to_str())
                .unwrap_or("Unknown")
                .to_string();

            results.push(PluginInfo {
                name: name.clone(),
                manufacturer: "Unknown".to_string(),
                version: 0,
                plugin_type: PluginType::Effect,
                path: vst3_path,
                unique_id: name,
            });
        }

        Ok(results)
    }

    fn load(&self, info: &PluginInfo) -> Result<Self::Plugin> {
        WineVst3Plugin::new(&self.host_exe, &info.path, self.wine_prefix.as_deref())
    }
}

/// A Windows VST3 plugin loaded via Wine
pub struct WineVst3Plugin {
    /// Wine host process
    _host_process: Child,
    /// IPC client
    client: WineClient,
    /// Plugin info
    info: PluginInfo,
    /// Parameter IDs (indexed by parameter index)
    param_ids: Vec<u32>,
    /// Shared memory path
    shm_path: Option<String>,
    /// Shared memory file descriptor (Linux side)
    #[cfg(target_os = "linux")]
    shm_fd: Option<i32>,
    /// Shared memory pointer
    shm_ptr: Option<*mut u8>,
    /// Shared memory size
    shm_size: usize,
    /// Audio config
    sample_rate: f64,
    block_size: usize,
    num_inputs: usize,
    num_outputs: usize,
    /// Initialized flag
    initialized: bool,
}

// Safety: WineVst3Plugin is Send because:
// - The Wine host runs in a separate process
// - Communication is via TCP socket (Send)
// - Shared memory is process-safe
unsafe impl Send for WineVst3Plugin {}

impl WineVst3Plugin {
    /// Create a new Wine VST3 plugin instance
    pub fn new(host_exe: &Path, plugin_path: &Path, wine_prefix: Option<&Path>) -> Result<Self> {
        // Build wine command
        let mut cmd = Command::new("wine");
        cmd.arg(host_exe);
        cmd.stdout(Stdio::piped());
        cmd.stderr(Stdio::piped());

        if let Some(prefix) = wine_prefix {
            cmd.env("WINEPREFIX", prefix);
        }

        // Spawn the host
        let host_process = cmd.spawn()
            .map_err(|e| Error::Other(format!("Failed to spawn Wine host: {}", e)))?;

        // Wait for host to start
        std::thread::sleep(Duration::from_millis(2000));

        // Connect to host
        let mut client = None;
        for port in RACK_WINE_PORT_BASE..=RACK_WINE_PORT_MAX {
            if let Ok(c) = WineClient::connect(port) {
                client = Some(c);
                break;
            }
        }
        let mut client = client.ok_or_else(|| Error::Other("Failed to connect to Wine host".to_string()))?;

        // Ping to verify connection
        client.ping()?;

        // Convert path to Wine format (Z: drive for Linux paths)
        let wine_path = if plugin_path.starts_with("/") {
            format!("Z:{}", plugin_path.display())
        } else {
            plugin_path.display().to_string()
        };

        // Load the plugin
        client.load_plugin(&wine_path, 0)?;

        // Get plugin info
        let host_info = client.get_info()?;

        // Get parameter IDs
        let param_count = host_info.num_params;
        let mut param_ids = Vec::with_capacity(param_count as usize);
        for i in 0..param_count {
            if let Ok(info) = client.get_param_info(i) {
                param_ids.push(info.id);
            }
        }

        let info = PluginInfo {
            name: host_info.name,
            manufacturer: host_info.vendor,
            version: 0,
            plugin_type: PluginType::Effect, // TODO: detect from flags
            path: plugin_path.to_path_buf(),
            unique_id: host_info.uid,
        };

        Ok(Self {
            _host_process: host_process,
            client,
            info,
            param_ids,
            shm_path: None,
            #[cfg(target_os = "linux")]
            shm_fd: None,
            shm_ptr: None,
            shm_size: 0,
            sample_rate: 0.0,
            block_size: 0,
            num_inputs: host_info.num_audio_inputs as usize,
            num_outputs: host_info.num_audio_outputs as usize,
            initialized: false,
        })
    }

    /// Open the plugin editor
    pub fn open_editor(&mut self) -> Result<(u32, u32, u32)> {
        let info = self.client.open_editor()?;
        Ok((info.x11_window_id, info.width, info.height))
    }

    /// Close the plugin editor
    pub fn close_editor(&mut self) -> Result<()> {
        self.client.close_editor()
    }

    /// Get parameter changes from GUI since last poll
    ///
    /// Returns a list of (param_id, value) tuples for parameters that were
    /// changed by the user in the plugin GUI. Call this regularly (e.g., every
    /// audio buffer or on a timer) to stay in sync with GUI changes.
    pub fn get_param_changes(&mut self) -> Result<Vec<(u32, f64)>> {
        let changes = self.client.get_param_changes()?;
        Ok(changes.into_iter().map(|c| (c.param_id, c.value)).collect())
    }

    #[cfg(target_os = "linux")]
    fn setup_shared_memory(&mut self, block_size: usize, num_inputs: usize, num_outputs: usize) -> Result<String> {
        use std::os::unix::io::AsRawFd;

        // Generate unique shared memory name
        let counter = SHM_COUNTER.fetch_add(1, Ordering::SeqCst);
        let pid = std::process::id();
        let shm_name = format!("/tmp/rack-wine-audio-{}-{}", pid, counter);

        // Calculate size
        let header_size = ShmHeader::SIZE;
        let buffer_size = (num_inputs + num_outputs) * block_size * std::mem::size_of::<f32>();
        let total_size = header_size + buffer_size;

        // Create and map shared memory using a regular file
        let file = std::fs::OpenOptions::new()
            .read(true)
            .write(true)
            .create(true)
            .open(&shm_name)
            .map_err(|e| Error::Other(format!("Failed to create shared memory file: {}", e)))?;

        // Set size
        file.set_len(total_size as u64)
            .map_err(|e| Error::Other(format!("Failed to set shared memory size: {}", e)))?;

        // Memory map
        let ptr = unsafe {
            libc::mmap(
                std::ptr::null_mut(),
                total_size,
                libc::PROT_READ | libc::PROT_WRITE,
                libc::MAP_SHARED,
                file.as_raw_fd(),
                0,
            )
        };

        if ptr == libc::MAP_FAILED {
            return Err(Error::Other("Failed to mmap shared memory".to_string()));
        }

        // Initialize header
        let header = ptr as *mut ShmHeader;
        unsafe {
            (*header).magic = RACK_WINE_SHM_MAGIC;
            (*header).version = RACK_WINE_PROTOCOL_VERSION;
            (*header).num_inputs = num_inputs as u32;
            (*header).num_outputs = num_outputs as u32;
            (*header).block_size = block_size as u32;
            (*header).sample_rate = self.sample_rate as u32;
            (*header).host_ready = 0;
            (*header).client_ready = 0;
            (*header).input_offset = header_size as u32;
            (*header).output_offset = (header_size + num_inputs * block_size * std::mem::size_of::<f32>()) as u32;
        }

        self.shm_fd = Some(file.as_raw_fd());
        self.shm_ptr = Some(ptr as *mut u8);
        self.shm_size = total_size;
        self.shm_path = Some(shm_name.clone());

        // Don't close the file - keep it open for the fd
        std::mem::forget(file);

        Ok(shm_name)
    }

    #[cfg(not(target_os = "linux"))]
    fn setup_shared_memory(&mut self, _block_size: usize, _num_inputs: usize, _num_outputs: usize) -> Result<String> {
        Err(Error::Other("Wine VST3 host only supported on Linux".to_string()))
    }
}

impl Drop for WineVst3Plugin {
    fn drop(&mut self) {
        // Try to shutdown gracefully
        let _ = self.client.shutdown();

        // Cleanup shared memory
        #[cfg(target_os = "linux")]
        if let Some(ptr) = self.shm_ptr {
            unsafe {
                libc::munmap(ptr as *mut libc::c_void, self.shm_size);
            }
        }

        #[cfg(target_os = "linux")]
        if let Some(path) = &self.shm_path {
            let _ = std::fs::remove_file(path);
        }
    }
}

impl PluginInstance for WineVst3Plugin {
    fn initialize(&mut self, sample_rate: f64, max_block_size: usize) -> Result<()> {
        self.sample_rate = sample_rate;
        self.block_size = max_block_size;

        // Setup shared memory
        let shm_name = self.setup_shared_memory(max_block_size, self.num_inputs, self.num_outputs)?;

        // Convert path for Wine (Z: drive prefix)
        let wine_shm_name = format!("Z:{}", shm_name);

        // Initialize audio on host
        self.client.init_audio(
            sample_rate as u32,
            max_block_size as u32,
            self.num_inputs as u32,
            self.num_outputs as u32,
            &wine_shm_name,
        )?;

        self.initialized = true;
        Ok(())
    }

    fn reset(&mut self) -> Result<()> {
        // No explicit reset command in the protocol
        // Could reinitialize if needed
        Ok(())
    }

    fn process(
        &mut self,
        inputs: &[&[f32]],
        outputs: &mut [&mut [f32]],
        num_frames: usize,
    ) -> Result<()> {
        if !self.initialized {
            return Err(Error::NotInitialized);
        }

        let shm_ptr = self.shm_ptr.ok_or(Error::NotInitialized)?;

        // Read header to get offsets
        let header = unsafe { &*(shm_ptr as *const ShmHeader) };
        let input_offset = header.input_offset as usize;
        let output_offset = header.output_offset as usize;
        let block_size = header.block_size as usize;

        // Copy input data to shared memory
        for (ch, input) in inputs.iter().enumerate() {
            if ch < self.num_inputs {
                let dest_offset = input_offset + ch * block_size * std::mem::size_of::<f32>();
                let dest = unsafe {
                    std::slice::from_raw_parts_mut(
                        shm_ptr.add(dest_offset) as *mut f32,
                        num_frames,
                    )
                };
                let copy_len = num_frames.min(input.len());
                dest[..copy_len].copy_from_slice(&input[..copy_len]);
            }
        }

        // Process
        self.client.process_audio(num_frames as u32)?;

        // Copy output data from shared memory
        for (ch, output) in outputs.iter_mut().enumerate() {
            if ch < self.num_outputs {
                let src_offset = output_offset + ch * block_size * std::mem::size_of::<f32>();
                let src = unsafe {
                    std::slice::from_raw_parts(
                        shm_ptr.add(src_offset) as *const f32,
                        num_frames,
                    )
                };
                let copy_len = num_frames.min(output.len());
                output[..copy_len].copy_from_slice(&src[..copy_len]);
            }
        }

        Ok(())
    }

    fn parameter_count(&self) -> usize {
        self.param_ids.len()
    }

    fn parameter_info(&self, index: usize) -> Result<ParameterInfo> {
        if index >= self.param_ids.len() {
            return Err(Error::InvalidParameter(index));
        }

        // We'd need to cache this or query the host
        // For now, return basic info
        Ok(ParameterInfo {
            index,
            name: format!("Parameter {}", index),
            min: 0.0,
            max: 1.0,
            default: 0.5,
            unit: String::new(),
        })
    }

    fn get_parameter(&self, index: usize) -> Result<f32> {
        if index >= self.param_ids.len() {
            return Err(Error::InvalidParameter(index));
        }
        // Need mutable client - this is a design issue
        // For now, we can't call this without &mut self
        Err(Error::Other("get_parameter requires mutable reference".to_string()))
    }

    fn set_parameter(&mut self, index: usize, value: f32) -> Result<()> {
        if index >= self.param_ids.len() {
            return Err(Error::InvalidParameter(index));
        }
        let param_id = self.param_ids[index];
        self.client.set_param(param_id, value as f64)
    }

    fn send_midi(&mut self, events: &[MidiEvent]) -> Result<()> {
        let midi_events: Vec<protocol::MidiEvent> = events.iter().map(|e| {
            let (status, data1, data2) = match e.kind {
                crate::MidiEventKind::NoteOn { note, velocity, channel } => {
                    (0x90 | (channel & 0x0F), note, velocity)
                }
                crate::MidiEventKind::NoteOff { note, velocity, channel } => {
                    (0x80 | (channel & 0x0F), note, velocity)
                }
                crate::MidiEventKind::ControlChange { controller, value, channel } => {
                    (0xB0 | (channel & 0x0F), controller, value)
                }
                crate::MidiEventKind::ProgramChange { program, channel } => {
                    (0xC0 | (channel & 0x0F), program, 0)
                }
                crate::MidiEventKind::PitchBend { value, channel } => {
                    let lsb = (value & 0x7F) as u8;
                    let msb = ((value >> 7) & 0x7F) as u8;
                    (0xE0 | (channel & 0x0F), lsb, msb)
                }
                crate::MidiEventKind::PolyphonicAftertouch { note, pressure, channel } => {
                    (0xA0 | (channel & 0x0F), note, pressure)
                }
                crate::MidiEventKind::ChannelAftertouch { pressure, channel } => {
                    (0xD0 | (channel & 0x0F), pressure, 0)
                }
                _ => (0, 0, 0), // Ignore system real-time messages
            };
            protocol::MidiEvent::new(e.sample_offset, status, data1, data2)
        }).collect();

        self.client.send_midi(&midi_events)
    }

    fn preset_count(&self) -> Result<usize> {
        // Not implemented in protocol yet
        Ok(0)
    }

    fn preset_info(&self, _index: usize) -> Result<PresetInfo> {
        Err(Error::Other("Presets not implemented".to_string()))
    }

    fn load_preset(&mut self, _preset_number: i32) -> Result<()> {
        Err(Error::Other("Presets not implemented".to_string()))
    }

    fn get_state(&self) -> Result<Vec<u8>> {
        // Not implemented in protocol yet
        Err(Error::Other("State serialization not implemented".to_string()))
    }

    fn set_state(&mut self, _data: &[u8]) -> Result<()> {
        Err(Error::Other("State serialization not implemented".to_string()))
    }

    fn info(&self) -> &PluginInfo {
        &self.info
    }

    fn is_initialized(&self) -> bool {
        self.initialized
    }
}
