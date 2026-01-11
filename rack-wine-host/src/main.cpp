// rack-wine-host: Windows VST3 plugin host for Wine
// Phase 2: TCP socket server for IPC with Linux

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../include/protocol.h"

// ============================================================================
// VST3 Types (minimal subset)
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

struct TUID {
    uint8 data[16];
};

class FUnknown {
public:
    virtual tresult queryInterface(const TUID& iid, void** obj) = 0;
    virtual uint32 addRef() = 0;
    virtual uint32 release() = 0;
};

enum { kResultOk = 0, kResultFalse = 1, kNoInterface = -1 };

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

static const TUID IPluginFactory_iid = {
    0x7A, 0x4D, 0x81, 0x1C, 0x52, 0x11, 0x4A, 0x1F,
    0xAE, 0xD9, 0xD2, 0xEE, 0x0B, 0x43, 0xBF, 0x9F
};

static const TUID IPluginFactory2_iid = {
    0x00, 0x07, 0xB6, 0x50, 0xF2, 0x4B, 0x4C, 0x0B,
    0xA4, 0x64, 0xED, 0xB9, 0xF0, 0x0B, 0x2A, 0xBB
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

} // namespace Steinberg

typedef Steinberg::IPluginFactory* (*GetFactoryProc)();
typedef bool (*InitModuleProc)();
typedef bool (*ExitModuleProc)();

// ============================================================================
// Plugin State
// ============================================================================

struct PluginState {
    bool loaded = false;
    HMODULE module = nullptr;
    Steinberg::IPluginFactory* factory = nullptr;
    Steinberg::IPluginFactory2* factory2 = nullptr;
    InitModuleProc initModule = nullptr;
    ExitModuleProc exitModule = nullptr;

    char name[256] = {0};
    char vendor[256] = {0};
    char category[128] = {0};
    char uid[64] = {0};
    int32_t num_classes = 0;
};

static PluginState g_plugin;

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

    // Try: bundle/Contents/x86_64-win/<name>.vst3
    snprintf(test_path, sizeof(test_path), "%s\\Contents\\x86_64-win\\%s.vst3",
             bundle_path, name_without_ext);
    if (GetFileAttributesA(test_path) != INVALID_FILE_ATTRIBUTES) {
        strncpy(dll_path, test_path, dll_path_size);
        return true;
    }

    // Try single file
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

bool load_plugin(const char* path, uint32_t class_index) {
    if (g_plugin.loaded) {
        printf("[HOST] Plugin already loaded, unloading first\n");
        if (g_plugin.factory2) g_plugin.factory2->release();
        if (g_plugin.factory) g_plugin.factory->release();
        if (g_plugin.exitModule) g_plugin.exitModule();
        if (g_plugin.module) FreeLibrary(g_plugin.module);
        g_plugin = PluginState();
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

    if (class_index < (uint32_t)g_plugin.num_classes) {
        if (g_plugin.factory2) {
            Steinberg::PClassInfo2 info;
            if (g_plugin.factory2->getClassInfo2(class_index, &info) == Steinberg::kResultOk) {
                strncpy(g_plugin.name, info.name, sizeof(g_plugin.name) - 1);
                strncpy(g_plugin.category, info.subCategories, sizeof(g_plugin.category) - 1);
                if (info.vendor[0]) {
                    strncpy(g_plugin.vendor, info.vendor, sizeof(g_plugin.vendor) - 1);
                }
                tuid_to_string(info.cid, g_plugin.uid);
            }
        } else {
            Steinberg::PClassInfo info;
            if (g_plugin.factory->getClassInfo(class_index, &info) == Steinberg::kResultOk) {
                strncpy(g_plugin.name, info.name, sizeof(g_plugin.name) - 1);
                strncpy(g_plugin.category, info.category, sizeof(g_plugin.category) - 1);
                tuid_to_string(info.cid, g_plugin.uid);
            }
        }
    }

    g_plugin.loaded = true;
    printf("[HOST] Plugin loaded: %s by %s\n", g_plugin.name, g_plugin.vendor);

    return true;
}

void unload_plugin() {
    if (!g_plugin.loaded) return;

    printf("[HOST] Unloading plugin\n");

    if (g_plugin.factory2) g_plugin.factory2->release();
    if (g_plugin.factory) g_plugin.factory->release();
    if (g_plugin.exitModule) g_plugin.exitModule();
    if (g_plugin.module) FreeLibrary(g_plugin.module);

    g_plugin = PluginState();
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
    printf("[HOST] Command: %u, payload: %u bytes\n", header->command, header->payload_size);

    switch (header->command) {
        case CMD_PING: {
            printf("[HOST] PING\n");
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
            info.num_params = 0;
            info.num_audio_inputs = 2;
            info.num_audio_outputs = 2;
            return send_response(client, STATUS_OK, &info, sizeof(info));
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

    // Create TCP socket
    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == INVALID_SOCKET) {
        printf("[HOST] Failed to create socket (%d)\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    // Allow address reuse
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    // Try ports in range until one works
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // 127.0.0.1 only

    int port = 0;
    for (int p = RACK_WINE_PORT_BASE; p <= RACK_WINE_PORT_MAX; p++) {
        addr.sin_port = htons(p);
        if (bind(server_socket, (struct sockaddr*)&addr, sizeof(addr)) != SOCKET_ERROR) {
            port = p;
            break;
        }
    }

    if (port == 0) {
        printf("[HOST] Failed to bind to any port in range %d-%d\n",
               RACK_WINE_PORT_BASE, RACK_WINE_PORT_MAX);
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    if (listen(server_socket, 1) == SOCKET_ERROR) {
        printf("[HOST] Listen failed (%d)\n", WSAGetLastError());
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    // Print port for client to connect
    printf("PORT=%d\n", port);
    fflush(stdout);

    printf("[HOST] Listening on 127.0.0.1:%d\n", port);
    printf("[HOST] Waiting for connection...\n");

    // Accept one client
    SOCKET client_socket = accept(server_socket, nullptr, nullptr);
    if (client_socket == INVALID_SOCKET) {
        printf("[HOST] Accept failed (%d)\n", WSAGetLastError());
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    printf("[HOST] Client connected\n");

    // Command loop
    bool running = true;
    while (running) {
        RackWineHeader header;
        int received = recv(client_socket, (char*)&header, sizeof(header), MSG_WAITALL);

        if (received <= 0) {
            printf("[HOST] Client disconnected\n");
            break;
        }

        if (received != sizeof(header)) {
            printf("[HOST] Incomplete header (%d bytes)\n", received);
            break;
        }

        if (header.magic != RACK_WINE_MAGIC) {
            printf("[HOST] Invalid magic: 0x%08X\n", header.magic);
            break;
        }

        if (header.version != RACK_WINE_PROTOCOL_VERSION) {
            printf("[HOST] Protocol version mismatch: %u\n", header.version);
            break;
        }

        uint8_t* payload = nullptr;
        if (header.payload_size > 0) {
            payload = new uint8_t[header.payload_size];
            received = recv(client_socket, (char*)payload, header.payload_size, MSG_WAITALL);
            if (received != (int)header.payload_size) {
                printf("[HOST] Incomplete payload\n");
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

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    printf("=== rack-wine-host v0.2 ===\n\n");
    return run_server();
}
