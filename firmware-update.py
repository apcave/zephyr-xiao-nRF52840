#!/usr/bin/env python3
"""
Firmware Update Client for Custom OTA Service
Sends firmware files over Bluetooth LE to the nRF52840 device
"""

import asyncio
import struct
import hashlib
import zlib
from bleak import BleakClient, BleakScanner
import argparse
import os

# Service and Characteristic UUIDs
DATA_STREAM_SERVICE_UUID = "12345678-1234-5678-9ABC-DEF012345678"
FIRMWARE_UPDATE_CHAR_UUID = "12345678-1234-5678-9ABC-DEF01234567B"
FIRMWARE_STATUS_CHAR_UUID = "12345678-1234-5678-9ABC-DEF01234567C"
FIRMWARE_CONTROL_CHAR_UUID = "12345678-1234-5678-9ABC-DEF01234567D"

# Firmware control commands
FW_CMD_START = 0x01
FW_CMD_RESET = 0x02
FW_CMD_VERIFY = 0x03
FW_CMD_FLASH = 0x04
FW_CMD_ABORT = 0x05

# Status codes
FW_STATUS_IDLE = 0x00
FW_STATUS_RECEIVING = 0x01
FW_STATUS_RECEIVED = 0x02
FW_STATUS_VERIFYING = 0x03
FW_STATUS_VERIFIED = 0x04
FW_STATUS_FLASHING = 0x05
FW_STATUS_COMPLETE = 0x06
FW_STATUS_ERROR = 0xFF

STATUS_NAMES = {
    FW_STATUS_IDLE: "IDLE",
    FW_STATUS_RECEIVING: "RECEIVING",
    FW_STATUS_RECEIVED: "RECEIVED",
    FW_STATUS_VERIFYING: "VERIFYING",
    FW_STATUS_VERIFIED: "VERIFIED",
    FW_STATUS_FLASHING: "FLASHING",
    FW_STATUS_COMPLETE: "COMPLETE",
    FW_STATUS_ERROR: "ERROR"
}

