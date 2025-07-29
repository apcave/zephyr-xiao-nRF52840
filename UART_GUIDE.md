# nRF52840 Xiao BLE UART Configuration Guide

## Overview
This document describes the proper UART configuration for the nRF52840 Xiao BLE board with OpenOCD programming support.

## Hardware Connections

### UART Pins
The nRF52840 Xiao BLE uses the following pins for UART communication:
- **TX (Transmit)**: Pin D6 (P1.11) - Connect to RX of your USB-UART adapter
- **RX (Receive)**: Pin D7 (P1.12) - Connect to TX of your USB-UART adapter  
- **GND**: Connect to GND of your USB-UART adapter
- **Power**: 3.3V (optional, if not powered via USB)

### Debug Probe Connection
- **SWD**: Uses Raspberry Pi Pico as debug probe via OpenOCD

## Software Configuration

### Project Files Modified
1. **prj.conf**: Disabled USB, enabled UART console
2. **boards/xiao_ble.overlay**: Configured correct UART pins (P1.11/P1.12)

### Key Configuration Options
```properties
# UART Console Only
CONFIG_CONSOLE=y
CONFIG_UART_CONSOLE=y
CONFIG_SERIAL=y

# USB Disabled
CONFIG_USB_DEVICE_STACK=n

# Shell via UART
CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_SERIAL=y
```

## Build and Flash Instructions

### Build
```bash
west build -b xiao_ble
```

### Flash with OpenOCD
```bash
./flash-openocd.sh merged
```

### Monitor UART Console
```bash
./monitor-usb.sh
# OR manually:
screen /dev/ttyUSB0 115200
```

## Expected Console Output
After flashing, you should see:
- MCUboot signature verification messages
- Application startup logs
- Shell prompt for interactive commands
- LED blinking status

## Pin Reference
| Xiao BLE Pin | nRF52840 Pin | Function | Connection |
|--------------|--------------|----------|------------|
| D6           | P1.11        | UART TX  | → RX of UART adapter |
| D7           | P1.12        | UART RX  | ← TX of UART adapter |
| GND          | GND          | Ground   | GND of UART adapter |

## Troubleshooting
1. **No output**: Check TX/RX are not swapped
2. **Garbled text**: Verify baud rate is 115200
3. **No connection**: Ensure ground connection is made
4. **Flash errors**: Run `nrf52_recover` in OpenOCD first

## Tools Created
- `flash-openocd.sh`: Flash using OpenOCD with recovery
- `monitor-usb.sh`: Monitor UART console
- `rtt-console.sh`: Alternative RTT debugging
- `openocd.cfg`: OpenOCD configuration for nRF52840

The UART console is now properly configured on pins D6/D7 with USB completely disabled.
