#!/bin/bash

# OpenOCD flash script for nRF52840 Xiao BLE
# Uses Raspberry Pi Pico as debug probe

set -e

OPENOCD_CONFIG="openocd.cfg"
BUILD_DIR="build"

echo "=== OpenOCD Flash Script for nRF52840 Xiao BLE ==="
echo ""

# Check if build exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "Error: Build directory not found. Run 'west build' first."
    exit 1
fi

# Function to check if files exist
check_file() {
    if [ ! -f "$1" ]; then
        echo "Error: $1 not found"
        exit 1
    fi
}

# Flash options
case "${1:-merged}" in
    "bootloader"|"mcuboot")
        echo "Flashing MCUboot bootloader only..."
        check_file "$BUILD_DIR/mcuboot/zephyr/zephyr.hex"
        echo "Running nRF52 recovery first..."
        openocd -f "$OPENOCD_CONFIG" -c "init; nrf52_recover; exit" >/dev/null 2>&1
        openocd -f "$OPENOCD_CONFIG" -c "init; program_mcuboot; exit"
        ;;
    
    "app"|"application") 
        echo "Flashing application only (requires bootloader already flashed)..."
        check_file "$BUILD_DIR/reset/zephyr/zephyr.signed.hex"
        openocd -f "$OPENOCD_CONFIG" -c "init; program_app; exit"
        ;;
    
    "merged"|"full"|*)
        echo "Flashing merged image (bootloader + application)..."
        check_file "$BUILD_DIR/merged.hex"
        echo "Running nRF52 recovery first..."
        openocd -f "$OPENOCD_CONFIG" -c "init; nrf52_recover; exit" >/dev/null 2>&1
        openocd -f "$OPENOCD_CONFIG" -c "init; halt; program $BUILD_DIR/merged.hex verify reset; exit"
        ;;
esac

echo ""
echo "Flash complete!"
echo ""
echo "Available monitoring options:"
echo "  USB CDC ACM: sudo screen /dev/ttyACM0 115200"
echo "  RTT: openocd -f openocd.cfg -c \"init; rtt server start 9090 0; exit\" &"
echo "       then: nc localhost 9090"
echo ""
echo "Usage: $0 [merged|bootloader|app]"
echo "  merged     - Flash complete system (default)"
echo "  bootloader - Flash MCUboot only"  
echo "  app        - Flash application only"
