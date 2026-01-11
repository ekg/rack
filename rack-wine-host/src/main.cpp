// rack-wine-host: Windows VST3 plugin host for Wine
// This is cross-compiled with MinGW and runs under Wine
// Phase 1: Load plugin and get info

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

// VST3 SDK types (minimal subset needed for loading)
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
typedef char8 tchar;

// COM-style GUID
struct TUID {
    uint8 data[16];
};

// FUnknown - base interface
class FUnknown {
public:
    virtual tresult queryInterface(const TUID& iid, void** obj) = 0;
    virtual uint32 addRef() = 0;
    virtual uint32 release() = 0;
};

// Result codes
enum {
    kResultOk = 0,
    kResultFalse = 1,
    kNoInterface = -1,
};

// Plugin category (used to identify audio processors)
[[maybe_unused]] static const char* kVstAudioEffectClass = "Audio Module Class";

// Class info structure
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

// Factory info
struct PFactoryInfo {
    char8 vendor[64];
    char8 url[256];
    char8 email[128];
    int32 flags;
};

// IPluginFactory interface GUID
// {7A4D811C-5211-4A1F-AED9-D2EE0B43BF9F}
static const TUID IPluginFactory_iid = {
    0x7A, 0x4D, 0x81, 0x1C, 0x52, 0x11, 0x4A, 0x1F,
    0xAE, 0xD9, 0xD2, 0xEE, 0x0B, 0x43, 0xBF, 0x9F
};

// IPluginFactory2 interface GUID
// {0007B650-F24B-4C0B-A464-EDB9F00B2ABB}
static const TUID IPluginFactory2_iid = {
    0x00, 0x07, 0xB6, 0x50, 0xF2, 0x4B, 0x4C, 0x0B,
    0xA4, 0x64, 0xED, 0xB9, 0xF0, 0x0B, 0x2A, 0xBB
};

// IPluginFactory interface
class IPluginFactory : public FUnknown {
public:
    virtual tresult getFactoryInfo(PFactoryInfo* info) = 0;
    virtual int32 countClasses() = 0;
    virtual tresult getClassInfo(int32 index, PClassInfo* info) = 0;
    virtual tresult createInstance(const TUID& cid, const TUID& iid, void** obj) = 0;
};

// IPluginFactory2 interface (extended info)
class IPluginFactory2 : public IPluginFactory {
public:
    virtual tresult getClassInfo2(int32 index, PClassInfo2* info) = 0;
};

} // namespace Steinberg

// VST3 module entry point typedef
typedef Steinberg::IPluginFactory* (*GetFactoryProc)();
typedef bool (*InitModuleProc)();
typedef bool (*ExitModuleProc)();

// Helper to convert TUID to hex string
void tuid_to_string(const Steinberg::TUID& tuid, char* out) {
    sprintf(out, "%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
        tuid.data[0], tuid.data[1], tuid.data[2], tuid.data[3],
        tuid.data[4], tuid.data[5], tuid.data[6], tuid.data[7],
        tuid.data[8], tuid.data[9], tuid.data[10], tuid.data[11],
        tuid.data[12], tuid.data[13], tuid.data[14], tuid.data[15]);
}

// Find the DLL inside a .vst3 bundle
bool find_vst3_dll(const char* bundle_path, char* dll_path, size_t dll_path_size) {
    // Try new bundle format: Contents/x86_64-win/<name>.vst3
    char test_path[MAX_PATH];

    // Extract base name from bundle path
    const char* base_name = strrchr(bundle_path, '\\');
    if (!base_name) base_name = strrchr(bundle_path, '/');
    if (base_name) base_name++; else base_name = bundle_path;

    // Remove .vst3 extension from base name
    char name_without_ext[256];
    strncpy(name_without_ext, base_name, sizeof(name_without_ext) - 1);
    char* ext = strstr(name_without_ext, ".vst3");
    if (ext) *ext = '\0';

    // Try: bundle/Contents/x86_64-win/<name>.vst3
    snprintf(test_path, sizeof(test_path), "%s\\Contents\\x86_64-win\\%s.vst3",
             bundle_path, name_without_ext);
    if (GetFileAttributesA(test_path) != INVALID_FILE_ATTRIBUTES) {
        strncpy(dll_path, test_path, dll_path_size);
        return true;
    }

    // Try: bundle/Contents/x86_64-win/<name>.dll
    snprintf(test_path, sizeof(test_path), "%s\\Contents\\x86_64-win\\%s.dll",
             bundle_path, name_without_ext);
    if (GetFileAttributesA(test_path) != INVALID_FILE_ATTRIBUTES) {
        strncpy(dll_path, test_path, dll_path_size);
        return true;
    }

    // If bundle_path itself is a DLL (single file plugin)
    if (strstr(bundle_path, ".dll") || strstr(bundle_path, ".vst3")) {
        DWORD attrs = GetFileAttributesA(bundle_path);
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            strncpy(dll_path, bundle_path, dll_path_size);
            return true;
        }
    }

    return false;
}

