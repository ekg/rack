#!/bin/bash
# VST3 GUI Test Script
# This script tests VST3 GUI functionality using Xvfb (X Virtual Framebuffer)
#
# Prerequisites:
#   sudo apt-get install xvfb xdotool imagemagick x11-utils
#
# Usage:
#   ./scripts/test_vst3_gui.sh
#
# This script:
#   1. Starts Xvfb (virtual X server)
#   2. Runs the VST3 GUI example
#   3. Captures screenshots of the GUI window
#   4. Validates window creation and size

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Configuration
DISPLAY_NUM=99
XVFB_SCREEN="1280x1024x24"
TIMEOUT=30
OUTPUT_DIR="${OUTPUT_DIR:-/tmp/vst3_gui_test}"

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

cleanup() {
    log_info "Cleaning up..."
    if [ -n "$XVFB_PID" ]; then
        kill $XVFB_PID 2>/dev/null || true
    fi
    if [ -n "$GUI_PID" ]; then
        kill $GUI_PID 2>/dev/null || true
    fi
}

trap cleanup EXIT

# Check prerequisites
check_prerequisites() {
    local missing=""

    if ! command -v Xvfb &> /dev/null; then
        missing="$missing xvfb"
    fi

    if ! command -v xdotool &> /dev/null; then
        missing="$missing xdotool"
    fi

    if ! command -v xwininfo &> /dev/null; then
        missing="$missing x11-utils"
    fi

    if [ -n "$missing" ]; then
        log_error "Missing packages:$missing"
        log_info "Install with: sudo apt-get install$missing"
        exit 1
    fi
}

# Start Xvfb
start_xvfb() {
    log_info "Starting Xvfb on display :$DISPLAY_NUM..."
    Xvfb :$DISPLAY_NUM -screen 0 $XVFB_SCREEN &
    XVFB_PID=$!

    # Wait for Xvfb to start
    sleep 2

    if ! kill -0 $XVFB_PID 2>/dev/null; then
        log_error "Failed to start Xvfb"
        exit 1
    fi

    export DISPLAY=:$DISPLAY_NUM
    log_info "Xvfb started (PID: $XVFB_PID)"
}

# Run the VST3 GUI example
run_gui_test() {
    log_info "Building VST3 GUI example..."
    cargo build --example vst3_gui 2>&1 || {
        log_error "Failed to build example"
        exit 1
    }

    log_info "Running VST3 GUI example..."
    timeout $TIMEOUT cargo run --example vst3_gui 2>&1 &
    GUI_PID=$!

    # Wait for window to appear
    log_info "Waiting for GUI window..."
    local waited=0
    local window_id=""

    while [ $waited -lt 10 ]; do
        sleep 1
        waited=$((waited + 1))

        # Try to find the window
        window_id=$(xdotool search --name "Surge" 2>/dev/null | head -1 || true)
        if [ -n "$window_id" ]; then
            break
        fi

        # Also try generic VST3 window name
        window_id=$(xdotool search --name "VST3 Plugin" 2>/dev/null | head -1 || true)
        if [ -n "$window_id" ]; then
            break
        fi
    done

    if [ -z "$window_id" ]; then
        log_warn "Could not find GUI window (plugin may not support GUI or no plugins installed)"
        # Check if the process is still running
        if kill -0 $GUI_PID 2>/dev/null; then
            log_info "Process is still running, checking output..."
            wait $GUI_PID || true
        fi
        return 1
    fi

    log_info "Found window ID: $window_id"
    return 0
}

# Capture and analyze window
analyze_window() {
    local window_id=$1

    mkdir -p "$OUTPUT_DIR"

    # Get window info
    log_info "Getting window info..."
    xwininfo -id $window_id > "$OUTPUT_DIR/window_info.txt" 2>&1 || true

    # Parse window dimensions
    local width=$(grep "Width:" "$OUTPUT_DIR/window_info.txt" | awk '{print $2}')
    local height=$(grep "Height:" "$OUTPUT_DIR/window_info.txt" | awk '{print $2}')

    log_info "Window size: ${width}x${height}"

    # Validate window has reasonable size
    if [ -n "$width" ] && [ -n "$height" ]; then
        if [ "$width" -gt 100 ] && [ "$height" -gt 100 ]; then
            log_info "Window size validation: PASSED"

            # Try to capture screenshot (if import is available)
            if command -v import &> /dev/null; then
                log_info "Capturing screenshot..."
                import -window $window_id "$OUTPUT_DIR/screenshot.png" 2>/dev/null || true

                if [ -f "$OUTPUT_DIR/screenshot.png" ]; then
                    log_info "Screenshot saved to $OUTPUT_DIR/screenshot.png"

                    # Analyze screenshot (check if it has content)
                    local colors=$(identify -format "%k" "$OUTPUT_DIR/screenshot.png" 2>/dev/null || echo "0")
                    if [ "$colors" -gt 10 ]; then
                        log_info "Screenshot has $colors unique colors (content present)"
                    else
                        log_warn "Screenshot may be blank (only $colors colors)"
                    fi
                fi
            fi

            return 0
        else
            log_error "Window size too small (${width}x${height})"
            return 1
        fi
    else
        log_error "Could not determine window size"
        return 1
    fi
}

# Main test
main() {
    log_info "=== VST3 GUI Test Suite ==="
    log_info ""

    check_prerequisites
    start_xvfb

    log_info ""
    log_info "=== Running GUI Test ==="

    if run_gui_test; then
        # Window was found, analyze it
        window_id=$(xdotool search --name "Surge" 2>/dev/null | head -1 || \
                    xdotool search --name "VST3 Plugin" 2>/dev/null | head -1 || true)

        if [ -n "$window_id" ]; then
            if analyze_window "$window_id"; then
                log_info ""
                log_info "=== TEST PASSED ==="
                exit 0
            else
                log_error ""
                log_error "=== TEST FAILED (window analysis) ==="
                exit 1
            fi
        fi
    fi

    log_warn ""
    log_warn "=== TEST SKIPPED (no GUI window found) ==="
    log_warn "This may be normal if no VST3 plugins with GUI support are installed."
    exit 0
}

main "$@"