class FirmwareUpdater:
    def __init__(self, device_name="AlexBlue"):
        self.device_name = device_name
        self.client = None
        self.status_received = asyncio.Event()
        self.last_status = None
        
    async def find_device(self):
        """Find the target device by name"""
        print(f"Scanning for device '{self.device_name}'...")
        devices = await BleakScanner.discover(timeout=10.0)
        
        for device in devices:
            if device.name == self.device_name:
                print(f"Found device: {device.name} [{device.address}]")
                return device.address
        
        raise Exception(f"Device '{self.device_name}' not found")
    
    async def connect(self):
        """Connect to the device"""
        address = await self.find_device()
        self.client = BleakClient(address)
        await self.client.connect()
        print(f"Connected to {self.device_name}")
        
        # Enable status notifications
        await self.client.start_notify(FIRMWARE_STATUS_CHAR_UUID, self.status_notification_handler)
        print("Status notifications enabled")
        
        # Longer delay to ensure notifications are properly subscribed
        await asyncio.sleep(1.0)
    
    async def disconnect(self):
        """Disconnect from the device"""
        if self.client and self.client.is_connected:
            await self.client.disconnect()
            print("Disconnected")
    
    def status_notification_handler(self, sender, data):
        """Handle firmware status notifications"""
        print(f"Received notification: {len(data)} bytes - {data.hex()}")
        if len(data) >= 8:
            status = data[0]
            received = struct.unpack('<I', data[1:5])[0]
            # Expected size is in bytes 5-7 (only 3 bytes), construct 4-byte value
            expected = data[5] | (data[6] << 8) | (data[7] << 16)
            
            status_name = STATUS_NAMES.get(status, f"UNKNOWN(0x{status:02X})")
            print(f"Status: {status_name}, Received: {received}/{expected} bytes")
            
            self.last_status = status
            self.status_received.set()
        else:
            print(f"Invalid notification data length: {len(data)}")
    
    async def wait_for_status(self, expected_status=None, timeout=30.0):
        """Wait for a status notification"""
        try:
            await asyncio.wait_for(self.status_received.wait(), timeout=timeout)
            self.status_received.clear()
            
            if expected_status is not None and self.last_status != expected_status:
                status_name = STATUS_NAMES.get(self.last_status, f"0x{self.last_status:02X}")
                expected_name = STATUS_NAMES.get(expected_status, f"0x{expected_status:02X}")
                raise Exception(f"Expected status {expected_name}, got {status_name}")
                
            return self.last_status
        except asyncio.TimeoutError:
            raise Exception(f"Timeout waiting for status notification")
    
    async def read_status(self):
        """Read current firmware status"""
        data = await self.client.read_gatt_char(FIRMWARE_STATUS_CHAR_UUID)
        if len(data) >= 8:
            status = data[0]
            received = struct.unpack('<I', data[1:5])[0]
            # Expected size is in bytes 5-7 (only 3 bytes), construct 4-byte value
            expected = data[5] | (data[6] << 8) | (data[7] << 16)
            return status, received, expected
        return None, 0, 0
    
    async def send_command(self, command, data=b''):
        """Send a firmware control command"""
        cmd_data = bytes([command]) + data
        await self.client.write_gatt_char(FIRMWARE_CONTROL_CHAR_UUID, cmd_data)
    
    async def send_firmware_chunk(self, chunk):
        """Send a firmware data chunk"""
        await self.client.write_gatt_char(FIRMWARE_UPDATE_CHAR_UUID, chunk, response=False)
    
    def calculate_crc32(self, data):
        """Calculate CRC32 (same algorithm as device)"""
        crc = 0xFFFFFFFF
        polynomial = 0xEDB88320
        
        for byte in data:
            crc ^= byte
            for _ in range(8):
                if crc & 1:
                    crc = (crc >> 1) ^ polynomial
                else:
                    crc >>= 1
        
        return (~crc) & 0xFFFFFFFF
    
    async def update_firmware(self, firmware_path, chunk_size=240):
        """Update firmware from file"""
        if not os.path.exists(firmware_path):
            raise Exception(f"Firmware file not found: {firmware_path}")
        
        # Read firmware file
        with open(firmware_path, 'rb') as f:
            firmware_data = f.read()
        
        firmware_size = len(firmware_data)
        print(f"Firmware file: {firmware_path}")
        print(f"Firmware size: {firmware_size} bytes")
        
        # Check if firmware fits in device buffer (64KB = 65536 bytes)
        max_size = 65536
        if firmware_size > max_size:
            raise Exception(f"Firmware too large: {firmware_size} bytes (max: {max_size} bytes)")
        
        # Calculate CRC32
        crc32 = self.calculate_crc32(firmware_data)
        print(f"Firmware CRC32: 0x{crc32:08X}")
        
        try:
            # Step 1: Start firmware update
            print("\\n1. Starting firmware update...")
            size_data = struct.pack('<I', firmware_size)
            print(f"   Sending START command with size: {firmware_size} bytes")
            print(f"   Size data: {size_data.hex()}")
            await self.send_command(FW_CMD_START, size_data)
            
            # Add a small delay and check status manually if no notification arrives
            try:
                await self.wait_for_status(FW_STATUS_RECEIVING, timeout=5.0)
            except Exception as e:
                print(f"   No notification received, checking status manually...")
                status, received, expected = await self.read_status()
                status_name = STATUS_NAMES.get(status, f"UNKNOWN(0x{status:02X})")
                print(f"   Manual status check: {status_name}, Received: {received}/{expected}")
                if status != FW_STATUS_RECEIVING:
                    raise e
            
            # Step 2: Send firmware chunks
            print("\\n2. Sending firmware chunks...")
            bytes_sent = 0
            chunk_count = 0
            
            while bytes_sent < firmware_size:
                chunk_end = min(bytes_sent + chunk_size, firmware_size)
                chunk = firmware_data[bytes_sent:chunk_end]
                
                print(f"  Sending chunk {chunk_count + 1}: {len(chunk)} bytes")
                await self.send_firmware_chunk(chunk)
                bytes_sent = chunk_end
                chunk_count += 1
                
                if chunk_count % 10 == 0 or bytes_sent >= firmware_size:
                    print(f"  Sent {bytes_sent}/{firmware_size} bytes ({bytes_sent*100//firmware_size}%)")
                
                # Small delay to avoid overwhelming the device
                await asyncio.sleep(0.01)
            
            print(f"\\n   All chunks sent. Checking device status...")
            # Check status manually after sending all data
            status, received, expected = await self.read_status()
            status_name = STATUS_NAMES.get(status, f"UNKNOWN(0x{status:02X})")
            print(f"   Device status: {status_name}, Received: {received}/{expected}")
            
            # Wait for reception complete
            await self.wait_for_status(FW_STATUS_RECEIVED)
            print("\\n3. Firmware reception complete")
            
            # Step 3: Verify firmware
            print("\\n4. Verifying firmware...")
            crc_data = struct.pack('<I', crc32)
            print(f"   Sending VERIFY command with CRC32: 0x{crc32:08X}")
            print(f"   CRC data: {crc_data.hex()}")
            await self.send_command(FW_CMD_VERIFY, crc_data)
            
            try:
                status = await self.wait_for_status(timeout=10.0)
            except Exception as e:
                print(f"   No notification received, checking status manually...")
                status, received, expected = await self.read_status()
                status_name = STATUS_NAMES.get(status, f"UNKNOWN(0x{status:02X})")
                print(f"   Manual status check: {status_name}, Received: {received}/{expected}")
                if status not in [FW_STATUS_VERIFIED, FW_STATUS_VERIFYING]:
                    raise e
            
            if status == FW_STATUS_VERIFIED:
                print("   Firmware verification successful!")
            elif status == FW_STATUS_VERIFYING:
                print("   Verification in progress, waiting...")
                status = await self.wait_for_status(timeout=10.0)
                if status == FW_STATUS_VERIFIED:
                    print("   Firmware verification successful!")
                else:
                    raise Exception("Firmware verification failed")
            else:
                raise Exception("Firmware verification failed")
            
            # Step 4: Flash firmware
            print("\\n5. Flashing firmware...")
            await self.send_command(FW_CMD_FLASH)
            
            # Wait for FLASHING status first
            try:
                status = await self.wait_for_status(timeout=10.0)
                if status == FW_STATUS_FLASHING:
                    print("   Firmware flashing in progress...")
                    # Wait for completion
                    await self.wait_for_status(FW_STATUS_COMPLETE, timeout=60.0)
                elif status == FW_STATUS_COMPLETE:
                    print("   Firmware flashing completed!")
                else:
                    raise Exception(f"Unexpected status during flashing: {STATUS_NAMES.get(status, f'0x{status:02X}')}")
            except Exception as e:
                print(f"   No notification received, checking status manually...")
                status, received, expected = await self.read_status()
                status_name = STATUS_NAMES.get(status, f"UNKNOWN(0x{status:02X})")
                print(f"   Manual status check: {status_name}, Received: {received}/{expected}")
                if status == FW_STATUS_COMPLETE:
                    print("   Firmware flashing completed!")
                elif status == FW_STATUS_FLASHING:
                    print("   Firmware flashing in progress...")
                    await self.wait_for_status(FW_STATUS_COMPLETE, timeout=60.0)
                else:
                    raise e
            
            print("\\n🎉 Firmware update completed successfully!")
            
        except Exception as e:
            print(f"\\n❌ Firmware update failed: {e}")
            print("Aborting update...")
            await self.send_command(FW_CMD_ABORT)
            raise

async def main():
    parser = argparse.ArgumentParser(description="Firmware Update Client")
    parser.add_argument("firmware", help="Path to firmware file")
    parser.add_argument("--device", default="AlexBlue", help="Device name to connect to")
    parser.add_argument("--chunk-size", type=int, default=240, help="Chunk size for transfer")
    
    args = parser.parse_args()
    
    updater = FirmwareUpdater(args.device)
    
    try:
        await updater.connect()
        
        # Show initial status
        status, received, expected = await updater.read_status()
        status_name = STATUS_NAMES.get(status, f"UNKNOWN(0x{status:02X})")
        print(f"Initial status: {status_name}")
        
        # Update firmware
        await updater.update_firmware(args.firmware, args.chunk_size)
        
    except KeyboardInterrupt:
        print("\\nInterrupted by user")
    except Exception as e:
        print(f"Error: {e}")
    finally:
        await updater.disconnect()

if __name__ == "__main__":
    asyncio.run(main())