int main(int argc, char* argv[]) {
    printf("=== rack-wine-host Phase 1 ===\n\n");

    if (argc < 2) {
        printf("Usage: %s <path-to-vst3>\n", argv[0]);
        printf("\nExample:\n");
        printf("  %s \"C:\\path\\to\\plugin.vst3\"\n", argv[0]);
        return 1;
    }

    const char* vst3_path = argv[1];
    printf("Loading: %s\n\n", vst3_path);

    // Find the actual DLL
    char dll_path[MAX_PATH];
    if (!find_vst3_dll(vst3_path, dll_path, sizeof(dll_path))) {
        printf("ERROR: Could not find VST3 DLL in bundle\n");
        printf("Tried bundle path: %s\n", vst3_path);
        return 1;
    }

    printf("DLL path: %s\n\n", dll_path);

    // Load the DLL
    HMODULE module = LoadLibraryA(dll_path);
    if (!module) {
        DWORD error = GetLastError();
        printf("ERROR: Failed to load DLL (error %lu)\n", error);
        return 1;
    }

    printf("DLL loaded successfully\n");

    // Get entry points
    InitModuleProc initModule = (InitModuleProc)GetProcAddress(module, "InitDll");
    ExitModuleProc exitModule = (ExitModuleProc)GetProcAddress(module, "ExitDll");
    GetFactoryProc getFactory = (GetFactoryProc)GetProcAddress(module, "GetPluginFactory");

    if (!getFactory) {
        printf("ERROR: GetPluginFactory not found\n");
        FreeLibrary(module);
        return 1;
    }

    printf("GetPluginFactory found\n");

    // Initialize module
    if (initModule) {
        if (!initModule()) {
            printf("WARNING: InitDll returned false\n");
        } else {
            printf("InitDll called successfully\n");
        }
    }

    // Get factory
    Steinberg::IPluginFactory* factory = getFactory();
    if (!factory) {
        printf("ERROR: GetPluginFactory returned null\n");
        if (exitModule) exitModule();
        FreeLibrary(module);
        return 1;
    }

    printf("Factory obtained\n\n");

    // Get factory info
    Steinberg::PFactoryInfo factory_info;
    if (factory->getFactoryInfo(&factory_info) == Steinberg::kResultOk) {
        printf("Factory Info:\n");
        printf("  Vendor: %s\n", factory_info.vendor);
        printf("  URL:    %s\n", factory_info.url);
        printf("  Email:  %s\n", factory_info.email);
        printf("\n");
    }

    // Try to get IPluginFactory2 for extended info
    Steinberg::IPluginFactory2* factory2 = nullptr;
    factory->queryInterface(Steinberg::IPluginFactory2_iid, (void**)&factory2);

    // Enumerate classes
    int32_t class_count = factory->countClasses();
    printf("Plugin classes: %d\n\n", class_count);

    for (int32_t i = 0; i < class_count; i++) {
        if (factory2) {
            // Use extended info
            Steinberg::PClassInfo2 info;
            if (factory2->getClassInfo2(i, &info) == Steinberg::kResultOk) {
                char uid_str[64];
                tuid_to_string(info.cid, uid_str);

                printf("Class %d:\n", i);
                printf("  Name:       %s\n", info.name);
                printf("  Category:   %s\n", info.category);
                printf("  Subcats:    %s\n", info.subCategories);
                printf("  Vendor:     %s\n", info.vendor);
                printf("  Version:    %s\n", info.version);
                printf("  SDK:        %s\n", info.sdkVersion);
                printf("  UID:        %s\n", uid_str);
                printf("\n");
            }
        } else {
            // Use basic info
            Steinberg::PClassInfo info;
            if (factory->getClassInfo(i, &info) == Steinberg::kResultOk) {
                char uid_str[64];
                tuid_to_string(info.cid, uid_str);

                printf("Class %d:\n", i);
                printf("  Name:     %s\n", info.name);
                printf("  Category: %s\n", info.category);
                printf("  UID:      %s\n", uid_str);
                printf("\n");
            }
        }
    }

    // Cleanup
    if (factory2) factory2->release();
    factory->release();

    if (exitModule) {
        exitModule();
        printf("ExitDll called\n");
    }

    FreeLibrary(module);
    printf("\nDLL unloaded. Done.\n");

    return 0;
}
