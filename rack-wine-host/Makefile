# rack-wine-host Makefile
# Cross-compile Windows executable using MinGW

CXX = x86_64-w64-mingw32-g++
CXXFLAGS = -std=c++17 -Wall -O2 -static
LDFLAGS = -static -lole32 -loleaut32

TARGET = rack-wine-host.exe
SRC = src/main.cpp

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Built $(TARGET)"

clean:
	rm -f $(TARGET)

# Test with Wine (requires a .vst3 plugin path as argument)
test: $(TARGET)
	@echo "Run: wine ./$(TARGET) /path/to/plugin.vst3"
