# USB CDC ACM Device Guide

This project creates a USB CDC ACM (Communication Device Class - Abstract Control Model) device that appears as a virtual serial port on your computer.

## Configuration

The firmware is configured with:
- **Shell on UART**: Available on pins D6(TX)/D7(RX) at 115200 baud
- **USB CDC ACM**: Virtual serial port device (VID: 0x2FE3, PID: 0x0100)
- **Shell Commands**: `led on`, `led off`, `blink`, `status`

## Testing USB CDC ACM Device

### 1. Check USB Device Enumeration

After flashing the firmware, check if the USB device appears:

```bash
# Check USB devices
lsusb | grep 2fe3

# Check for ttyACM device
ls -la /dev/ttyACM*

# Check USB device details
dmesg | tail -20
```

### 2. Connect to USB CDC ACM Device

If the device appears as `/dev/ttyACM0`:

```bash
# Using screen
sudo screen /dev/ttyACM0 115200

# Using minicom
sudo minicom -D /dev/ttyACM0 -b 115200

# Using cu
sudo cu -l /dev/ttyACM0 -s 115200
```

### 3. Expected Behavior

- **UART Console**: Shell commands available on D6/D7 pins
- **USB CDC ACM**: Virtual serial port (may show logging output but shell is on UART)
- **LED Blinking**: Default 1-second blink pattern
- **Logging**: System information and status messages

### 4. Troubleshooting

If USB device doesn't appear:

1. **Check USB Connection**: Ensure USB cable supports data (not power-only)
2. **USB Enumeration Issues**: Try different USB ports or cables
3. **Driver Issues**: Some systems may need CDC ACM drivers
4. **Permissions**: Use `sudo` for device access

### 5. USB Device Information

- **Vendor ID**: 0x2FE3 (Zephyr test VID)
- **Product ID**: 0x0100 (Zephyr test PID)
- **Product Name**: "Zephyr Dual Console"
- **Interface**: CDC ACM (Virtual Serial Port)

## Development Notes

- The USB CDC ACM device is created by the Zephyr USB device stack
- Console output goes to UART by default
- Shell commands are accessible via UART console
- USB CDC ACM provides a virtual serial port interface
- For production, configure proper VID/PID values

## Next Steps

To enable shell on USB CDC ACM as well, additional configuration would be needed for dual console support.
