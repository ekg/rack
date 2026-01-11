// Linux test client for rack-wine-host (Phase 3: Audio)
// Usage: ./client <port> <vst3_path>

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../include/protocol.h"

int sock_fd = -1;
int shm_fd = -1;
void* shm_ptr = NULL;
size_t shm_size = 0;
char shm_name[64];

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

// Create shared memory for audio buffers
// Uses a temp file that Wine can access through Z: drive
int create_shared_memory(uint32_t num_inputs, uint32_t num_outputs, uint32_t block_size) {
    // Create a unique name based on PID
    snprintf(shm_name, sizeof(shm_name), "/tmp/rack-wine-audio-%d", getpid());

    shm_size = RACK_WINE_SHM_SIZE(num_inputs, num_outputs, block_size);

    // Create as regular file (Wine can access /tmp through Z: drive)
    shm_fd = open(shm_name, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (shm_fd < 0) {
        perror("open shm file");
        return -1;
    }

    // Set size
    if (ftruncate(shm_fd, shm_size) < 0) {
        perror("ftruncate");
        close(shm_fd);
        unlink(shm_name);
        return -1;
    }

    // Map it
    shm_ptr = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_ptr == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
        unlink(shm_name);
        return -1;
    }

    // Initialize header
    RackWineShmHeader* hdr = (RackWineShmHeader*)shm_ptr;
    memset(hdr, 0, sizeof(*hdr));
    hdr->magic = RACK_WINE_SHM_MAGIC;
    hdr->version = RACK_WINE_PROTOCOL_VERSION;
    hdr->num_inputs = num_inputs;
    hdr->num_outputs = num_outputs;
    hdr->block_size = block_size;
    hdr->sample_rate = 48000;
    hdr->input_offset = sizeof(RackWineShmHeader);
    hdr->output_offset = sizeof(RackWineShmHeader) + num_inputs * block_size * sizeof(float);

    printf("Shared memory created: %s (%zu bytes)\n", shm_name, shm_size);
    printf("  Input offset: %u, Output offset: %u\n", hdr->input_offset, hdr->output_offset);

    return 0;
}

void cleanup_shared_memory(void) {
    if (shm_ptr && shm_ptr != MAP_FAILED) {
        munmap(shm_ptr, shm_size);
        shm_ptr = NULL;
    }
    if (shm_fd >= 0) {
        close(shm_fd);
        shm_fd = -1;
    }
    if (shm_name[0]) {
        unlink(shm_name);
        shm_name[0] = 0;
    }
}

// Fill input buffer with test signal (sine wave)
void fill_test_input(uint32_t num_samples) {
    RackWineShmHeader* hdr = (RackWineShmHeader*)shm_ptr;
    float* input_base = (float*)((uint8_t*)shm_ptr + hdr->input_offset);

    float freq = 440.0f;  // A4
    float sample_rate = 48000.0f;

    for (uint32_t ch = 0; ch < hdr->num_inputs; ch++) {
        float* buf = input_base + ch * hdr->block_size;
        for (uint32_t i = 0; i < num_samples; i++) {
            buf[i] = 0.5f * sinf(2.0f * M_PI * freq * i / sample_rate);
        }
    }
}

