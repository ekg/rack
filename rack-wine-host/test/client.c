// Linux test client for rack-wine-host (TCP version)
// Usage: ./client <port> <vst3_path>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../include/protocol.h"

int sock_fd = -1;

int send_command(uint32_t cmd, const void* payload, uint32_t payload_size) {
    RackWineHeader header;
    header.magic = RACK_WINE_MAGIC;
    header.version = RACK_WINE_PROTOCOL_VERSION;
    header.command = cmd;
    header.payload_size = payload_size;

    if (write(sock_fd, &header, sizeof(header)) != sizeof(header)) {
        perror("write header");
        return -1;
    }

    if (payload_size > 0 && payload) {
        if (write(sock_fd, payload, payload_size) != (ssize_t)payload_size) {
            perror("write payload");
            return -1;
        }
    }

    return 0;
}

int recv_response(RackWineResponse* resp, void* payload, uint32_t max_payload) {
    if (read(sock_fd, resp, sizeof(*resp)) != sizeof(*resp)) {
        perror("read response");
        return -1;
    }

    if (resp->magic != RACK_WINE_RESPONSE_MAGIC) {
        fprintf(stderr, "Invalid response magic: 0x%08X\n", resp->magic);
        return -1;
    }

    if (resp->payload_size > 0) {
        if (resp->payload_size > max_payload) {
            fprintf(stderr, "Payload too large: %u > %u\n", resp->payload_size, max_payload);
            return -1;
        }
        if (read(sock_fd, payload, resp->payload_size) != (ssize_t)resp->payload_size) {
            perror("read payload");
            return -1;
        }
    }

    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <port> <vst3_path>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    const char* vst3_path = argv[2];

    printf("=== rack-wine-host test client ===\n\n");
    printf("Port:   %d\n", port);
    printf("Plugin: %s\n\n", vst3_path);

    // Connect to TCP socket
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    printf("Connecting to 127.0.0.1:%d...\n", port);
    if (connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sock_fd);
        return 1;
    }
    printf("Connected!\n\n");

    RackWineResponse resp;
    uint8_t payload_buf[4096];

    // Test 1: PING
    printf("Test 1: PING\n");
    if (send_command(CMD_PING, NULL, 0) < 0) return 1;
    if (recv_response(&resp, payload_buf, sizeof(payload_buf)) < 0) return 1;
    printf("  Response: status=%u\n", resp.status);
    if (resp.status != STATUS_OK) {
        printf("  FAILED\n");
        return 1;
    }
    printf("  OK\n\n");

    // Test 2: LOAD_PLUGIN
    printf("Test 2: LOAD_PLUGIN\n");
    CmdLoadPlugin load_cmd;
    memset(&load_cmd, 0, sizeof(load_cmd));
    strncpy(load_cmd.path, vst3_path, sizeof(load_cmd.path) - 1);
    load_cmd.class_index = 0;

    if (send_command(CMD_LOAD_PLUGIN, &load_cmd, sizeof(load_cmd)) < 0) return 1;
    if (recv_response(&resp, payload_buf, sizeof(payload_buf)) < 0) return 1;
    printf("  Response: status=%u\n", resp.status);
    if (resp.status != STATUS_OK) {
        printf("  FAILED\n");
        return 1;
    }
    printf("  OK\n\n");

    // Test 3: GET_INFO
    printf("Test 3: GET_INFO\n");
    if (send_command(CMD_GET_INFO, NULL, 0) < 0) return 1;
    if (recv_response(&resp, payload_buf, sizeof(payload_buf)) < 0) return 1;
    printf("  Response: status=%u, payload=%u bytes\n", resp.status, resp.payload_size);
    if (resp.status != STATUS_OK) {
        printf("  FAILED\n");
        return 1;
    }
    if (resp.payload_size >= sizeof(RespPluginInfo)) {
        RespPluginInfo* info = (RespPluginInfo*)payload_buf;
        printf("  Name:     %s\n", info->name);
        printf("  Vendor:   %s\n", info->vendor);
        printf("  Category: %s\n", info->category);
        printf("  UID:      %s\n", info->uid);
        printf("  Inputs:   %u\n", info->num_audio_inputs);
        printf("  Outputs:  %u\n", info->num_audio_outputs);
    }
    printf("  OK\n\n");

    // Test 4: SHUTDOWN
    printf("Test 4: SHUTDOWN\n");
    if (send_command(CMD_SHUTDOWN, NULL, 0) < 0) return 1;
    if (recv_response(&resp, payload_buf, sizeof(payload_buf)) < 0) return 1;
    printf("  Response: status=%u\n", resp.status);
    printf("  OK\n\n");

    close(sock_fd);
    printf("=== All tests passed! ===\n");

    return 0;
}
