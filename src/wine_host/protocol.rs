//! Protocol definitions for Wine host communication
//!
//! This module mirrors the C protocol.h definitions for IPC with the Wine host.

/// Protocol magic numbers
pub const RACK_WINE_MAGIC: u32 = 0x484E5752; // 'RWNH' in little-endian
pub const RACK_WINE_RESPONSE_MAGIC: u32 = 0x524E5752; // 'RWNR'

/// Protocol version
pub const RACK_WINE_PROTOCOL_VERSION: u32 = 1;

/// TCP port range for Wine host
pub const RACK_WINE_PORT_BASE: u16 = 47100;
pub const RACK_WINE_PORT_MAX: u16 = 47199;

/// Command types (named HostCommand to avoid conflict with std::process::Command)
#[repr(u32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum HostCommand {
    Ping = 1,
    LoadPlugin = 2,
    UnloadPlugin = 3,
    GetInfo = 4,
    Init = 5,
    Process = 6,
    GetParamCount = 7,
    GetParamInfo = 8,
    GetParam = 9,
    SetParam = 10,
    SendMidi = 11,
    GetState = 12,
    SetState = 13,
    OpenEditor = 14,
    CloseEditor = 15,
    GetEditorSize = 16,
    InitAudio = 20,
    ProcessAudio = 21,
    Shutdown = 99,
}

/// Response status codes
#[repr(u32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Status {
    Ok = 0,
    Error = 1,
    NotLoaded = 2,
    NotInitialized = 3,
    InvalidParam = 4,
}

impl From<u32> for Status {
    fn from(value: u32) -> Self {
        match value {
            0 => Status::Ok,
            1 => Status::Error,
            2 => Status::NotLoaded,
            3 => Status::NotInitialized,
            4 => Status::InvalidParam,
            _ => Status::Error,
        }
    }
}

/// Message header (all messages start with this)
#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct Header {
    pub magic: u32,
    pub version: u32,
    pub command: u32,
    pub payload_size: u32,
}

impl Header {
    pub fn new(command: HostCommand, payload_size: u32) -> Self {
        Self {
            magic: RACK_WINE_MAGIC,
            version: RACK_WINE_PROTOCOL_VERSION,
            command: command as u32,
            payload_size,
        }
    }

    pub fn to_bytes(&self) -> [u8; 16] {
        let mut buf = [0u8; 16];
        buf[0..4].copy_from_slice(&self.magic.to_le_bytes());
        buf[4..8].copy_from_slice(&self.version.to_le_bytes());
        buf[8..12].copy_from_slice(&self.command.to_le_bytes());
        buf[12..16].copy_from_slice(&self.payload_size.to_le_bytes());
        buf
    }

    pub fn from_bytes(buf: &[u8; 16]) -> Self {
        Self {
            magic: u32::from_le_bytes([buf[0], buf[1], buf[2], buf[3]]),
            version: u32::from_le_bytes([buf[4], buf[5], buf[6], buf[7]]),
            command: u32::from_le_bytes([buf[8], buf[9], buf[10], buf[11]]),
            payload_size: u32::from_le_bytes([buf[12], buf[13], buf[14], buf[15]]),
        }
    }
}

/// Response header
#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct ResponseHeader {
    pub magic: u32,
    pub status: u32,
    pub payload_size: u32,
}

impl ResponseHeader {
    pub fn from_bytes(buf: &[u8; 12]) -> Self {
        Self {
            magic: u32::from_le_bytes([buf[0], buf[1], buf[2], buf[3]]),
            status: u32::from_le_bytes([buf[4], buf[5], buf[6], buf[7]]),
            payload_size: u32::from_le_bytes([buf[8], buf[9], buf[10], buf[11]]),
        }
    }

    pub fn status(&self) -> Status {
        Status::from(self.status)
    }
}

