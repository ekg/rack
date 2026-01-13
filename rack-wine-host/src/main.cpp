// rack-wine-host: Windows VST3 plugin host for Wine
// Phase 3: Shared memory audio processing

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "../include/protocol.h"

// ============================================================================
// VST3 Types and Interfaces
// ============================================================================

namespace Steinberg {

typedef int8_t int8;
typedef uint8_t uint8;
typedef int16_t int16;
typedef uint16_t uint16;
typedef int32_t int32;
typedef uint32_t uint32;
typedef int64_t int64;
typedef uint64_t uint64;
typedef int32 tresult;
typedef char char8;
typedef char16_t char16;

struct TUID {
    uint8 data[16];
};

inline bool operator==(const TUID& a, const TUID& b) {
    return memcmp(a.data, b.data, 16) == 0;
}

class FUnknown {
public:
    virtual tresult queryInterface(const TUID& iid, void** obj) = 0;
    virtual uint32 addRef() = 0;
    virtual uint32 release() = 0;
};

enum { kResultOk = 0, kResultFalse = 1, kNoInterface = -1, kNotImplemented = -2 };

struct PClassInfo {
    TUID cid;
    int32 cardinality;
    char8 category[32];
    char8 name[64];
};

struct PClassInfo2 {
    TUID cid;
    int32 cardinality;
    char8 category[32];
    char8 name[64];
    uint32 classFlags;
    char8 subCategories[128];
    char8 vendor[64];
    char8 version[64];
    char8 sdkVersion[64];
};

struct PFactoryInfo {
    char8 vendor[64];
    char8 url[256];
    char8 email[128];
    int32 flags;
};

// Interface IDs - stored in COM GUID format for Windows
// COM format: Data1 (LE 32-bit), Data2 (LE 16-bit), Data3 (LE 16-bit), Data4 (8 bytes big-endian)

// IPluginFactory {7A4D811C-5211-4A1F-AED9-D2EE0B43BF9F}
// COM bytes: 1C 81 4D 7A  11 52  1F 4A  AE D9 D2 EE 0B 43 BF 9F
static const TUID IPluginFactory_iid = {
    0x1C, 0x81, 0x4D, 0x7A, 0x11, 0x52, 0x1F, 0x4A,
    0xAE, 0xD9, 0xD2, 0xEE, 0x0B, 0x43, 0xBF, 0x9F
};

// IPluginFactory2 {0007B650-F24B-4C0B-A464-EDB9F00B2ABB}
// COM bytes: 50 B6 07 00  4B F2  0B 4C  A4 64 ED B9 F0 0B 2A BB
static const TUID IPluginFactory2_iid = {
    0x50, 0xB6, 0x07, 0x00, 0x4B, 0xF2, 0x0B, 0x4C,
    0xA4, 0x64, 0xED, 0xB9, 0xF0, 0x0B, 0x2A, 0xBB
};

// IComponent {E831FF31-F2D5-4301-928E-BBEE25697802}
// COM bytes: 31 FF 31 E8  D5 F2  01 43  92 8E BB EE 25 69 78 02
static const TUID IComponent_iid = {
    0x31, 0xFF, 0x31, 0xE8, 0xD5, 0xF2, 0x01, 0x43,
    0x92, 0x8E, 0xBB, 0xEE, 0x25, 0x69, 0x78, 0x02
};

// IAudioProcessor {42043F99-B7DA-453C-A569-E79D9AAEC33D}
// COM bytes: 99 3F 04 42  DA B7  3C 45  A5 69 E7 9D 9A AE C3 3D
static const TUID IAudioProcessor_iid = {
    0x99, 0x3F, 0x04, 0x42, 0xDA, 0xB7, 0x3C, 0x45,
    0xA5, 0x69, 0xE7, 0x9D, 0x9A, 0xAE, 0xC3, 0x3D
};

// IEditController {DCD7BBE3-7742-448D-A874-AACC979C759E}
// COM bytes: E3 BB D7 DC  42 77  8D 44  A8 74 AA CC 97 9C 75 9E
static const TUID IEditController_iid = {
    0xE3, 0xBB, 0xD7, 0xDC, 0x42, 0x77, 0x8D, 0x44,
    0xA8, 0x74, 0xAA, 0xCC, 0x97, 0x9C, 0x75, 0x9E
};

// FUnknown {00000000-0000-0000-C000-000000000046}
// COM bytes: 00 00 00 00  00 00  00 00  C0 00 00 00 00 00 00 46
static const TUID FUnknown_iid = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46
};

class IPluginFactory : public FUnknown {
public:
    virtual tresult getFactoryInfo(PFactoryInfo* info) = 0;
    virtual int32 countClasses() = 0;
    virtual tresult getClassInfo(int32 index, PClassInfo* info) = 0;
    virtual tresult createInstance(const TUID& cid, const TUID& iid, void** obj) = 0;
};

class IPluginFactory2 : public IPluginFactory {
public:
    virtual tresult getClassInfo2(int32 index, PClassInfo2* info) = 0;
};

// IPluginBase
class IPluginBase : public FUnknown {
public:
    virtual tresult initialize(FUnknown* context) = 0;
    virtual tresult terminate() = 0;
};

// Bus types and directions
enum MediaType { kAudio = 0, kEvent = 1 };
enum BusDirection { kInput = 0, kOutput = 1 };
enum BusType { kMain = 0, kAux = 1 };

struct BusInfo {
    MediaType mediaType;
    BusDirection direction;
    int32 channelCount;
    char16 name[128];
    BusType busType;
    uint32 flags;
};

// IComponent
class IComponent : public IPluginBase {
public:
    virtual tresult getControllerClassId(TUID& classId) = 0;
    virtual tresult setIoMode(int32 mode) = 0;
    virtual int32 getBusCount(MediaType type, BusDirection dir) = 0;
    virtual tresult getBusInfo(MediaType type, BusDirection dir, int32 index, BusInfo& bus) = 0;
    virtual tresult getRoutingInfo(void* inInfo, void* outInfo) = 0;
    virtual tresult activateBus(MediaType type, BusDirection dir, int32 index, uint8 state) = 0;
    virtual tresult setActive(uint8 state) = 0;
    virtual tresult setState(void* state) = 0;
    virtual tresult getState(void* state) = 0;
};

// Speaker arrangements
typedef uint64 SpeakerArrangement;
static const SpeakerArrangement kStereo = 0x3;  // L + R

// Process setup
struct ProcessSetup {
    int32 processMode;      // 0 = realtime, 1 = prefetch, 2 = offline
    int32 symbolicSampleSize; // 0 = 32-bit float, 1 = 64-bit double
    int32 maxSamplesPerBlock;
    double sampleRate;
};

// Audio bus buffers
struct AudioBusBuffers {
    int32 numChannels;
    uint64 silenceFlags;
    union {
        float** channelBuffers32;
        double** channelBuffers64;
    };
};

// Forward declarations
class IEventList;

// Process data
struct ProcessData {
    int32 processMode;
    int32 symbolicSampleSize;
    int32 numSamples;
    int32 numInputs;
    int32 numOutputs;
    AudioBusBuffers* inputs;
    AudioBusBuffers* outputs;
    void* inputParameterChanges;
    void* outputParameterChanges;
    IEventList* inputEvents;
    IEventList* outputEvents;
    void* processContext;
};

// ============================================================================
// VST3 Event structures for MIDI
// ============================================================================