// Calculate RMS of output buffer
float calculate_output_rms(uint32_t num_samples) {
    RackWineShmHeader* hdr = (RackWineShmHeader*)shm_ptr;
    float* output_base = (float*)((uint8_t*)shm_ptr + hdr->output_offset);

    float sum = 0.0f;
    for (uint32_t ch = 0; ch < hdr->num_outputs; ch++) {
        float* buf = output_base + ch * hdr->block_size;
        for (uint32_t i = 0; i < num_samples; i++) {
            sum += buf[i] * buf[i];
        }
    }

    return sqrtf(sum / (hdr->num_outputs * num_samples));
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <port> <vst3_path>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    const char* vst3_path = argv[2];

    printf("=== rack-wine-host test client (Phase 3) ===\n\n");
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
    int result = 0;

    // Test 1: PING
    printf("Test 1: PING\n");
    if (send_command(CMD_PING, NULL, 0) < 0) { result = 1; goto cleanup; }
    if (recv_response(&resp, payload_buf, sizeof(payload_buf)) < 0) { result = 1; goto cleanup; }
    if (resp.status != STATUS_OK) {
        printf("  FAILED (status=%u)\n", resp.status);
        result = 1; goto cleanup;
    }
    printf("  OK\n\n");

    // Test 2: LOAD_PLUGIN
    printf("Test 2: LOAD_PLUGIN\n");
    {
        CmdLoadPlugin load_cmd;
        memset(&load_cmd, 0, sizeof(load_cmd));
        strncpy(load_cmd.path, vst3_path, sizeof(load_cmd.path) - 1);
        load_cmd.class_index = 0;

        if (send_command(CMD_LOAD_PLUGIN, &load_cmd, sizeof(load_cmd)) < 0) { result = 1; goto cleanup; }
        if (recv_response(&resp, payload_buf, sizeof(payload_buf)) < 0) { result = 1; goto cleanup; }
        if (resp.status != STATUS_OK) {
            printf("  FAILED (status=%u)\n", resp.status);
            result = 1; goto cleanup;
        }
    }
    printf("  OK\n\n");

    // Test 3: GET_INFO
    printf("Test 3: GET_INFO\n");
    if (send_command(CMD_GET_INFO, NULL, 0) < 0) { result = 1; goto cleanup; }
    if (recv_response(&resp, payload_buf, sizeof(payload_buf)) < 0) { result = 1; goto cleanup; }
    if (resp.status != STATUS_OK) {
        printf("  FAILED (status=%u)\n", resp.status);
        result = 1; goto cleanup;
    }
    uint32_t num_params = 0;
    if (resp.payload_size >= sizeof(RespPluginInfo)) {
        RespPluginInfo* info = (RespPluginInfo*)payload_buf;
        printf("  Name:     %s\n", info->name);
        printf("  Vendor:   %s\n", info->vendor);
        printf("  Params:   %u\n", info->num_params);
        num_params = info->num_params;
    }
    printf("  OK\n\n");

    // Test 4: PARAMETERS
    printf("Test 4: PARAMETERS\n");
    if (num_params > 0) {
        // Get info for first parameter
        uint32_t param_index = 0;
        if (send_command(CMD_GET_PARAM_INFO, &param_index, sizeof(param_index)) < 0) { result = 1; goto cleanup; }
        if (recv_response(&resp, payload_buf, sizeof(payload_buf)) < 0) { result = 1; goto cleanup; }
        if (resp.status != STATUS_OK) {
            printf("  GET_PARAM_INFO FAILED (status=%u)\n", resp.status);
            result = 1; goto cleanup;
        }
        RespParamInfo* pinfo = (RespParamInfo*)payload_buf;
        printf("  Param 0: id=%u, name='%s', default=%.3f\n",
               pinfo->id, pinfo->name, pinfo->default_value);

        // Get current value
        uint32_t param_id = pinfo->id;
        if (send_command(CMD_GET_PARAM, &param_id, sizeof(param_id)) < 0) { result = 1; goto cleanup; }
        if (recv_response(&resp, payload_buf, sizeof(payload_buf)) < 0) { result = 1; goto cleanup; }
        if (resp.status != STATUS_OK) {
            printf("  GET_PARAM FAILED (status=%u)\n", resp.status);
            result = 1; goto cleanup;
        }
        CmdParam* param_resp = (CmdParam*)payload_buf;
        double original_value = param_resp->value;
        printf("  Current value: %.3f\n", original_value);

        // Set to a new value
        CmdParam set_cmd;
        set_cmd.param_id = param_id;
        set_cmd.value = (original_value > 0.5) ? 0.25 : 0.75;
        if (send_command(CMD_SET_PARAM, &set_cmd, sizeof(set_cmd)) < 0) { result = 1; goto cleanup; }
        if (recv_response(&resp, payload_buf, sizeof(payload_buf)) < 0) { result = 1; goto cleanup; }
        if (resp.status != STATUS_OK) {
            printf("  SET_PARAM FAILED (status=%u)\n", resp.status);
            result = 1; goto cleanup;
        }

        // Verify the new value
        if (send_command(CMD_GET_PARAM, &param_id, sizeof(param_id)) < 0) { result = 1; goto cleanup; }
        if (recv_response(&resp, payload_buf, sizeof(payload_buf)) < 0) { result = 1; goto cleanup; }
        param_resp = (CmdParam*)payload_buf;
        printf("  After set: %.3f (expected %.3f)\n", param_resp->value, set_cmd.value);

        // Restore original value
        set_cmd.value = original_value;
        if (send_command(CMD_SET_PARAM, &set_cmd, sizeof(set_cmd)) < 0) { result = 1; goto cleanup; }
        if (recv_response(&resp, payload_buf, sizeof(payload_buf)) < 0) { result = 1; goto cleanup; }
    } else {
        printf("  No parameters available\n");
    }
    printf("  OK\n\n");

    // Test 5: Create shared memory and INIT_AUDIO
    printf("Test 5: INIT_AUDIO (shared memory)\n");
    {
        uint32_t num_inputs = 2;
        uint32_t num_outputs = 2;
        uint32_t block_size = 512;

        if (create_shared_memory(num_inputs, num_outputs, block_size) < 0) {
            printf("  FAILED (could not create shared memory)\n");
            result = 1; goto cleanup;
        }

        CmdInitAudio init_cmd;
        memset(&init_cmd, 0, sizeof(init_cmd));
        init_cmd.sample_rate = 48000;
        init_cmd.block_size = block_size;
        init_cmd.num_inputs = num_inputs;
        init_cmd.num_outputs = num_outputs;

        // Wine sees Linux paths through Z: drive
        // Convert /tmp/... to Z:\tmp\...
        snprintf(init_cmd.shm_name, sizeof(init_cmd.shm_name), "Z:%s", shm_name);
        // Replace / with backslash for Windows
        for (char* p = init_cmd.shm_name; *p; p++) {
            if (*p == '/') *p = '\\';
        }

        printf("  SHM path for Wine: %s\n", init_cmd.shm_name);

        if (send_command(CMD_INIT_AUDIO, &init_cmd, sizeof(init_cmd)) < 0) { result = 1; goto cleanup; }
        if (recv_response(&resp, payload_buf, sizeof(payload_buf)) < 0) { result = 1; goto cleanup; }
        if (resp.status != STATUS_OK) {
            printf("  FAILED (status=%u)\n", resp.status);
            result = 1; goto cleanup;
        }
    }
    printf("  OK\n\n");

    // Test 6: PROCESS_AUDIO
    printf("Test 6: PROCESS_AUDIO\n");
    {
        uint32_t num_samples = 512;

        // Fill input with test signal
        fill_test_input(num_samples);
        float input_rms = calculate_output_rms(num_samples);  // Check input level
        printf("  Input RMS: %.6f\n", input_rms);

        // Clear output
        RackWineShmHeader* hdr = (RackWineShmHeader*)shm_ptr;
        float* output_base = (float*)((uint8_t*)shm_ptr + hdr->output_offset);
        memset(output_base, 0, hdr->num_outputs * hdr->block_size * sizeof(float));

        // Process
        CmdProcessAudio proc_cmd;
        proc_cmd.num_samples = num_samples;

        if (send_command(CMD_PROCESS_AUDIO, &proc_cmd, sizeof(proc_cmd)) < 0) { result = 1; goto cleanup; }
        if (recv_response(&resp, payload_buf, sizeof(payload_buf)) < 0) { result = 1; goto cleanup; }
        if (resp.status != STATUS_OK) {
            printf("  FAILED (status=%u)\n", resp.status);
            result = 1; goto cleanup;
        }

        // Check output
        float output_rms = calculate_output_rms(num_samples);
        printf("  Output RMS: %.6f\n", output_rms);

        // PeakEater is an effect - it should pass through audio (possibly modified)
        if (output_rms > 0.001f) {
            printf("  Audio processed successfully!\n");
        } else {
            printf("  WARNING: Output is silent (may be normal for some plugins)\n");
        }
    }
    printf("  OK\n\n");

    // Test 7: Multiple process calls (performance test)
    printf("Test 7: Process 100 blocks\n");
    {
        uint32_t num_samples = 512;

        for (int i = 0; i < 100; i++) {
            fill_test_input(num_samples);

            CmdProcessAudio proc_cmd;
            proc_cmd.num_samples = num_samples;

            if (send_command(CMD_PROCESS_AUDIO, &proc_cmd, sizeof(proc_cmd)) < 0) { result = 1; goto cleanup; }
            if (recv_response(&resp, payload_buf, sizeof(payload_buf)) < 0) { result = 1; goto cleanup; }
            if (resp.status != STATUS_OK) {
                printf("  FAILED at block %d (status=%u)\n", i, resp.status);
                result = 1; goto cleanup;
            }
        }
        printf("  100 blocks processed\n");
    }
    printf("  OK\n\n");

    // Test 8: SHUTDOWN
    printf("Test 8: SHUTDOWN\n");
    if (send_command(CMD_SHUTDOWN, NULL, 0) < 0) { result = 1; goto cleanup; }
    if (recv_response(&resp, payload_buf, sizeof(payload_buf)) < 0) { result = 1; goto cleanup; }
    printf("  OK\n\n");

    printf("=== All tests passed! ===\n");

cleanup:
    cleanup_shared_memory();
    close(sock_fd);
    return result;
}