/// CMD_LOAD_PLUGIN payload
#[repr(C, packed)]
pub struct CmdLoadPlugin {
    pub path: [u8; 1024],
    pub class_index: u32,
}

impl CmdLoadPlugin {
    pub fn new(path: &str, class_index: u32) -> Self {
        let mut cmd = Self {
            path: [0u8; 1024],
            class_index,
        };
        let bytes = path.as_bytes();
        let len = bytes.len().min(1023);
        cmd.path[..len].copy_from_slice(&bytes[..len]);
        cmd
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        let mut buf = Vec::with_capacity(1028);
        buf.extend_from_slice(&self.path);
        buf.extend_from_slice(&self.class_index.to_le_bytes());
        buf
    }
}

/// CMD_INIT_AUDIO payload
#[repr(C, packed)]
pub struct CmdInitAudio {
    pub sample_rate: u32,
    pub block_size: u32,
    pub num_inputs: u32,
    pub num_outputs: u32,
    pub shm_name: [u8; 64],
}

impl CmdInitAudio {
    pub fn new(sample_rate: u32, block_size: u32, num_inputs: u32, num_outputs: u32, shm_name: &str) -> Self {
        let mut cmd = Self {
            sample_rate,
            block_size,
            num_inputs,
            num_outputs,
            shm_name: [0u8; 64],
        };
        let bytes = shm_name.as_bytes();
        let len = bytes.len().min(63);
        cmd.shm_name[..len].copy_from_slice(&bytes[..len]);
        cmd
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        let mut buf = Vec::with_capacity(80);
        buf.extend_from_slice(&self.sample_rate.to_le_bytes());
        buf.extend_from_slice(&self.block_size.to_le_bytes());
        buf.extend_from_slice(&self.num_inputs.to_le_bytes());
        buf.extend_from_slice(&self.num_outputs.to_le_bytes());
        buf.extend_from_slice(&self.shm_name);
        buf
    }
}

/// CMD_PROCESS_AUDIO payload
#[repr(C, packed)]
pub struct CmdProcessAudio {
    pub num_samples: u32,
}

impl CmdProcessAudio {
    pub fn new(num_samples: u32) -> Self {
        Self { num_samples }
    }

    pub fn to_bytes(&self) -> [u8; 4] {
        self.num_samples.to_le_bytes()
    }
}

/// CMD_GET_PARAM / CMD_SET_PARAM payload
#[repr(C, packed)]
pub struct CmdParam {
    pub param_id: u32,
    pub value: f64,
}

impl CmdParam {
    pub fn new(param_id: u32, value: f64) -> Self {
        Self { param_id, value }
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        let mut buf = Vec::with_capacity(12);
        buf.extend_from_slice(&self.param_id.to_le_bytes());
        buf.extend_from_slice(&self.value.to_le_bytes());
        buf
    }
}

/// Response: Plugin info
#[derive(Debug, Clone, Default)]
pub struct RespPluginInfo {
    pub name: String,
    pub vendor: String,
    pub category: String,
    pub uid: String,
    pub num_params: u32,
    pub num_audio_inputs: u32,
    pub num_audio_outputs: u32,
    pub flags: u32,
}

impl RespPluginInfo {
    pub fn from_bytes(buf: &[u8]) -> Option<Self> {
        if buf.len() < 716 {
            return None;
        }

        fn read_string(data: &[u8], max_len: usize) -> String {
            let end = data.iter().position(|&b| b == 0).unwrap_or(max_len);
            String::from_utf8_lossy(&data[..end]).to_string()
        }

        Some(Self {
            name: read_string(&buf[0..256], 256),
            vendor: read_string(&buf[256..512], 256),
            category: read_string(&buf[512..640], 128),
            uid: read_string(&buf[640..704], 64),
            num_params: u32::from_le_bytes([buf[704], buf[705], buf[706], buf[707]]),
            num_audio_inputs: u32::from_le_bytes([buf[708], buf[709], buf[710], buf[711]]),
            num_audio_outputs: u32::from_le_bytes([buf[712], buf[713], buf[714], buf[715]]),
            flags: if buf.len() >= 720 {
                u32::from_le_bytes([buf[716], buf[717], buf[718], buf[719]])
            } else {
                0
            },
        })
    }
}

