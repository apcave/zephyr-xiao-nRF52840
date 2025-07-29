# Zephyr Blinky Default Project

A clean Zephyr RTOS project for the nRF52840 Seeed Xiao BLE board with default MCUboot configuration.

## Overview

This project demonstrates a basic LED blinking application using Zephyr RTOS with:
- MCUboot bootloader with default RSA signature verification
- USB CDC ACM console output (default for Xiao BLE)
- Basic Bluetooth LE support
- Standard flash partition layout
- Structured logging

## Hardware Requirements

- **Board**: Seeed Xiao nRF52840 BLE (Sense)
- **Debug Probe**: Any SWD-compatible debugger (optional)
- **USB Cable**: USB-C for power and programming

## Project Structure

```
.
├── CMakeLists.txt              # Main CMake configuration
├── prj.conf                    # Application configuration
├── sysbuild.conf              # Sysbuild configuration
├── west.yml                   # West manifest (if standalone)
├── src/
│   └── main.c                 # Main application source
├── boards/
│   └── xiao_ble.overlay      # Board-specific device tree overlay
└── sysbuild/
    └── mcuboot.conf          # MCUboot configuration
```

## Features

### Default Configurations
- **Console**: USB CDC ACM (plug-and-play USB serial)
- **Signature Verification**: RSA (standard MCUboot default)
- **Flash Layout**: Standard dual-slot OTA layout
- **Logging**: Structured logging at INFO level
- **Bluetooth**: Basic peripheral mode enabled

### Application
- Simple LED blinky with 1-second intervals
- Structured logging output via USB serial
- Error handling and status reporting
- Standard Zephyr GPIO and logging APIs

## Building

### Prerequisites
```bash
# Ensure west and Zephyr SDK are installed
west --version
```

### Build Commands
```bash
# Build the project
west build -b xiao_ble

# Clean build
west build -b xiao_ble -t clean

# Flash via USB DFU (requires bootloader mode)
west flash --runner dfu-util

# Flash via SWD debugger
west flash --runner openocd
```

## Console Output

Connect to the USB serial port (typically `/dev/ttyACM0` on Linux) at any baud rate to see output:

```
[00:00:00.123,000] <inf> main: Starting Zephyr Blinky application on xiao_ble
[00:00:00.124,000] <inf> main: LED configured successfully
[00:00:00.125,000] <inf> main: LED turned on
[00:00:01.125,000] <inf> main: LED turned off
[00:00:02.125,000] <inf> main: LED turned on
```

## Flash Layout

| Partition | Address | Size | Purpose |
|-----------|---------|------|---------|
| MCUboot | 0x00000 | 48KB | Bootloader |
| Slot 0 | 0x0C000 | 472KB | Primary app |
| Slot 1 | 0x82000 | 472KB | Update app |
| Storage | 0xF8000 | 32KB | Settings/data |

## Development

### Adding Features
1. Modify `src/main.c` for application logic
2. Update `prj.conf` for Zephyr configuration options
3. Use `boards/xiao_ble.overlay` for hardware-specific settings
4. Update `sysbuild/mcuboot.conf` for bootloader changes

### Debugging
- Use `west build -b xiao_ble -- -DCONFIG_LOG_DEFAULT_LEVEL=4` for debug logging
- Connect debugger and use `west debug` for GDB session
- Monitor USB serial port for runtime logs

## Default vs Custom

This project uses **default** Zephyr and MCUboot configurations:
- Standard RSA signature verification (not disabled)
- USB CDC ACM console (not UART)
- Default partition sizes and layout
- Standard Bluetooth configuration
- No custom overlays for pins or peripherals

Compare this with custom projects that may disable signature verification, use UART console, or have modified partition layouts.

## Troubleshooting

### Build Issues
```bash
# Clean and rebuild
rm -rf build
west build -b xiao_ble
```

### Flash Issues
```bash
# Try different runners
west flash --runner openocd
west flash --runner dfu-util
west flash --runner nrfjprog
```

### Console Issues
- Ensure USB cable supports data (not power-only)
- Check for `/dev/ttyACM*` devices on Linux
- No specific baud rate required for USB CDC ACM

## License

SPDX-License-Identifier: Apache-2.0