// Note-on event
struct NoteOnEvent {
    int16 channel;      // channel index in event bus
    int16 pitch;        // range [0, 127]
    float tuning;       // 1.f = +1 cent
    float velocity;     // range [0.0, 1.0]
    int32 length;       // in sample frames
    int32 noteId;       // note identifier (-1 if not available)
};

// Note-off event
struct NoteOffEvent {
    int16 channel;
    int16 pitch;
    float velocity;
    int32 noteId;
    float tuning;
};

// Poly pressure event
struct PolyPressureEvent {
    int16 channel;
    int16 pitch;
    float pressure;     // range [0.0, 1.0]
    int32 noteId;
};

// Legacy MIDI CC event
struct LegacyMIDICCOutEvent {
    uint8 controlNumber;
    int8 channel;
    int8 value;
    int8 value2;        // for pitch bend
};

// Event types
enum EventTypes {
    kNoteOnEvent = 0,
    kNoteOffEvent = 1,
    kDataEvent = 2,
    kPolyPressureEvent = 3,
    kLegacyMIDICCOutEvent = 65535
};

// Event flags
enum EventFlags {
    kIsLive = 1 << 0
};

// Event structure (union of all event types)
struct Event {
    int32 busIndex;         // event bus index
    int32 sampleOffset;     // sample frames offset
    double ppqPosition;     // position in project
    uint16 flags;           // EventFlags
    uint16 type;            // EventTypes

    union {
        NoteOnEvent noteOn;
        NoteOffEvent noteOff;
        PolyPressureEvent polyPressure;
        LegacyMIDICCOutEvent midiCCOut;
    };
};

// IEventList {3A2C4214-3463-49FE-B2C4-F397B9695A44}
// COM bytes: 14 42 2C 3A  63 34  FE 49  B2 C4 F3 97 B9 69 5A 44
static const TUID IEventList_iid = {
    0x14, 0x42, 0x2C, 0x3A, 0x63, 0x34, 0xFE, 0x49,
    0xB2, 0xC4, 0xF3, 0x97, 0xB9, 0x69, 0x5A, 0x44
};

class IEventList : public FUnknown {
public:
    virtual int32 getEventCount() = 0;
    virtual tresult getEvent(int32 index, Event& e) = 0;
    virtual tresult addEvent(Event& e) = 0;
};

// Simple EventList implementation for host
#define MAX_EVENTS 256

class HostEventList : public IEventList {
public:
    Event events[MAX_EVENTS];
    int32 count = 0;

    // FUnknown
    tresult queryInterface(const TUID&, void** obj) override {
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 addRef() override { return 1; }
    uint32 release() override { return 1; }

    // IEventList
    int32 getEventCount() override { return count; }

    tresult getEvent(int32 index, Event& e) override {
        if (index < 0 || index >= count) return kResultFalse;
        e = events[index];
        return kResultOk;
    }

    tresult addEvent(Event& e) override {
        if (count >= MAX_EVENTS) return kResultFalse;
        events[count++] = e;
        return kResultOk;
    }

    void clear() { count = 0; }
};

// IAudioProcessor
class IAudioProcessor : public FUnknown {
public:
    virtual tresult setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
                                       SpeakerArrangement* outputs, int32 numOuts) = 0;
    virtual tresult getBusArrangement(BusDirection dir, int32 index, SpeakerArrangement& arr) = 0;
    virtual tresult canProcessSampleSize(int32 symbolicSampleSize) = 0;
    virtual uint32 getLatencySamples() = 0;
    virtual tresult setupProcessing(ProcessSetup& setup) = 0;
    virtual tresult setProcessing(uint8 state) = 0;
    virtual tresult process(ProcessData& data) = 0;
    virtual uint32 getTailSamples() = 0;
};

// Parameter info structure
struct ParameterInfo {
    uint32 id;
    char16 title[128];
    char16 shortTitle[128];
    char16 units[128];
    int32 stepCount;
    double defaultNormalizedValue;
    int32 unitId;
    int32 flags;
};

// Parameter flags
enum ParameterFlags {
    kCanAutomate = 1 << 0,
    kIsReadOnly = 1 << 1,
    kIsWrapAround = 1 << 2,
    kIsList = 1 << 3,
    kIsProgramChange = 1 << 15,
    kIsBypass = 1 << 16
};

// IEditController
class IEditController : public IPluginBase {
public:
    virtual tresult setComponentState(void* state) = 0;
    virtual tresult setState(void* state) = 0;
    virtual tresult getState(void* state) = 0;
    virtual int32 getParameterCount() = 0;
    virtual tresult getParameterInfo(int32 paramIndex, ParameterInfo& info) = 0;
    virtual tresult getParamStringByValue(uint32 id, double valueNormalized, char16* string) = 0;
    virtual tresult getParamValueByString(uint32 id, char16* string, double& valueNormalized) = 0;
    virtual double normalizedParamToPlain(uint32 id, double valueNormalized) = 0;
    virtual double plainParamToNormalized(uint32 id, double plainValue) = 0;
    virtual double getParamNormalized(uint32 id) = 0;
    virtual tresult setParamNormalized(uint32 id, double value) = 0;
    virtual tresult setComponentHandler(void* handler) = 0;
    virtual void* createView(const char* name) = 0;
};

// IConnectionPoint {70A4156F-6E6E-4026-9891-48BFAA60D8D1}
// COM bytes: 6F 15 A4 70  6E 6E  26 40  98 91 48 BF AA 60 D8 D1
static const TUID IConnectionPoint_iid = {
    0x6F, 0x15, 0xA4, 0x70, 0x6E, 0x6E, 0x26, 0x40,
    0x98, 0x91, 0x48, 0xBF, 0xAA, 0x60, 0xD8, 0xD1
};

class IConnectionPoint : public FUnknown {
public:
    virtual tresult connect(IConnectionPoint* other) = 0;
    virtual tresult disconnect(IConnectionPoint* other) = 0;
    virtual tresult notify(void* message) = 0;
};

// ============================================================================
// GUI interfaces
// ============================================================================

// ViewRect structure
struct ViewRect {
    int32 left;
    int32 top;
    int32 right;
    int32 bottom;

    int32 getWidth() const { return right - left; }
    int32 getHeight() const { return bottom - top; }
};

// Forward declare
class IPlugView;

// IPlugFrame {367FAF01-AFA9-4693-8D4D-A2A0ED0882A3}
// COM bytes: 01 AF 7F 36  A9 AF  93 46  8D 4D A2 A0 ED 08 82 A3
static const TUID IPlugFrame_iid = {
    0x01, 0xAF, 0x7F, 0x36, 0xA9, 0xAF, 0x93, 0x46,
    0x8D, 0x4D, 0xA2, 0xA0, 0xED, 0x08, 0x82, 0xA3
};

class IPlugFrame : public FUnknown {
public:
    virtual tresult resizeView(IPlugView* view, ViewRect* newSize) = 0;
};

// IPlugView {5BC32507-D060-49EA-A615-1B522B755B29}
// COM bytes: 07 25 C3 5B  60 D0  EA 49  A6 15 1B 52 2B 75 5B 29
static const TUID IPlugView_iid = {
    0x07, 0x25, 0xC3, 0x5B, 0x60, 0xD0, 0xEA, 0x49,
    0xA6, 0x15, 0x1B, 0x52, 0x2B, 0x75, 0x5B, 0x29
};

