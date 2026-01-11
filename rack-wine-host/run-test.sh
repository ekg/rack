#!/bin/bash
# Test script for rack-wine-host IPC (TCP version)
set -e

VST3_PATH="${1:-/tmp/win_vst3_test/peakeater_artefacts/Release/VST3/peakeater.vst3}"

echo "=== rack-wine-host IPC Test ==="
echo "VST3: $VST3_PATH"
echo ""

# Build if needed
make -q all 2>/dev/null || make all

# Start Wine host in background, capture output
echo "Starting Wine host..."
TMPFILE=$(mktemp)
wine ./rack-wine-host.exe > "$TMPFILE" 2>&1 &
HOST_PID=$!

# Wait for port to be printed
sleep 2

# Extract port from output
PORT=$(grep "PORT=" "$TMPFILE" | cut -d= -f2)

if [ -z "$PORT" ]; then
    echo "ERROR: Could not get port from host"
    echo "Host output:"
    cat "$TMPFILE"
    kill $HOST_PID 2>/dev/null || true
    rm "$TMPFILE"
    exit 1
fi

echo "Port: $PORT"
echo ""

# Run test client
echo "Running test client..."
echo ""
./test-client "$PORT" "$VST3_PATH"
RESULT=$?

# Wait for host to exit
wait $HOST_PID 2>/dev/null || true

echo ""
echo "Host output:"
cat "$TMPFILE"

rm "$TMPFILE"

exit $RESULT
