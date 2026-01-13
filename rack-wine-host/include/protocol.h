// rack-wine-host protocol definition
// Shared between Wine host and Linux client

#ifndef RACK_WINE_PROTOCOL_H
#define RACK_WINE_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Protocol version
#define RACK_WINE_PROTOCOL_VERSION 1

// Message types (client -> host)
enum RackWineCommand {
    CMD_PING = 1,           // Check if host is alive
    CMD_LOAD_PLUGIN = 2,    // Load a VST3 plugin
    CMD_UNLOAD_PLUGIN = 3,  // Unload current plugin
    CMD_GET_INFO = 4,       // Get plugin info
    CMD_INIT = 5,           // Initialize plugin (sample rate, block size)
    CMD_PROCESS = 6,        // Process audio block
    CMD_GET_PARAM_COUNT = 7,
    CMD_GET_PARAM_INFO = 8,
    CMD_GET_PARAM = 9,      // Get parameter value
    CMD_SET_PARAM = 10,     // Set parameter value
    CMD_SEND_MIDI = 11,     // Send MIDI events
    CMD_GET_STATE = 12,     // Get plugin state
    CMD_SET_STATE = 13,     // Set plugin state
    CMD_OPEN_EDITOR = 14,   // Open plugin GUI editor
    CMD_CLOSE_EDITOR = 15,  // Close plugin GUI editor
    CMD_GET_EDITOR_SIZE = 16, // Get editor window size
    CMD_GET_PARAM_CHANGES = 17, // Get parameter changes from GUI
    CMD_SHUTDOWN = 99,      // Shutdown host
};

// Response status
enum RackWineStatus {
    STATUS_OK = 0,
    STATUS_ERROR = 1,
    STATUS_NOT_LOADED = 2,
    STATUS_NOT_INITIALIZED = 3,
    STATUS_INVALID_PARAM = 4,
};

// Message header (all messages start with this)
#pragma pack(push, 1)

typedef struct {
    uint32_t magic;         // 'RWNH' = 0x484E5752
    uint32_t version;       // Protocol version
    uint32_t command;       // RackWineCommand
    uint32_t payload_size;  // Size of payload after header
} RackWineHeader;

#define RACK_WINE_MAGIC 0x484E5752  // 'RWNH' in little-endian

// Response header
typedef struct {
    uint32_t magic;         // 'RWNR' = 0x524E5752
    uint32_t status;        // RackWineStatus
    uint32_t payload_size;  // Size of payload after header
} RackWineResponse;

#define RACK_WINE_RESPONSE_MAGIC 0x524E5752  // 'RWNR'

// CMD_LOAD_PLUGIN payload
typedef struct {
    char path[1024];        // Path to .vst3 bundle
    uint32_t class_index;   // Which class to instantiate (usually 0)
} CmdLoadPlugin;

// CMD_INIT payload
typedef struct {
    double sample_rate;
    uint32_t max_block_size;
} CmdInit;

// CMD_GET_PARAM / CMD_SET_PARAM payload
typedef struct {
    uint32_t param_id;
    double value;           // Normalized 0.0-1.0
} CmdParam;

// Response: Plugin info (for CMD_GET_INFO)
typedef struct {
    char name[256];
    char vendor[256];
    char category[128];
    char uid[64];
    uint32_t num_params;
    uint32_t num_audio_inputs;
    uint32_t num_audio_outputs;
    uint32_t flags;         // Instrument, effect, etc.
} RespPluginInfo;

// Response: Parameter info (for CMD_GET_PARAM_INFO)
typedef struct {
    uint32_t id;
    char name[128];
    char units[32];
    double default_value;
    double min_value;
    double max_value;
    uint32_t flags;         // Automatable, etc.
} RespParamInfo;

// CMD_PROCESS uses shared memory for audio buffers
// This struct just signals processing
typedef struct {
    uint32_t num_samples;
    uint32_t shm_offset_in;   // Offset in shared memory for input
    uint32_t shm_offset_out;  // Offset in shared memory for output
} CmdProcess;

// CMD_SEND_MIDI payload
typedef struct {
    uint32_t num_events;
    // Followed by num_events * MidiEvent
} CmdMidi;

typedef struct {
    uint32_t sample_offset;
    uint8_t data[4];        // MIDI bytes (status, data1, data2, 0)
} MidiEvent;

// Response: Editor info (for CMD_OPEN_EDITOR)
typedef struct {
    uint32_t x11_window_id; // X11 window ID for embedding (0 if failed)
    uint32_t width;         // Editor width in pixels
    uint32_t height;        // Editor height in pixels
} RespEditorInfo;

// Response: Editor size (for CMD_GET_EDITOR_SIZE)
typedef struct {
    uint32_t width;
    uint32_t height;
} RespEditorSize;

// Response: Parameter changes from GUI (for CMD_GET_PARAM_CHANGES)
// Returns array of parameter changes since last poll
typedef struct {
    uint32_t param_id;
    double value;           // Normalized 0.0-1.0
} ParamChangeEvent;

typedef struct {
    uint32_t num_changes;
    // Followed by num_changes * ParamChangeEvent
} RespParamChanges;

#pragma pack(pop)

// TCP port range for Wine host (picks first available)
#define RACK_WINE_PORT_BASE 47100
#define RACK_WINE_PORT_MAX  47199

// ============================================================================
// Shared Memory for Audio
// ============================================================================

// Shared memory name template - %d is replaced with host PID
#define RACK_WINE_SHM_NAME "/rack-wine-audio-%d"

// Maximum supported configuration
#define RACK_WINE_MAX_CHANNELS 8
#define RACK_WINE_MAX_BLOCK_SIZE 4096

// Shared memory header
typedef struct {
    uint32_t magic;              // RACK_WINE_SHM_MAGIC
    uint32_t version;            // Protocol version
    uint32_t num_inputs;         // Number of input channels
    uint32_t num_outputs;        // Number of output channels
    uint32_t block_size;         // Current block size
    uint32_t sample_rate;        // Sample rate (as uint32)

    // Synchronization
    volatile uint32_t host_ready;    // Host has processed, output ready
    volatile uint32_t client_ready;  // Client has written input, ready to process

    // Buffer offsets (from start of shared memory)
    uint32_t input_offset;       // Offset to input buffers
    uint32_t output_offset;      // Offset to output buffers

    // Reserved for future use
    uint32_t reserved[4];
} RackWineShmHeader;

#define RACK_WINE_SHM_MAGIC 0x52574153  // 'RWAS' - Rack Wine Audio Shm

// Calculate shared memory size needed
// Layout: [Header][Input ch0][Input ch1]...[Output ch0][Output ch1]...
#define RACK_WINE_SHM_SIZE(num_in, num_out, block_size) \
    (sizeof(RackWineShmHeader) + \
     ((num_in) + (num_out)) * (block_size) * sizeof(float))

// CMD_INIT_AUDIO payload - initialize audio processing
typedef struct {
    uint32_t sample_rate;
    uint32_t block_size;
    uint32_t num_inputs;
    uint32_t num_outputs;
    char shm_name[64];           // Shared memory name
} CmdInitAudio;

// CMD_PROCESS_AUDIO payload - trigger processing
typedef struct {
    uint32_t num_samples;        // Samples to process (may be < block_size)
} CmdProcessAudio;

// Add new commands
#define CMD_INIT_AUDIO    20     // Initialize audio with shared memory
#define CMD_PROCESS_AUDIO 21     // Process audio block

#ifdef __cplusplus
}
#endif

#endif // RACK_WINE_PROTOCOL_H