class IPlugView : public FUnknown {
public:
    virtual tresult isPlatformTypeSupported(const char* type) = 0;
    virtual tresult attached(void* parent, const char* type) = 0;
    virtual tresult removed() = 0;
    virtual tresult onWheel(float distance) = 0;
    virtual tresult onKeyDown(char16 key, int16 keyCode, int16 modifiers) = 0;
    virtual tresult onKeyUp(char16 key, int16 keyCode, int16 modifiers) = 0;
    virtual tresult getSize(ViewRect* size) = 0;
    virtual tresult onSize(ViewRect* newSize) = 0;
    virtual tresult onFocus(uint8 state) = 0;
    virtual tresult setFrame(IPlugFrame* frame) = 0;
    virtual tresult canResize() = 0;
    virtual tresult checkSizeConstraint(ViewRect* rect) = 0;
};

// Platform type for Windows
static const char* kPlatformTypeHWND = "HWND";

// Simple IPlugFrame implementation for resize callbacks
class HostPlugFrame : public IPlugFrame {
public:
    IPlugView* view = nullptr;
    HWND hwnd = nullptr;

    tresult queryInterface(const TUID&, void** obj) override {
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 addRef() override { return 1; }
    uint32 release() override { return 1; }

    tresult resizeView(IPlugView* v, ViewRect* newSize) override {
        if (hwnd && newSize) {
            // Resize the window
            SetWindowPos(hwnd, nullptr, 0, 0,
                         newSize->getWidth(), newSize->getHeight(),
                         SWP_NOMOVE | SWP_NOZORDER);
            // Notify the view
            if (view) {
                view->onSize(newSize);
            }
        }
        return kResultOk;
    }
};

// ============================================================================
// IComponentHandler - receives parameter changes from plugin GUI
// ============================================================================

// IComponentHandler {93A0BEA3-0BD0-45db-8E89-0B0CC1E46AC6}
// COM bytes: A3 BE A0 93  D0 0B  db 45  8E 89 0B 0C C1 E4 6A C6
static const TUID IComponentHandler_iid = {
    0xA3, 0xBE, 0xA0, 0x93, 0xD0, 0x0B, 0xdb, 0x45,
    0x8E, 0x89, 0x0B, 0x0C, 0xC1, 0xE4, 0x6A, 0xC6
};

class IComponentHandler : public FUnknown {
public:
    virtual tresult beginEdit(uint32 id) = 0;
    virtual tresult performEdit(uint32 id, double valueNormalized) = 0;
    virtual tresult endEdit(uint32 id) = 0;
    virtual tresult restartComponent(int32 flags) = 0;
};

// Parameter change record
struct ParamChange {
    uint32 param_id;
    double value;
};

// Maximum pending parameter changes
static const int MAX_PARAM_CHANGES = 256;

// Host implementation of IComponentHandler
// Captures parameter changes from the plugin GUI
class HostComponentHandler : public IComponentHandler {
public:
    // Circular buffer for parameter changes
    ParamChange changes[MAX_PARAM_CHANGES];
    volatile int write_index = 0;
    volatile int read_index = 0;

    tresult queryInterface(const TUID& iid, void** obj) override {
        if (memcmp(iid.data, IComponentHandler_iid.data, sizeof(TUID)) == 0) {
            *obj = static_cast<IComponentHandler*>(this);
            addRef();
            return kResultOk;
        }
        if (memcmp(iid.data, FUnknown_iid.data, sizeof(TUID)) == 0) {
            *obj = static_cast<FUnknown*>(this);
            addRef();
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 addRef() override { return 1; }  // Static lifetime
    uint32 release() override { return 1; }

    tresult beginEdit(uint32 id) override {
        // Called when user starts adjusting a parameter in GUI
        return kResultOk;
    }

    tresult performEdit(uint32 id, double valueNormalized) override {
        // Called when parameter value changes in GUI
        // Store in circular buffer
        int next = (write_index + 1) % MAX_PARAM_CHANGES;
        if (next != read_index) {  // Don't overflow
            changes[write_index].param_id = id;
            changes[write_index].value = valueNormalized;
            write_index = next;
        }
        return kResultOk;
    }

    tresult endEdit(uint32 id) override {
        // Called when user stops adjusting a parameter in GUI
        return kResultOk;
    }

    tresult restartComponent(int32 flags) override {
        // Plugin requests host to restart (e.g., after latency change)
        return kResultOk;
    }

    // Get pending changes count
    int getPendingCount() const {
        int w = write_index;
        int r = read_index;
        if (w >= r) return w - r;
        return MAX_PARAM_CHANGES - r + w;
    }

    // Get next pending change, returns false if none
    bool getNextChange(ParamChange* out) {
        if (read_index == write_index) return false;
        *out = changes[read_index];
        read_index = (read_index + 1) % MAX_PARAM_CHANGES;
        return true;
    }

    // Clear all pending changes
    void clear() {
        read_index = write_index;
    }
};

} // namespace Steinberg

typedef Steinberg::IPluginFactory* (*GetFactoryProc)();
typedef bool (*InitModuleProc)();
typedef bool (*ExitModuleProc)();

// ============================================================================
// Plugin State
// ============================================================================

struct PluginState {
    bool loaded = false;
    bool initialized = false;
    bool processing = false;

    HMODULE module = nullptr;
    Steinberg::IPluginFactory* factory = nullptr;
    Steinberg::IPluginFactory2* factory2 = nullptr;
    Steinberg::IComponent* component = nullptr;
    Steinberg::IAudioProcessor* processor = nullptr;
    Steinberg::IEditController* controller = nullptr;
    InitModuleProc initModule = nullptr;
    ExitModuleProc exitModule = nullptr;

    char name[256] = {0};
    char vendor[256] = {0};
    char category[128] = {0};
    char uid[64] = {0};
    Steinberg::TUID cid = {0};
    int32_t num_classes = 0;

    // Audio config
    uint32_t sample_rate = 48000;
    uint32_t block_size = 512;
    uint32_t num_inputs = 2;
    uint32_t num_outputs = 2;

    // Editor state
    Steinberg::IPlugView* view = nullptr;
    Steinberg::HostPlugFrame plugFrame;
    HWND editorHwnd = nullptr;
    bool editorOpen = false;

    // Component handler for parameter change notifications from GUI
    Steinberg::HostComponentHandler componentHandler;
};

static PluginState g_plugin;

// Shared memory state
static HANDLE g_shm_handle = nullptr;
static void* g_shm_ptr = nullptr;
static size_t g_shm_size = 0;

// Event list for MIDI
static Steinberg::HostEventList g_input_events;
static Steinberg::HostEventList g_output_events;

// ============================================================================
// Helpers
// ============================================================================

void tuid_to_string(const Steinberg::TUID& tuid, char* out) {
    sprintf(out, "%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
        tuid.data[0], tuid.data[1], tuid.data[2], tuid.data[3],
        tuid.data[4], tuid.data[5], tuid.data[6], tuid.data[7],
        tuid.data[8], tuid.data[9], tuid.data[10], tuid.data[11],
        tuid.data[12], tuid.data[13], tuid.data[14], tuid.data[15]);
}

bool find_vst3_dll(const char* bundle_path, char* dll_path, size_t dll_path_size) {
    char test_path[MAX_PATH];

    const char* base_name = strrchr(bundle_path, '\\');
    if (!base_name) base_name = strrchr(bundle_path, '/');
    if (base_name) base_name++; else base_name = bundle_path;

    char name_without_ext[256];
    strncpy(name_without_ext, base_name, sizeof(name_without_ext) - 1);
    name_without_ext[sizeof(name_without_ext) - 1] = '\0';
    char* ext = strstr(name_without_ext, ".vst3");
    if (ext) *ext = '\0';

    snprintf(test_path, sizeof(test_path), "%s\\Contents\\x86_64-win\\%s.vst3",
             bundle_path, name_without_ext);
    if (GetFileAttributesA(test_path) != INVALID_FILE_ATTRIBUTES) {
        strncpy(dll_path, test_path, dll_path_size);
        return true;
    }

    if (strstr(bundle_path, ".dll") || strstr(bundle_path, ".vst3")) {
        DWORD attrs = GetFileAttributesA(bundle_path);
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            strncpy(dll_path, bundle_path, dll_path_size);
            return true;
        }
    }

