import asyncio
from bleak import BleakScanner, BleakClient
import time

# Custom Data Stream Service UUIDs (from our device)
DATA_STREAM_SERVICE_UUID = "12345678-1234-5678-9abc-def012345678"
DATA_INPUT_CHAR_UUID = "12345678-1234-5678-9abc-def012345679"      # Write to this
DATA_OUTPUT_CHAR_UUID = "12345678-1234-5678-9abc-def01234567a"     # Read/Notify from this

async def main():
    print("🔍 Scanning for AlexBlue...")
    devices = await BleakScanner.discover()
    target = None
    for d in devices:
        print(f"Found device: {d.name} - {d.address}")
        if d.name and "AlexBlue" in d.name:
            target = d
            break
    
    if not target:
        print("❌ AlexBlue device not found.")
        return

    async with BleakClient(target.address) as client:
        print(f"✅ Connected to {target.name}!")
        
        # Discover all services and characteristics
        print("\n📋 Discovering services and characteristics...")
        for service in client.services:
            print(f"Service: {service.uuid}")
            for char in service.characteristics:
                print(f"  Characteristic: {char.uuid} (Properties: {char.properties})")
        
        # Setup notification handler for data output
        received_data = []
        def handle_data_output(_, data):
            print(f"📤 Received processed data: {data.hex()} ({len(data)} bytes)")
            print(f"   As bytes: {list(data)}")
            received_data.append(data)
        
        # Enable notifications on data output characteristic
        try:
            await client.start_notify(DATA_OUTPUT_CHAR_UUID, handle_data_output)
            print("🔔 Notifications enabled for data output")
        except Exception as e:
            print(f"❌ Failed to enable notifications: {e}")
            return
        
        # Test data streaming with different payloads
        test_payloads = [
            bytes([0x01, 0x02, 0x03, 0x04, 0x05]),
            b"Hello",
            bytes(range(10)),  # 0x00 to 0x09
            b"BLE_DATA_STREAM_TEST",
        ]
        
        for i, payload in enumerate(test_payloads, 1):
            print(f"\n🚀 Test {i}: Sending {len(payload)} bytes: {payload.hex()}")
            print(f"   As text: {payload}")
            
            try:
                # Write data to input characteristic
                await client.write_gatt_char(DATA_INPUT_CHAR_UUID, payload)
                print("✅ Data sent successfully")
                
                # Wait a bit for processing and notification
                await asyncio.sleep(0.5)
                
            except Exception as e:
                print(f"❌ Failed to send data: {e}")
        
        # Read the latest processed data directly
        print(f"\n📖 Reading data output characteristic directly...")
        try:
            output_data = await client.read_gatt_char(DATA_OUTPUT_CHAR_UUID)
            print(f"📤 Read data: {output_data.hex()} ({len(output_data)} bytes)")
            print(f"   As bytes: {list(output_data)}")
        except Exception as e:
            print(f"❌ Failed to read output: {e}")
        
        # Interactive mode
        print(f"\n🎮 Interactive mode - Enter hex data to send (or 'quit' to exit):")
        print("Example: 01020304 or Hello")
        
        while True:
            user_input = input("Data to send: ").strip()
            if user_input.lower() in ['quit', 'exit', 'q']:
                break
            
            try:
                # Try to parse as hex first
                if all(c in '0123456789abcdefABCDEF' for c in user_input.replace(' ', '')):
                    # Remove spaces and convert hex to bytes
                    hex_str = user_input.replace(' ', '')
                    if len(hex_str) % 2 == 0:
                        data = bytes.fromhex(hex_str)
                    else:
                        raise ValueError("Odd number of hex digits")
                else:
                    # Treat as ASCII text
                    data = user_input.encode('utf-8')
                
                print(f"📤 Sending: {data.hex()} ({len(data)} bytes)")
                await client.write_gatt_char(DATA_INPUT_CHAR_UUID, data)
                await asyncio.sleep(0.2)  # Wait for response
                
            except Exception as e:
                print(f"❌ Error: {e}")
        
        # Cleanup
        try:
            await client.stop_notify(DATA_OUTPUT_CHAR_UUID)
            print("🔕 Notifications disabled")
        except:
            pass
        
        print("👋 Disconnecting...")

if __name__ == "__main__":
    asyncio.run(main())