#!/bin/bash

# RTT Console script using OpenOCD
# Provides real-time debug output from nRF52840

echo "Starting RTT console..."
echo "Press Ctrl+C to exit"
echo ""

# Start OpenOCD with RTT enabled in background
openocd -f openocd.cfg -c "init; rtt setup 0x20000000 0x40000 \"SEGGER RTT\"; rtt start; rtt server start 9090 0" &
OPENOCD_PID=$!

# Give OpenOCD time to start
sleep 2

echo "OpenOCD started with PID $OPENOCD_PID"
echo "RTT server listening on port 9090"
echo "Connecting to RTT..."
echo ""

# Function to cleanup on exit
cleanup() {
    echo ""
    echo "Stopping RTT console..."
    kill $OPENOCD_PID 2>/dev/null
    exit 0
}

# Set trap for cleanup
trap cleanup SIGINT SIGTERM

# Connect to RTT server
nc localhost 9090

# Cleanup if nc exits
cleanup