    return false;
}

// ============================================================================
// Plugin Operations
// ============================================================================

void cleanup_audio() {
    if (g_plugin.processing && g_plugin.processor) {
        g_plugin.processor->setProcessing(0);
        g_plugin.processing = false;
    }

    if (g_plugin.initialized && g_plugin.component) {
        g_plugin.component->setActive(0);
        g_plugin.initialized = false;
    }

    if (g_shm_ptr) {
        UnmapViewOfFile(g_shm_ptr);
        g_shm_ptr = nullptr;
    }
    if (g_shm_handle) {
        CloseHandle(g_shm_handle);
        g_shm_handle = nullptr;
    }
    g_shm_size = 0;
}

void unload_plugin();  // Forward declaration
void close_editor();   // Forward declaration

void unload_plugin() {
    if (!g_plugin.loaded) return;

    printf("[HOST] Unloading plugin\n");

    // Close editor first
    close_editor();

    cleanup_audio();

    if (g_plugin.controller) {
        g_plugin.controller->terminate();
        g_plugin.controller->release();
        g_plugin.controller = nullptr;
    }
    if (g_plugin.processor) {
        g_plugin.processor->release();
        g_plugin.processor = nullptr;
    }
    if (g_plugin.component) {
        g_plugin.component->terminate();
        g_plugin.component->release();
        g_plugin.component = nullptr;
    }
    if (g_plugin.factory2) {
        g_plugin.factory2->release();
        g_plugin.factory2 = nullptr;
    }
    if (g_plugin.factory) {
        g_plugin.factory->release();
        g_plugin.factory = nullptr;
    }
    if (g_plugin.exitModule) g_plugin.exitModule();
    if (g_plugin.module) {
        FreeLibrary(g_plugin.module);
        g_plugin.module = nullptr;
    }

    g_plugin = PluginState();
}

bool load_plugin(const char* path, uint32_t class_index) {
    if (g_plugin.loaded) {
        unload_plugin();
    }

    printf("[HOST] Loading plugin: %s\n", path);

    char dll_path[MAX_PATH];
    if (!find_vst3_dll(path, dll_path, sizeof(dll_path))) {
        printf("[HOST] ERROR: Could not find DLL in bundle\n");
        return false;
    }

    printf("[HOST] DLL path: %s\n", dll_path);

    g_plugin.module = LoadLibraryA(dll_path);
    if (!g_plugin.module) {
        printf("[HOST] ERROR: LoadLibrary failed (%lu)\n", GetLastError());
        return false;
    }

    g_plugin.initModule = (InitModuleProc)GetProcAddress(g_plugin.module, "InitDll");
    g_plugin.exitModule = (ExitModuleProc)GetProcAddress(g_plugin.module, "ExitDll");
    GetFactoryProc getFactory = (GetFactoryProc)GetProcAddress(g_plugin.module, "GetPluginFactory");

    if (!getFactory) {
        printf("[HOST] ERROR: GetPluginFactory not found\n");
        FreeLibrary(g_plugin.module);
        g_plugin.module = nullptr;
        return false;
    }

    if (g_plugin.initModule) {
        g_plugin.initModule();
    }

    g_plugin.factory = getFactory();
    if (!g_plugin.factory) {
        printf("[HOST] ERROR: Factory is null\n");
        if (g_plugin.exitModule) g_plugin.exitModule();
        FreeLibrary(g_plugin.module);
        g_plugin.module = nullptr;
        return false;
    }

    g_plugin.factory->queryInterface(Steinberg::IPluginFactory2_iid, (void**)&g_plugin.factory2);

    Steinberg::PFactoryInfo factory_info;
    if (g_plugin.factory->getFactoryInfo(&factory_info) == Steinberg::kResultOk) {
        strncpy(g_plugin.vendor, factory_info.vendor, sizeof(g_plugin.vendor) - 1);
    }

    g_plugin.num_classes = g_plugin.factory->countClasses();
    printf("[HOST] Found %d classes\n", g_plugin.num_classes);

    // Find the Audio Module Class (processor)
    bool found = false;
    for (int32_t i = 0; i < g_plugin.num_classes && !found; i++) {
        Steinberg::PClassInfo info;
        if (g_plugin.factory->getClassInfo(i, &info) == Steinberg::kResultOk) {
            printf("[HOST] Class %d: name='%s', category='%s'\n", i, info.name, info.category);
            if (strcmp(info.category, "Audio Module Class") == 0) {
                if (class_index == 0) {
                    memcpy(&g_plugin.cid, &info.cid, sizeof(Steinberg::TUID));
                    strncpy(g_plugin.name, info.name, sizeof(g_plugin.name) - 1);
                    strncpy(g_plugin.category, info.category, sizeof(g_plugin.category) - 1);
                    tuid_to_string(info.cid, g_plugin.uid);
                    found = true;
                    printf("[HOST] Using class %d: %s\n", i, info.name);
                }
                class_index--;
            }
        }
    }

    if (!found) {
        printf("[HOST] ERROR: No Audio Module Class found\n");
        unload_plugin();
        return false;
    }

    // Create component instance
    Steinberg::FUnknown* unknown = nullptr;
    Steinberg::tresult result = g_plugin.factory->createInstance(g_plugin.cid, Steinberg::FUnknown_iid,
                                          (void**)&unknown);
    printf("[HOST] createInstance(FUnknown) result=%d, ptr=%p\n", result, unknown);

    if (result != Steinberg::kResultOk || !unknown) {
        printf("[HOST] ERROR: Failed to create component instance\n");
        unload_plugin();
        return false;
    }

    // Get IComponent interface via QueryInterface on the FUnknown
    result = unknown->queryInterface(Steinberg::IComponent_iid, (void**)&g_plugin.component);
    printf("[HOST] queryInterface(IComponent) result=%d, ptr=%p\n", result, g_plugin.component);

    if (result != Steinberg::kResultOk || !g_plugin.component) {
        // The object might already be IComponent without needing QueryInterface
        // In VST3, many implementations return the interface directly
        printf("[HOST] QueryInterface failed, trying direct cast\n");
        g_plugin.component = reinterpret_cast<Steinberg::IComponent*>(unknown);
    } else {
        // QueryInterface succeeded, release the original FUnknown reference
        unknown->release();
    }

    // Initialize component
    result = g_plugin.component->initialize(nullptr);
    printf("[HOST] component->initialize() result=%d\n", result);
    if (result != Steinberg::kResultOk) {
        printf("[HOST] ERROR: Failed to initialize component\n");
        unload_plugin();
        return false;
    }

    // Get audio processor interface - try via the FUnknown first since that seemed to work
    // The same object typically implements both IComponent and IAudioProcessor
    Steinberg::FUnknown* component_as_unknown = reinterpret_cast<Steinberg::FUnknown*>(g_plugin.component);
    result = component_as_unknown->queryInterface(Steinberg::IAudioProcessor_iid,
                                            (void**)&g_plugin.processor);
    printf("[HOST] queryInterface(IAudioProcessor) result=%d, ptr=%p\n", result, g_plugin.processor);

    if (result != Steinberg::kResultOk || !g_plugin.processor) {
        // QueryInterface failed - we can still load the plugin but can't do audio processing
        printf("[HOST] WARNING: Could not get IAudioProcessor - audio will be passthrough only\n");
        g_plugin.processor = nullptr;
    }

    // Get edit controller interface for parameters
    // First try: QueryInterface (for plugins where controller is same object as component)
    result = component_as_unknown->queryInterface(Steinberg::IEditController_iid,
                                            (void**)&g_plugin.controller);
    printf("[HOST] queryInterface(IEditController) result=%d, ptr=%p\n", result, g_plugin.controller);

    if (result != Steinberg::kResultOk || !g_plugin.controller) {
        // Second try: Get controller class ID and create separate instance
        printf("[HOST] Trying to get separate controller class...\n");
        Steinberg::TUID controller_cid;
        result = g_plugin.component->getControllerClassId(controller_cid);
        printf("[HOST] getControllerClassId result=%d\n", result);

        if (result == Steinberg::kResultOk) {
            // Create controller instance
            Steinberg::FUnknown* ctrl_unknown = nullptr;
            result = g_plugin.factory->createInstance(controller_cid, Steinberg::FUnknown_iid,
                                                      (void**)&ctrl_unknown);
            printf("[HOST] createInstance(controller) result=%d, ptr=%p\n", result, ctrl_unknown);

            if (result == Steinberg::kResultOk && ctrl_unknown) {
                result = ctrl_unknown->queryInterface(Steinberg::IEditController_iid,
                                                      (void**)&g_plugin.controller);
                printf("[HOST] queryInterface(IEditController) on controller result=%d, ptr=%p\n",
                       result, g_plugin.controller);
                ctrl_unknown->release();
            }
        }

        if (!g_plugin.controller) {
            printf("[HOST] WARNING: Could not get IEditController - parameters not available\n");
        }
    }

    if (g_plugin.controller) {
        // Initialize the controller
        result = g_plugin.controller->initialize(nullptr);
        printf("[HOST] controller->initialize() result=%d\n", result);
        if (result != Steinberg::kResultOk) {
            printf("[HOST] WARNING: Controller initialization failed\n");
        }

        // Set component handler to receive parameter change callbacks from GUI
        result = g_plugin.controller->setComponentHandler(&g_plugin.componentHandler);
        printf("[HOST] setComponentHandler result=%d\n", result);

        // Connect component and controller via IConnectionPoint
        // This is required for separate processor/controller plugins (e.g., JUCE)
        Steinberg::FUnknown* component_unknown = reinterpret_cast<Steinberg::FUnknown*>(g_plugin.component);
        Steinberg::FUnknown* controller_unknown = reinterpret_cast<Steinberg::FUnknown*>(g_plugin.controller);

        Steinberg::IConnectionPoint* comp_conn = nullptr;
        Steinberg::IConnectionPoint* ctrl_conn = nullptr;

        component_unknown->queryInterface(Steinberg::IConnectionPoint_iid, (void**)&comp_conn);
        controller_unknown->queryInterface(Steinberg::IConnectionPoint_iid, (void**)&ctrl_conn);

        if (comp_conn && ctrl_conn) {
            comp_conn->connect(ctrl_conn);
            ctrl_conn->connect(comp_conn);
            printf("[HOST] Component/controller connected\n");
        }

        printf("[HOST] Parameters: %d\n", g_plugin.controller->getParameterCount());
    }

    g_plugin.loaded = true;
    printf("[HOST] Plugin loaded: %s by %s\n", g_plugin.name, g_plugin.vendor);

    return true;
}

// ============================================================================
// Editor (GUI) Functions
// ============================================================================

// Window procedure for editor window
static LRESULT CALLBACK EditorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CLOSE:
            // Don't destroy, just hide
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_DESTROY:
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

// Register editor window class
static bool g_editor_class_registered = false;
static const wchar_t* EDITOR_CLASS_NAME = L"RackWineEditor";

static bool register_editor_class() {
    if (g_editor_class_registered) return true;

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = EditorWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = EDITOR_CLASS_NAME;

    if (!RegisterClassExW(&wc)) {
        printf("[HOST] ERROR: Failed to register editor window class\n");
        return false;
    }

    g_editor_class_registered = true;
    return true;
}

bool open_editor(RespEditorInfo* resp) {
    memset(resp, 0, sizeof(*resp));

    if (!g_plugin.loaded || !g_plugin.controller) {
        printf("[HOST] ERROR: No plugin loaded or no controller\n");
        return false;
    }

    if (g_plugin.editorOpen) {
        printf("[HOST] Editor already open\n");
        // Return existing window info
        if (g_plugin.editorHwnd) {
            // Get X11 window ID from Wine window
            // Wine exposes this through a special property
            resp->x11_window_id = (uint32_t)(uintptr_t)g_plugin.editorHwnd;
            Steinberg::ViewRect rect;
            if (g_plugin.view && g_plugin.view->getSize(&rect) == Steinberg::kResultOk) {
                resp->width = rect.getWidth();
                resp->height = rect.getHeight();
            }
        }
        return true;
    }

    // Create the view
    void* rawView = g_plugin.controller->createView("editor");
    if (!rawView) {
        printf("[HOST] ERROR: createView returned null\n");
        return false;
    }

    // Query for IPlugView interface
    Steinberg::FUnknown* viewUnknown = reinterpret_cast<Steinberg::FUnknown*>(rawView);
    Steinberg::tresult result = viewUnknown->queryInterface(Steinberg::IPlugView_iid,
                                                            (void**)&g_plugin.view);
    if (result != Steinberg::kResultOk || !g_plugin.view) {
        printf("[HOST] ERROR: Failed to get IPlugView interface\n");
        viewUnknown->release();
        return false;
    }
    viewUnknown->release();

    // Check if HWND is supported
    result = g_plugin.view->isPlatformTypeSupported(Steinberg::kPlatformTypeHWND);
    if (result != Steinberg::kResultOk) {
        printf("[HOST] ERROR: Plugin doesn't support HWND platform\n");
        g_plugin.view->release();
        g_plugin.view = nullptr;
        return false;
    }

    // Get initial size
    Steinberg::ViewRect rect = {0, 0, 800, 600};  // Default size
    g_plugin.view->getSize(&rect);
    int width = rect.getWidth();
    int height = rect.getHeight();
    printf("[HOST] Editor size: %dx%d\n", width, height);

    // Register window class
    if (!register_editor_class()) {
        g_plugin.view->release();
        g_plugin.view = nullptr;
        return false;
    }

    // Create editor window
    g_plugin.editorHwnd = CreateWindowExW(
        0,
        EDITOR_CLASS_NAME,
        L"Plugin Editor",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        width, height,
        nullptr, nullptr,
        GetModuleHandle(nullptr),
        nullptr
    );

    if (!g_plugin.editorHwnd) {
        printf("[HOST] ERROR: Failed to create editor window\n");
        g_plugin.view->release();
        g_plugin.view = nullptr;
        return false;
    }

    // Set up the plug frame
    g_plugin.plugFrame.view = g_plugin.view;
    g_plugin.plugFrame.hwnd = g_plugin.editorHwnd;
    g_plugin.view->setFrame(&g_plugin.plugFrame);

    // Attach the view to the window
    result = g_plugin.view->attached((void*)g_plugin.editorHwnd, Steinberg::kPlatformTypeHWND);
    if (result != Steinberg::kResultOk) {
        printf("[HOST] ERROR: Failed to attach view to window (result=%d)\n", result);
        DestroyWindow(g_plugin.editorHwnd);
        g_plugin.editorHwnd = nullptr;
        g_plugin.view->release();
        g_plugin.view = nullptr;
        return false;
    }

    // Show the window
    ShowWindow(g_plugin.editorHwnd, SW_SHOW);
    UpdateWindow(g_plugin.editorHwnd);

    g_plugin.editorOpen = true;

    // Return the window handle as X11 window ID
    // Wine windows are X11 windows, so the HWND can be used directly
    // (Wine internally maps HWNDs to X11 window IDs)
    resp->x11_window_id = (uint32_t)(uintptr_t)g_plugin.editorHwnd;
    resp->width = width;
    resp->height = height;

    printf("[HOST] Editor opened, HWND=%p\n", g_plugin.editorHwnd);
    return true;
}

void close_editor() {
    if (!g_plugin.editorOpen) return;

    if (g_plugin.view) {
        g_plugin.view->removed();
        g_plugin.view->setFrame(nullptr);
        g_plugin.view->release();
        g_plugin.view = nullptr;
    }

    if (g_plugin.editorHwnd) {
        DestroyWindow(g_plugin.editorHwnd);
        g_plugin.editorHwnd = nullptr;
    }

    g_plugin.editorOpen = false;
    printf("[HOST] Editor closed\n");
}

bool get_editor_size(RespEditorSize* resp) {
    memset(resp, 0, sizeof(*resp));

    if (!g_plugin.view) {
        return false;
    }

    Steinberg::ViewRect rect;
    if (g_plugin.view->getSize(&rect) == Steinberg::kResultOk) {
        resp->width = rect.getWidth();
        resp->height = rect.getHeight();
        return true;
    }
    return false;
}

bool init_audio(const CmdInitAudio* cmd) {
    if (!g_plugin.loaded) {
        printf("[HOST] ERROR: No plugin loaded\n");
        return false;
    }

    printf("[HOST] Initializing audio: %uHz, %u samples, %u in, %u out\n",
           cmd->sample_rate, cmd->block_size, cmd->num_inputs, cmd->num_outputs);
    printf("[HOST] SHM name: %s\n", cmd->shm_name);

    cleanup_audio();

    g_plugin.sample_rate = cmd->sample_rate;
    g_plugin.block_size = cmd->block_size;
    g_plugin.num_inputs = cmd->num_inputs;
    g_plugin.num_outputs = cmd->num_outputs;

    // Open the file (created by Linux client, accessed via Wine's Z: drive)
    // The path is like "Z:\tmp\rack-wine-audio-12345"
    g_shm_size = RACK_WINE_SHM_SIZE(cmd->num_inputs, cmd->num_outputs, cmd->block_size);

    HANDLE file_handle = CreateFileA(
        cmd->shm_name,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (file_handle == INVALID_HANDLE_VALUE) {
        printf("[HOST] ERROR: Failed to open file '%s' (%lu)\n",
               cmd->shm_name, GetLastError());
        return false;
    }

    // Create file mapping on the file
    g_shm_handle = CreateFileMappingA(file_handle, NULL, PAGE_READWRITE, 0, (DWORD)g_shm_size, NULL);
    if (!g_shm_handle) {
        printf("[HOST] ERROR: Failed to create file mapping (%lu)\n", GetLastError());
        CloseHandle(file_handle);
        return false;
    }

    g_shm_ptr = MapViewOfFile(g_shm_handle, FILE_MAP_ALL_ACCESS, 0, 0, g_shm_size);
    if (!g_shm_ptr) {
        printf("[HOST] ERROR: Failed to map shared memory (%lu)\n", GetLastError());
        CloseHandle(g_shm_handle);
        CloseHandle(file_handle);
        g_shm_handle = nullptr;
        return false;
    }

    // We can close the file handle now - the mapping keeps it open
    CloseHandle(file_handle);

    printf("[HOST] Shared memory mapped: %zu bytes\n", g_shm_size);

    // Setup bus arrangements (stereo in/out)
    if (g_plugin.processor) {
        Steinberg::SpeakerArrangement inArr = Steinberg::kStereo;
        Steinberg::SpeakerArrangement outArr = Steinberg::kStereo;
        g_plugin.processor->setBusArrangements(&inArr, 1, &outArr, 1);
    }

    // Activate buses
    g_plugin.component->activateBus(Steinberg::kAudio, Steinberg::kInput, 0, 1);
    g_plugin.component->activateBus(Steinberg::kAudio, Steinberg::kOutput, 0, 1);

    // Setup processing
    if (g_plugin.processor) {
        Steinberg::ProcessSetup setup;
        setup.processMode = 0;  // Realtime
        setup.symbolicSampleSize = 0;  // 32-bit float
        setup.maxSamplesPerBlock = cmd->block_size;
        setup.sampleRate = cmd->sample_rate;

        Steinberg::tresult r = g_plugin.processor->setupProcessing(setup);
        printf("[HOST] setupProcessing result=%d\n", r);
    }

    // Activate
    Steinberg::tresult r = g_plugin.component->setActive(1);
    printf("[HOST] setActive result=%d\n", r);
    g_plugin.initialized = true;

    // Start processing
    if (g_plugin.processor) {
        r = g_plugin.processor->setProcessing(1);
        printf("[HOST] setProcessing result=%d\n", r);
    }
    g_plugin.processing = true;

    printf("[HOST] Audio initialized\n");
    return true;
}

bool process_audio(uint32_t num_samples) {
    if (!g_plugin.processing || !g_shm_ptr) {
        return false;
    }

    RackWineShmHeader* shm = (RackWineShmHeader*)g_shm_ptr;

    // Calculate buffer pointers
    float* input_base = (float*)((uint8_t*)g_shm_ptr + shm->input_offset);
    float* output_base = (float*)((uint8_t*)g_shm_ptr + shm->output_offset);

    // If no processor, just copy input to output (passthrough)
    if (!g_plugin.processor) {
        uint32_t channels = (shm->num_inputs < shm->num_outputs) ? shm->num_inputs : shm->num_outputs;
        for (uint32_t ch = 0; ch < channels; ch++) {
            float* in = input_base + ch * shm->block_size;
            float* out = output_base + ch * shm->block_size;
            memcpy(out, in, num_samples * sizeof(float));
        }
        for (uint32_t ch = channels; ch < shm->num_outputs; ch++) {
            float* out = output_base + ch * shm->block_size;
            memset(out, 0, num_samples * sizeof(float));
        }
        return true;
    }

    // Set up channel pointers
    float* input_channels[RACK_WINE_MAX_CHANNELS];
    float* output_channels[RACK_WINE_MAX_CHANNELS];

    for (uint32_t i = 0; i < shm->num_inputs; i++) {
        input_channels[i] = input_base + i * shm->block_size;
    }
    for (uint32_t i = 0; i < shm->num_outputs; i++) {
        output_channels[i] = output_base + i * shm->block_size;
    }

    // Setup process data
    Steinberg::AudioBusBuffers inputs;
    inputs.numChannels = shm->num_inputs;
    inputs.silenceFlags = 0;
    inputs.channelBuffers32 = input_channels;

    Steinberg::AudioBusBuffers outputs;
    outputs.numChannels = shm->num_outputs;
    outputs.silenceFlags = 0;
    outputs.channelBuffers32 = output_channels;

    Steinberg::ProcessData data;
    memset(&data, 0, sizeof(data));
    data.processMode = 0;  // Realtime
    data.symbolicSampleSize = 0;  // 32-bit
    data.numSamples = num_samples;
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = &inputs;
    data.outputs = &outputs;
    data.inputEvents = &g_input_events;
    data.outputEvents = &g_output_events;

    // Process
    Steinberg::tresult result = g_plugin.processor->process(data);

    // Clear events after processing
    g_input_events.clear();
    g_output_events.clear();

    return result == Steinberg::kResultOk;
}

// ============================================================================
// Socket Server
// ============================================================================

bool send_response(SOCKET client, uint32_t status, const void* payload, uint32_t payload_size) {
    RackWineResponse resp;
    resp.magic = RACK_WINE_RESPONSE_MAGIC;
    resp.status = status;
    resp.payload_size = payload_size;

    if (send(client, (const char*)&resp, sizeof(resp), 0) != sizeof(resp)) {
        return false;
    }

    if (payload_size > 0 && payload) {
        if (send(client, (const char*)payload, payload_size, 0) != (int)payload_size) {
            return false;
        }
    }

    return true;
}

bool handle_command(SOCKET client, const RackWineHeader* header, const uint8_t* payload) {
    switch (header->command) {
        case CMD_PING: {
            return send_response(client, STATUS_OK, nullptr, 0);
        }

        case CMD_LOAD_PLUGIN: {
            if (header->payload_size < sizeof(CmdLoadPlugin)) {
                return send_response(client, STATUS_INVALID_PARAM, nullptr, 0);
            }
            const CmdLoadPlugin* cmd = (const CmdLoadPlugin*)payload;
            bool ok = load_plugin(cmd->path, cmd->class_index);
            return send_response(client, ok ? STATUS_OK : STATUS_ERROR, nullptr, 0);
        }

        case CMD_UNLOAD_PLUGIN: {
            unload_plugin();
            return send_response(client, STATUS_OK, nullptr, 0);
        }

        case CMD_GET_INFO: {
            if (!g_plugin.loaded) {
                return send_response(client, STATUS_NOT_LOADED, nullptr, 0);
            }
            RespPluginInfo info = {0};
            strncpy(info.name, g_plugin.name, sizeof(info.name) - 1);
            strncpy(info.vendor, g_plugin.vendor, sizeof(info.vendor) - 1);
            strncpy(info.category, g_plugin.category, sizeof(info.category) - 1);
            strncpy(info.uid, g_plugin.uid, sizeof(info.uid) - 1);
            info.num_params = g_plugin.controller ? g_plugin.controller->getParameterCount() : 0;
            info.num_audio_inputs = g_plugin.num_inputs;
            info.num_audio_outputs = g_plugin.num_outputs;
            return send_response(client, STATUS_OK, &info, sizeof(info));
        }

        case CMD_GET_PARAM_COUNT: {
            if (!g_plugin.loaded) {
                return send_response(client, STATUS_NOT_LOADED, nullptr, 0);
            }
            uint32_t count = g_plugin.controller ? g_plugin.controller->getParameterCount() : 0;
            return send_response(client, STATUS_OK, &count, sizeof(count));
        }

        case CMD_GET_PARAM_INFO: {
            if (!g_plugin.loaded) {
                return send_response(client, STATUS_NOT_LOADED, nullptr, 0);
            }
            if (!g_plugin.controller) {
                return send_response(client, STATUS_ERROR, nullptr, 0);
            }
            if (header->payload_size < sizeof(uint32_t)) {
                return send_response(client, STATUS_INVALID_PARAM, nullptr, 0);
            }
            uint32_t param_index = *(const uint32_t*)payload;

            Steinberg::ParameterInfo pinfo;
            if (g_plugin.controller->getParameterInfo(param_index, pinfo) != Steinberg::kResultOk) {
                return send_response(client, STATUS_INVALID_PARAM, nullptr, 0);
            }

            RespParamInfo resp = {0};
            resp.id = pinfo.id;
            // Convert UTF-16 title to ASCII
            for (int i = 0; i < 127 && pinfo.title[i]; i++) {
                resp.name[i] = (char)pinfo.title[i];
            }
            // Convert UTF-16 units to ASCII
            for (int i = 0; i < 31 && pinfo.units[i]; i++) {
                resp.units[i] = (char)pinfo.units[i];
            }
            resp.default_value = pinfo.defaultNormalizedValue;
            resp.min_value = 0.0;  // Normalized range is always 0-1
            resp.max_value = 1.0;
            resp.flags = pinfo.flags;
            return send_response(client, STATUS_OK, &resp, sizeof(resp));
        }

        case CMD_GET_PARAM: {
            if (!g_plugin.loaded) {
                return send_response(client, STATUS_NOT_LOADED, nullptr, 0);
            }
            if (!g_plugin.controller) {
                return send_response(client, STATUS_ERROR, nullptr, 0);
            }
            if (header->payload_size < sizeof(uint32_t)) {
                return send_response(client, STATUS_INVALID_PARAM, nullptr, 0);
            }
            uint32_t param_id = *(const uint32_t*)payload;
            double value = g_plugin.controller->getParamNormalized(param_id);
            CmdParam resp;
            resp.param_id = param_id;
            resp.value = value;
            return send_response(client, STATUS_OK, &resp, sizeof(resp));
        }

        case CMD_SET_PARAM: {
            if (!g_plugin.loaded) {
                return send_response(client, STATUS_NOT_LOADED, nullptr, 0);
            }
            if (!g_plugin.controller) {
                return send_response(client, STATUS_ERROR, nullptr, 0);
            }
            if (header->payload_size < sizeof(CmdParam)) {
                return send_response(client, STATUS_INVALID_PARAM, nullptr, 0);
            }
            const CmdParam* cmd = (const CmdParam*)payload;
            Steinberg::tresult result = g_plugin.controller->setParamNormalized(cmd->param_id, cmd->value);
            return send_response(client, result == Steinberg::kResultOk ? STATUS_OK : STATUS_ERROR, nullptr, 0);
        }

        case CMD_SEND_MIDI: {
            if (!g_plugin.loaded) {
                return send_response(client, STATUS_NOT_LOADED, nullptr, 0);
            }
            if (header->payload_size < sizeof(CmdMidi)) {
                return send_response(client, STATUS_INVALID_PARAM, nullptr, 0);
            }
            const CmdMidi* cmd = (const CmdMidi*)payload;
            const MidiEvent* events = (const MidiEvent*)(payload + sizeof(CmdMidi));

            // Convert MIDI events to VST3 events
            for (uint32_t i = 0; i < cmd->num_events && i < MAX_EVENTS; i++) {
                const MidiEvent* me = &events[i];
                uint8_t status = me->data[0];
                uint8_t data1 = me->data[1];
                uint8_t data2 = me->data[2];
                uint8_t type = status & 0xF0;
                uint8_t channel = status & 0x0F;

                Steinberg::Event e;
                memset(&e, 0, sizeof(e));
                e.busIndex = 0;
                e.sampleOffset = me->sample_offset;
                e.flags = Steinberg::kIsLive;

                if (type == 0x90 && data2 > 0) {
                    // Note On
                    e.type = Steinberg::kNoteOnEvent;
                    e.noteOn.channel = channel;
                    e.noteOn.pitch = data1;
                    e.noteOn.velocity = data2 / 127.0f;
                    e.noteOn.tuning = 0.0f;
                    e.noteOn.length = 0;
                    e.noteOn.noteId = -1;
                    g_input_events.addEvent(e);
                } else if (type == 0x80 || (type == 0x90 && data2 == 0)) {
                    // Note Off
                    e.type = Steinberg::kNoteOffEvent;
                    e.noteOff.channel = channel;
                    e.noteOff.pitch = data1;
                    e.noteOff.velocity = data2 / 127.0f;
                    e.noteOff.tuning = 0.0f;
                    e.noteOff.noteId = -1;
                    g_input_events.addEvent(e);
                } else if (type == 0xA0) {
                    // Poly Pressure (Aftertouch)
                    e.type = Steinberg::kPolyPressureEvent;
                    e.polyPressure.channel = channel;
                    e.polyPressure.pitch = data1;
                    e.polyPressure.pressure = data2 / 127.0f;
                    e.polyPressure.noteId = -1;
                    g_input_events.addEvent(e);
                }
                // CC, pitch bend, etc. handled through parameters in VST3
            }
            printf("[HOST] Received %u MIDI events, queued %d\n", cmd->num_events, g_input_events.count);
            return send_response(client, STATUS_OK, nullptr, 0);
        }

        case CMD_INIT_AUDIO: {
            if (header->payload_size < sizeof(CmdInitAudio)) {
                return send_response(client, STATUS_INVALID_PARAM, nullptr, 0);
            }
            const CmdInitAudio* cmd = (const CmdInitAudio*)payload;
            bool ok = init_audio(cmd);
            return send_response(client, ok ? STATUS_OK : STATUS_ERROR, nullptr, 0);
        }

        case CMD_PROCESS_AUDIO: {
            if (!g_plugin.processing) {
                return send_response(client, STATUS_NOT_INITIALIZED, nullptr, 0);
            }
            uint32_t num_samples = g_plugin.block_size;
            if (header->payload_size >= sizeof(CmdProcessAudio)) {
                const CmdProcessAudio* cmd = (const CmdProcessAudio*)payload;
                num_samples = cmd->num_samples;
            }
            bool ok = process_audio(num_samples);
            return send_response(client, ok ? STATUS_OK : STATUS_ERROR, nullptr, 0);
        }

        case CMD_OPEN_EDITOR: {
            if (!g_plugin.loaded) {
                return send_response(client, STATUS_NOT_LOADED, nullptr, 0);
            }
            RespEditorInfo resp;
            bool ok = open_editor(&resp);
            return send_response(client, ok ? STATUS_OK : STATUS_ERROR, &resp, sizeof(resp));
        }

        case CMD_CLOSE_EDITOR: {
            close_editor();
            return send_response(client, STATUS_OK, nullptr, 0);
        }

        case CMD_GET_EDITOR_SIZE: {
            if (!g_plugin.view) {
                return send_response(client, STATUS_ERROR, nullptr, 0);
            }
            RespEditorSize resp;
            bool ok = get_editor_size(&resp);
            return send_response(client, ok ? STATUS_OK : STATUS_ERROR, &resp, sizeof(resp));
        }

        case CMD_GET_PARAM_CHANGES: {
            // Get pending parameter changes from GUI
            int count = g_plugin.componentHandler.getPendingCount();

            // Build response: header + array of changes
            size_t resp_size = sizeof(RespParamChanges) + count * sizeof(ParamChangeEvent);
            uint8_t* resp_buf = (uint8_t*)alloca(resp_size);
            RespParamChanges* resp = (RespParamChanges*)resp_buf;
            ParamChangeEvent* events = (ParamChangeEvent*)(resp_buf + sizeof(RespParamChanges));

            resp->num_changes = 0;
            Steinberg::ParamChange change;
            while (g_plugin.componentHandler.getNextChange(&change) && resp->num_changes < (uint32_t)count) {
                events[resp->num_changes].param_id = change.param_id;
                events[resp->num_changes].value = change.value;
                resp->num_changes++;
            }

            return send_response(client, STATUS_OK, resp_buf, sizeof(RespParamChanges) + resp->num_changes * sizeof(ParamChangeEvent));
        }

        case CMD_SHUTDOWN: {
            printf("[HOST] Shutdown requested\n");
            send_response(client, STATUS_OK, nullptr, 0);
            return false;
        }

        default:
            printf("[HOST] Unknown command: %u\n", header->command);
            return send_response(client, STATUS_ERROR, nullptr, 0);
    }
}

int run_server() {
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        printf("[HOST] WSAStartup failed\n");
        return 1;
    }

    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == INVALID_SOCKET) {
        printf("[HOST] Failed to create socket (%d)\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int port = 0;
    for (int p = RACK_WINE_PORT_BASE; p <= RACK_WINE_PORT_MAX; p++) {
        addr.sin_port = htons(p);
        if (bind(server_socket, (struct sockaddr*)&addr, sizeof(addr)) != SOCKET_ERROR) {
            port = p;
            break;
        }
    }

    if (port == 0) {
        printf("[HOST] Failed to bind to any port\n");
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    if (listen(server_socket, 1) == SOCKET_ERROR) {
        printf("[HOST] Listen failed\n");
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    printf("PORT=%d\n", port);
    fflush(stdout);

    printf("[HOST] Listening on 127.0.0.1:%d\n", port);

    SOCKET client_socket = accept(server_socket, nullptr, nullptr);
    if (client_socket == INVALID_SOCKET) {
        printf("[HOST] Accept failed\n");
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    printf("[HOST] Client connected\n");

    bool running = true;
    while (running) {
        RackWineHeader header;
        int received = recv(client_socket, (char*)&header, sizeof(header), MSG_WAITALL);

        if (received <= 0) break;
        if (received != sizeof(header)) break;
        if (header.magic != RACK_WINE_MAGIC) break;
        if (header.version != RACK_WINE_PROTOCOL_VERSION) break;

        uint8_t* payload = nullptr;
        if (header.payload_size > 0) {
            payload = new uint8_t[header.payload_size];
            received = recv(client_socket, (char*)payload, header.payload_size, MSG_WAITALL);
            if (received != (int)header.payload_size) {
                delete[] payload;
                break;
            }
        }

        running = handle_command(client_socket, &header, payload);
        delete[] payload;
    }

    unload_plugin();
    closesocket(client_socket);
    closesocket(server_socket);
    WSACleanup();

    printf("[HOST] Server shutdown\n");
    return 0;
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    printf("=== rack-wine-host v0.3 ===\n\n");
    return run_server();
}
