#!/bin/bash

# USB CDC ACM Console monitor for nRF52840 Xiao BLE

DEVICE="/dev/ttyACM1"
BAUDRATE="115200"

echo "=== USB CDC ACM Console Monitor ==="
echo "Device: $DEVICE"
echo "Baudrate: $BAUDRATE"
echo ""

# Check if device exists
if [ ! -e "$DEVICE" ]; then
    echo "Error: Device $DEVICE not found"
    echo "Available devices:"
    ls -la /dev/ttyACM* 2>/dev/null || echo "  No /dev/ttyACM* devices found"
    echo ""
    echo "Make sure the device is connected and the application is running"
    exit 1
fi

echo "Starting console monitor..."
echo "Press Ctrl+A, K to exit screen session"
echo ""

# Use screen to monitor the console
exec screen "$DEVICE" "$BAUDRATE"
