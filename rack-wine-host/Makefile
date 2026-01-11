# rack-wine-host Makefile
# Cross-compile Windows executable using MinGW

# Windows target (MinGW)
CXX = x86_64-w64-mingw32-g++
CXXFLAGS = -std=c++17 -Wall -O2 -static -Iinclude
LDFLAGS = -static -lws2_32 -lole32 -loleaut32

# Linux target (native)
CC_LINUX = gcc
CFLAGS_LINUX = -std=c11 -Wall -O2 -Iinclude

TARGET = rack-wine-host.exe
TEST_CLIENT = test-client
SRC = src/main.cpp

.PHONY: all clean test client

all: $(TARGET) $(TEST_CLIENT)

$(TARGET): $(SRC) include/protocol.h
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) $(LDFLAGS)
	@echo "Built $(TARGET)"

$(TEST_CLIENT): test/client.c include/protocol.h
	$(CC_LINUX) $(CFLAGS_LINUX) -o $@ test/client.c
	@echo "Built $(TEST_CLIENT)"

client: $(TEST_CLIENT)

clean:
	rm -f $(TARGET) $(TEST_CLIENT)

# Run test: start host in background, run client
test: $(TARGET) $(TEST_CLIENT)
	@echo "=== Test Instructions ==="
	@echo "Terminal 1: wine ./$(TARGET)"
	@echo "Terminal 2: ./$(TEST_CLIENT) <socket_path> <vst3_path>"
	@echo ""
	@echo "Example:"
	@echo "  wine ./$(TARGET)"
	@echo "  # Note the SOCKET_PATH output"
	@echo "  ./$(TEST_CLIENT) /tmp/rack-wine-host-XXXX.sock /path/to/plugin.vst3"