/// Response: Parameter info
#[derive(Debug, Clone)]
pub struct RespParamInfo {
    pub id: u32,
    pub name: String,
    pub units: String,
    pub default_value: f64,
    pub min_value: f64,
    pub max_value: f64,
    pub flags: u32,
}

impl RespParamInfo {
    pub fn from_bytes(buf: &[u8]) -> Option<Self> {
        if buf.len() < 196 {
            return None;
        }

        fn read_string(data: &[u8], max_len: usize) -> String {
            let end = data.iter().position(|&b| b == 0).unwrap_or(max_len);
            String::from_utf8_lossy(&data[..end]).to_string()
        }

        Some(Self {
            id: u32::from_le_bytes([buf[0], buf[1], buf[2], buf[3]]),
            name: read_string(&buf[4..132], 128),
            units: read_string(&buf[132..164], 32),
            default_value: f64::from_le_bytes([
                buf[164], buf[165], buf[166], buf[167],
                buf[168], buf[169], buf[170], buf[171],
            ]),
            min_value: f64::from_le_bytes([
                buf[172], buf[173], buf[174], buf[175],
                buf[176], buf[177], buf[178], buf[179],
            ]),
            max_value: f64::from_le_bytes([
                buf[180], buf[181], buf[182], buf[183],
                buf[184], buf[185], buf[186], buf[187],
            ]),
            flags: u32::from_le_bytes([buf[188], buf[189], buf[190], buf[191]]),
        })
    }
}

/// Response: Editor info
#[derive(Debug, Clone, Default)]
pub struct RespEditorInfo {
    pub x11_window_id: u32,
    pub width: u32,
    pub height: u32,
}

impl RespEditorInfo {
    pub fn from_bytes(buf: &[u8]) -> Option<Self> {
        if buf.len() < 12 {
            return None;
        }
        Some(Self {
            x11_window_id: u32::from_le_bytes([buf[0], buf[1], buf[2], buf[3]]),
            width: u32::from_le_bytes([buf[4], buf[5], buf[6], buf[7]]),
            height: u32::from_le_bytes([buf[8], buf[9], buf[10], buf[11]]),
        })
    }
}

/// MIDI event for sending to host
#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
pub struct MidiEvent {
    pub sample_offset: u32,
    pub data: [u8; 4],
}

impl MidiEvent {
    pub fn new(sample_offset: u32, status: u8, data1: u8, data2: u8) -> Self {
        Self {
            sample_offset,
            data: [status, data1, data2, 0],
        }
    }

    pub fn to_bytes(&self) -> [u8; 8] {
        let mut buf = [0u8; 8];
        buf[0..4].copy_from_slice(&self.sample_offset.to_le_bytes());
        buf[4..8].copy_from_slice(&self.data);
        buf
    }
}

/// CMD_SEND_MIDI header
pub struct CmdMidi {
    pub num_events: u32,
}

impl CmdMidi {
    pub fn to_bytes(&self) -> [u8; 4] {
        self.num_events.to_le_bytes()
    }
}

/// Shared memory header
#[repr(C)]
pub struct ShmHeader {
    pub magic: u32,
    pub version: u32,
    pub num_inputs: u32,
    pub num_outputs: u32,
    pub block_size: u32,
    pub sample_rate: u32,
    pub host_ready: u32,
    pub client_ready: u32,
    pub input_offset: u32,
    pub output_offset: u32,
    pub reserved: [u32; 4],
}

pub const RACK_WINE_SHM_MAGIC: u32 = 0x52574153; // 'RWAS'

impl ShmHeader {
    pub const SIZE: usize = std::mem::size_of::<Self>();
}
