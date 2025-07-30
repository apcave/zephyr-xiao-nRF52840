/*
 * Copyright (c) 2016 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/reboot.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#ifdef CONFIG_MCUMGR
#include <zephyr/mgmt/mcumgr/transport/smp_bt.h>
// #include <zephyr/mgmt/mcumgr/grp/img_mgmt/img_mgmt.h>  // Disabled - requires bootutil
#include <zephyr/mgmt/mcumgr/grp/os_mgmt/os_mgmt.h>
#endif


LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)

/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static bool led_state = true;
static bool blink_enabled = true;

// <------------------------------ Bluetooth Configuration ----------------------------->
/* Custom Data Stream Service UUID: 12345678-1234-5678-9ABC-DEF012345678 */
#define DATA_STREAM_SERVICE_UUID \
	BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x9ABC, 0xDEF012345678))

/* Data Input Characteristic UUID: 12345678-1234-5678-9ABC-DEF012345679 */
#define DATA_INPUT_CHAR_UUID \
	BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x9ABC, 0xDEF012345679))

/* Data Output Characteristic UUID: 12345678-1234-5678-9ABC-DEF01234567A */
#define DATA_OUTPUT_CHAR_UUID \
	BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x9ABC, 0xDEF01234567A))

/* Firmware Update Characteristic UUID: 12345678-1234-5678-9ABC-DEF01234567B */
#define FIRMWARE_UPDATE_CHAR_UUID \
	BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x9ABC, 0xDEF01234567B))

/* Firmware Status Characteristic UUID: 12345678-1234-5678-9ABC-DEF01234567C */
#define FIRMWARE_STATUS_CHAR_UUID \
	BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x9ABC, 0xDEF01234567C))

/* Firmware Control Characteristic UUID: 12345678-1234-5678-9ABC-DEF01234567D */
#define FIRMWARE_CONTROL_CHAR_UUID \
	BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x9ABC, 0xDEF01234567D))

/* Buffer for data processing */
#define MAX_DATA_SIZE 244  // MTU - overhead
static uint8_t input_data[MAX_DATA_SIZE];
static uint8_t output_data[MAX_DATA_SIZE];
static uint16_t data_length = 0;

/* Firmware update buffers and state */
#define FIRMWARE_CHUNK_SIZE 240  // MTU - overhead for firmware chunks
static uint32_t firmware_size = 0;
static uint32_t firmware_received = 0;
static uint32_t firmware_crc32 = 0;
static bool firmware_update_active = false;

/* Firmware update status */
typedef enum {
	FW_STATUS_IDLE = 0x00,
	FW_STATUS_RECEIVING = 0x01,
	FW_STATUS_RECEIVED = 0x02,
	FW_STATUS_VERIFYING = 0x03,
	FW_STATUS_VERIFIED = 0x04,
	FW_STATUS_FLASHING = 0x05,
	FW_STATUS_COMPLETE = 0x06,
	FW_STATUS_ERROR = 0xFF
} firmware_status_t;

static firmware_status_t firmware_status = FW_STATUS_IDLE;

/* Firmware control commands */
#define FW_CMD_START 0x01
#define FW_CMD_RESET 0x02
#define FW_CMD_VERIFY 0x03
#define FW_CMD_FLASH 0x04
#define FW_CMD_ABORT 0x05
#define FW_CMD_SWAP_AND_REBOOT 0x06

/* Forward declaration of the service attributes */
static const struct bt_gatt_attr *data_output_attr;
static const struct bt_gatt_attr *firmware_status_attr;

/* Reset firmware update state */
static void firmware_reset(void)
{
firmware_size = 0;
firmware_received = 0;
firmware_crc32 = 0;
firmware_update_active = false;
firmware_status = FW_STATUS_IDLE;
LOG_INF("Firmware update state reset");
}

/* Notify firmware status change */
static void notify_firmware_status(struct bt_conn *conn)
{
	uint8_t status_data[8];
	status_data[0] = firmware_status;
	status_data[1] = (firmware_received >> 0) & 0xFF;
	status_data[2] = (firmware_received >> 8) & 0xFF;
	status_data[3] = (firmware_received >> 16) & 0xFF;
	status_data[4] = (firmware_received >> 24) & 0xFF;
	status_data[5] = (firmware_size >> 0) & 0xFF;
	status_data[6] = (firmware_size >> 8) & 0xFF;
	status_data[7] = (firmware_size >> 16) & 0xFF;
	
	int err = bt_gatt_notify(conn, firmware_status_attr, status_data, sizeof(status_data));
	if (err) {
		LOG_ERR("Failed to notify firmware status: %d", err);
	}
}

/* Function to process/alter the data */
static void process_data(const uint8_t *input, uint8_t *output, uint16_t length)
{
	LOG_INF("Processing %d bytes of data", length);
	
	/* Example processing: XOR with 0xAA, reverse bytes, add prefix */
	output[0] = 0xBE;  // Magic prefix byte
	output[1] = 0xEF;  // Magic prefix byte
	
	/* Reverse and XOR the data */
	for (int i = 0; i < length; i++) {
		output[2 + i] = input[length - 1 - i] ^ 0xAA;
	}
	
	LOG_INF("Data processed: input[0]=0x%02X -> output[2]=0x%02X", 
		   input[0], output[2]);
}

/* Data Input write callback */
static ssize_t data_input_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	const uint8_t *data = buf;
	
	LOG_INF("Received %d bytes via Bluetooth", len);
	
	if (offset + len > MAX_DATA_SIZE) {
		LOG_ERR("Data too large: %d bytes", offset + len);
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	
	/* Store input data */
	memcpy(input_data + offset, data, len);
	data_length = offset + len;
	
	/* Process the data */
	process_data(input_data, output_data, data_length);
	
	/* Send processed data back via notification */
	int err = bt_gatt_notify(conn, data_output_attr, 
					 output_data, data_length + 2);  // +2 for prefix
	if (err) {
		LOG_ERR("Failed to send notification: %d", err);
	} else {
		LOG_INF("Sent %d bytes back to client", data_length + 2);
	}
	
	return len;
}

/* Data Output read callback */
static ssize_t data_output_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				void *buf, uint16_t len, uint16_t offset)
{
	LOG_INF("Client reading output data");
	
	if (offset > data_length + 2) {
		return 0;
	}
	
	return bt_gatt_attr_read(conn, attr, buf, len, offset, 
				 output_data, data_length + 2);
}

/* CCC (Client Characteristic Configuration) callback for notifications */
static void data_output_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	bool notif_enabled = (value == BT_GATT_CCC_NOTIFY);
	LOG_INF("Data output notifications %s", notif_enabled ? "enabled" : "disabled");
}

/* Firmware Update write callback - receives firmware chunks */
static ssize_t firmware_update_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
					 const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	const uint8_t *data = buf;
	
	if (!firmware_update_active) {
		LOG_ERR("Firmware update not active");
		return BT_GATT_ERR(BT_ATT_ERR_NOT_SUPPORTED);
	}

	if (firmware_received + len > firmware_size) {
		LOG_ERR("Firmware chunk exceeds expected size");
		firmware_status = FW_STATUS_ERROR;
		notify_firmware_status(conn);
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	/* Write chunk directly to flash */
	const struct flash_area *fa;
	int ret = flash_area_open(FIXED_PARTITION_ID(slot1_partition), &fa);
	if (ret) {
		LOG_ERR("Failed to open flash area: %d", ret);
		firmware_status = FW_STATUS_ERROR;
		notify_firmware_status(conn);
		return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
	}

	/* Write chunk at current offset */
    if (firmware_received + len >= firmware_size && len % 4 != 0) {
        // Write a few bytes to align to 4-byte boundary only if the file is finished.
        uint16_t tmp = 4*((len / 4)+1);
        ret = flash_area_write(fa, firmware_received, data, tmp);
    } else {
	    ret = flash_area_write(fa, firmware_received, data, len);
    }
	flash_area_close(fa);
	if (ret) {
		LOG_ERR("Failed to write chunk to flash: %d", ret);
		firmware_status = FW_STATUS_ERROR;
		notify_firmware_status(conn);
		return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
	}

	firmware_received += len;
	// LOG_INF("Received firmware chunk: %d/%d bytes", firmware_received, firmware_size);

	/* Update status */
	firmware_status = FW_STATUS_RECEIVING;

	/* Check if complete */
	if (firmware_received >= firmware_size) {
		firmware_status = FW_STATUS_RECEIVED;
		LOG_INF("Firmware completely received: %d bytes", firmware_received);
	}

	notify_firmware_status(conn);
	return len;
}

/* Firmware Status read callback */
static ssize_t firmware_status_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
					void *buf, uint16_t len, uint16_t offset)
{
	uint8_t status_data[8];
	status_data[0] = firmware_status;
	status_data[1] = (firmware_received >> 0) & 0xFF;
	status_data[2] = (firmware_received >> 8) & 0xFF;
	status_data[3] = (firmware_received >> 16) & 0xFF;
	status_data[4] = (firmware_received >> 24) & 0xFF;
	status_data[5] = (firmware_size >> 0) & 0xFF;
	status_data[6] = (firmware_size >> 8) & 0xFF;
	status_data[7] = (firmware_size >> 16) & 0xFF;
	
	return bt_gatt_attr_read(conn, attr, buf, len, offset, status_data, sizeof(status_data));
}

/* Firmware Control write callback - handles commands */
static ssize_t firmware_control_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
					  const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	const uint8_t *data = buf;
	
	if (len < 1) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}
	
	uint8_t command = data[0];
	
	switch (command) {
	case FW_CMD_START:
		if (len < 5) {
			LOG_ERR("FW_CMD_START requires 5 bytes (cmd + size)");
			return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
		}

		uint32_t new_firmware_size = (data[1] << 0) | (data[2] << 8) | (data[3] << 16) | (data[4] << 24);
		/* Check partition size */
		const struct flash_area *fa;
		int ret = flash_area_open(FIXED_PARTITION_ID(slot1_partition), &fa);
		if (ret || new_firmware_size > fa->fa_size) {
			LOG_ERR("Firmware size too large for partition: %d", new_firmware_size);
			firmware_status = FW_STATUS_ERROR;
			if (!ret) flash_area_close(fa);
		} else {
			firmware_reset();
			firmware_size = new_firmware_size;  // Set size after reset
			firmware_update_active = true;
			firmware_status = FW_STATUS_RECEIVING;
			LOG_INF("Firmware update started, expecting %d bytes", firmware_size);
			/* Erase partition before writing */
			ret = flash_area_erase(fa, 0, fa->fa_size);
			flash_area_close(fa);
			if (ret) {
				LOG_ERR("Failed to erase partition: %d", ret);
				firmware_status = FW_STATUS_ERROR;
			}
		}
		break;

	case FW_CMD_VERIFY:
		if (firmware_status != FW_STATUS_RECEIVED) {
			LOG_ERR("Cannot verify - firmware not received");
			firmware_status = FW_STATUS_ERROR;
		} else {
			firmware_status = FW_STATUS_VERIFYING;
			/* Read back data from flash and calculate CRC32 */
			const struct flash_area *fa;
			int ret = flash_area_open(FIXED_PARTITION_ID(slot1_partition), &fa);
			if (ret) {
				LOG_ERR("Failed to open flash area for CRC: %d", ret);
				firmware_status = FW_STATUS_ERROR;
				break;
			}
			uint8_t buf[FIRMWARE_CHUNK_SIZE];
			uint32_t crc = 0xFFFFFFFF;
			const uint32_t polynomial = 0xEDB88320;
			uint32_t offset = 0;
			while (offset < firmware_received) {
				uint32_t chunk = (firmware_received - offset) > FIRMWARE_CHUNK_SIZE ? FIRMWARE_CHUNK_SIZE : (firmware_received - offset);
				ret = flash_area_read(fa, offset, buf, chunk);
				if (ret) {
					LOG_ERR("Failed to read flash for CRC: %d", ret);
					flash_area_close(fa);
					firmware_status = FW_STATUS_ERROR;
					break;
				}
				for (uint32_t i = 0; i < chunk; i++) {
					crc ^= buf[i];
					for (int j = 0; j < 8; j++) {
						if (crc & 1) {
							crc = (crc >> 1) ^ polynomial;
						} else {
							crc >>= 1;
						}
					}
				}
				offset += chunk;
			}
			flash_area_close(fa);
			uint32_t calculated_crc = ~crc;
			if (len >= 5) {
				uint32_t expected_crc = (data[1] << 0) | (data[2] << 8) | (data[3] << 16) | (data[4] << 24);
				if (calculated_crc == expected_crc) {
					firmware_status = FW_STATUS_VERIFIED;
					LOG_INF("Firmware verified successfully (CRC32: 0x%08X)", calculated_crc);
				} else {
					firmware_status = FW_STATUS_ERROR;
					LOG_ERR("Firmware verification failed. Expected: 0x%08X, Got: 0x%08X", expected_crc, calculated_crc);
				}
			} else {
				firmware_status = FW_STATUS_VERIFIED;
				LOG_INF("Firmware verified (CRC32: 0x%08X)", calculated_crc);
			}
		}
		break;
		
	case FW_CMD_FLASH:
		if (firmware_status != FW_STATUS_VERIFIED) {
			LOG_ERR("Cannot flash - firmware not verified");
			firmware_status = FW_STATUS_ERROR;
		} else {
			firmware_status = FW_STATUS_FLASHING;
			notify_firmware_status(conn);
			LOG_INF("Firmware already written to secondary partition during transfer.");
			firmware_status = FW_STATUS_COMPLETE;
			LOG_INF("Firmware update marked complete. Ready to swap and reboot.");
		}
		break;
		
	case FW_CMD_RESET:
		firmware_reset();
		LOG_INF("Firmware update reset");
		break;
		
	case FW_CMD_ABORT:
		firmware_reset();
		LOG_INF("Firmware update aborted");
		break;
		
	case FW_CMD_SWAP_AND_REBOOT:
		if (firmware_status != FW_STATUS_COMPLETE) {
			LOG_ERR("Cannot swap - firmware update not complete");
			firmware_status = FW_STATUS_ERROR;
		} else {
			LOG_INF("Marking new firmware for boot and rebooting...");
			
			/* Mark the secondary slot as ready for swap */
			const struct flash_area *fa;
			int ret = flash_area_open(FIXED_PARTITION_ID(slot1_partition), &fa);
			if (ret == 0) {
				/* For MCUboot, we would typically write image header and trailer
				 * For this demo, we'll just trigger a reboot */
				flash_area_close(fa);
				LOG_INF("System rebooting to apply new firmware...");
				
				/* Give some time for the log message to be sent */
				k_msleep(500);
				
				/* Trigger system reboot */
				sys_reboot(SYS_REBOOT_COLD);
			} else {
				LOG_ERR("Failed to access flash area for swap: %d", ret);
				firmware_status = FW_STATUS_ERROR;
			}
		}
		break;
		
	default:
		LOG_ERR("Unknown firmware command: 0x%02X", command);
		return BT_GATT_ERR(BT_ATT_ERR_NOT_SUPPORTED);
	}
	
	notify_firmware_status(conn);
	return len;
}

/* Firmware Status CCC callback */
static void firmware_status_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	bool notif_enabled = (value == BT_GATT_CCC_NOTIFY);
	LOG_INF("Firmware status notifications %s", notif_enabled ? "enabled" : "disabled");
}

/* Define the Data Stream GATT Service with Firmware Update */
BT_GATT_SERVICE_DEFINE(data_stream_service,
	/* Service Declaration */
	BT_GATT_PRIMARY_SERVICE(DATA_STREAM_SERVICE_UUID),
	
	/* Data Input Characteristic - Write Only */
	BT_GATT_CHARACTERISTIC(DATA_INPUT_CHAR_UUID,
				   BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
				   BT_GATT_PERM_WRITE,
				   NULL, data_input_write, NULL),
	
	/* Data Output Characteristic - Read + Notify */
	BT_GATT_CHARACTERISTIC(DATA_OUTPUT_CHAR_UUID,
				   BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
				   BT_GATT_PERM_READ,
				   data_output_read, NULL, NULL),
	
	/* CCC Descriptor for data output notifications */
	BT_GATT_CCC(data_output_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	
	/* Firmware Update Characteristic - Write Only (for firmware chunks) */
	BT_GATT_CHARACTERISTIC(FIRMWARE_UPDATE_CHAR_UUID,
				   BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
				   BT_GATT_PERM_WRITE,
				   NULL, firmware_update_write, NULL),
	
	/* Firmware Status Characteristic - Read + Notify */
	BT_GATT_CHARACTERISTIC(FIRMWARE_STATUS_CHAR_UUID,
				   BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
				   BT_GATT_PERM_READ,
				   firmware_status_read, NULL, NULL),
	
	/* CCC Descriptor for firmware status notifications */
	BT_GATT_CCC(firmware_status_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	
	/* Firmware Control Characteristic - Write Only (for commands) */
	BT_GATT_CHARACTERISTIC(FIRMWARE_CONTROL_CHAR_UUID,
				   BT_GATT_CHRC_WRITE,
				   BT_GATT_PERM_WRITE,
				   NULL, firmware_control_write, NULL),
);

/* Initialize the output attribute pointers after service definition */
static void init_gatt_service(void)
{
	/* The data output characteristic is at index 3 in the service attributes */
	data_output_attr = &data_stream_service.attrs[3];
	/* The firmware status characteristic is at index 7 in the service attributes */
	firmware_status_attr = &data_stream_service.attrs[7];
}

/* Bluetooth advertising data */
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

/* Bluetooth scan response data with our custom service */
static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_GATT_VAL)),
	/* Advertise our custom Data Stream Service */
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, 
			  0x78, 0x56, 0x34, 0x12, 0xF0, 0xDE, 0xBC, 0x9A,
			  0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12),
};

/* Bluetooth ready callback */
static void bt_ready_cb(int err)
{
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return;
	}

	LOG_INF("Bluetooth initialized");
	printk("Bluetooth initialized successfully\n");

	/* Initialize GATT service */
	init_gatt_service();

#ifdef CONFIG_MCUMGR
	/* Initialize MCUmgr Bluetooth transport for OTA updates */
	smp_bt_register();
	LOG_INF("MCUmgr Bluetooth transport initialized");
	printk("OTA updates enabled via Bluetooth\n");
#endif

	/* Start advertising with default connectable parameters */
	err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err) {
		LOG_ERR("Advertising failed to start (err %d)", err);
		printk("ERROR: Failed to start advertising! err = %d\n", err);
		return;
	}

	LOG_INF("Advertising successfully started");
	printk("Bluetooth advertising started as '%s'\n", CONFIG_BT_DEVICE_NAME);
}


/* Shell command to control LED */
static int cmd_led_on(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int ret = gpio_pin_set_dt(&led, 1);
	if (ret < 0) {
		shell_error(sh, "Failed to turn LED on: %d", ret);
		return ret;
	}
	
	led_state = true;
	shell_print(sh, "LED turned ON");
	return 0;
}

/* Shell command to control LED */
static int cmd_led_off(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int ret = gpio_pin_set_dt(&led, 0);
	if (ret < 0) {
		shell_error(sh, "Failed to turn LED off: %d", ret);
		return ret;
	}
	
	led_state = false;
	shell_print(sh, "LED turned OFF");
	return 0;
}

/* Shell command to toggle blinking */
static int cmd_blink_toggle(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	blink_enabled = !blink_enabled;
	shell_print(sh, "LED blinking %s", blink_enabled ? "ENABLED" : "DISABLED");
	return 0;
}

/* Shell command to show status */
static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "=== System Status ===");
	shell_print(sh, "Board: %s", CONFIG_BOARD);
	shell_print(sh, "LED State: %s", led_state ? "ON" : "OFF");
	shell_print(sh, "Blinking: %s", blink_enabled ? "ENABLED" : "DISABLED");
	shell_print(sh, "Uptime: %llu ms", k_uptime_get());
	return 0;
}

/* Shell command to test data processing */
static int cmd_test_data(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	/* Test data processing locally */
	uint8_t test_input[] = {0x01, 0x02, 0x03, 0x04, 0x05};
	uint8_t test_output[MAX_DATA_SIZE];
	
	shell_print(sh, "=== Data Processing Test ===");
	shell_print(sh, "Input:  %02X %02X %02X %02X %02X", 
		   test_input[0], test_input[1], test_input[2], test_input[3], test_input[4]);
	
	process_data(test_input, test_output, 5);
	
	shell_print(sh, "Output: %02X %02X %02X %02X %02X %02X %02X", 
		   test_output[0], test_output[1], test_output[2], 
		   test_output[3], test_output[4], test_output[5], test_output[6]);
	shell_print(sh, "Processing: Prefix(BEEF) + Reverse + XOR(0xAA)");
	return 0;
}

/* Shell command to show firmware update status */
static int cmd_firmware_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "=== Firmware Update Status ===");
	shell_print(sh, "Status: %s (0x%02X)", 
		   firmware_status == FW_STATUS_IDLE ? "IDLE" :
		   firmware_status == FW_STATUS_RECEIVING ? "RECEIVING" :
		   firmware_status == FW_STATUS_RECEIVED ? "RECEIVED" :
		   firmware_status == FW_STATUS_VERIFYING ? "VERIFYING" :
		   firmware_status == FW_STATUS_VERIFIED ? "VERIFIED" :
		   firmware_status == FW_STATUS_FLASHING ? "FLASHING" :
		   firmware_status == FW_STATUS_COMPLETE ? "COMPLETE" :
		   firmware_status == FW_STATUS_ERROR ? "ERROR" : "UNKNOWN",
		   firmware_status);
	shell_print(sh, "Expected Size: %d bytes", firmware_size);
	shell_print(sh, "Received: %d bytes", firmware_received);
	shell_print(sh, "Active: %s", firmware_update_active ? "YES" : "NO");

	// Show CRC only if firmware is verified or complete
	if (firmware_status == FW_STATUS_VERIFIED || firmware_status == FW_STATUS_COMPLETE) {
		// Calculate CRC from flash
		const struct flash_area *fa;
		int ret = flash_area_open(FIXED_PARTITION_ID(slot1_partition), &fa);
		if (!ret) {
			uint8_t buf[FIRMWARE_CHUNK_SIZE];
			uint32_t crc = 0xFFFFFFFF;
			const uint32_t polynomial = 0xEDB88320;
			uint32_t offset = 0;
			while (offset < firmware_received) {
				uint32_t chunk = (firmware_received - offset) > FIRMWARE_CHUNK_SIZE ? FIRMWARE_CHUNK_SIZE : (firmware_received - offset);
				ret = flash_area_read(fa, offset, buf, chunk);
				if (ret) {
					shell_print(sh, "CRC read error at offset %u", offset);
					break;
				}
				for (uint32_t i = 0; i < chunk; i++) {
					crc ^= buf[i];
					for (int j = 0; j < 8; j++) {
						if (crc & 1) {
							crc = (crc >> 1) ^ polynomial;
						} else {
							crc >>= 1;
						}
					}
				}
				offset += chunk;
			}
			flash_area_close(fa);
			shell_print(sh, "CRC32: 0x%08X", ~crc);
		} else {
			shell_print(sh, "Unable to open flash for CRC");
		}
	}

	shell_print(sh, "");
	shell_print(sh, "Firmware Update Service UUIDs:");
	shell_print(sh, "  Update (chunks): 12345678-1234-5678-9ABC-DEF01234567B");
	shell_print(sh, "  Status:          12345678-1234-5678-9ABC-DEF01234567C");
	shell_print(sh, "  Control:         12345678-1234-5678-9ABC-DEF01234567D");

	return 0;
}

/* Shell command to reset firmware update */
static int cmd_firmware_reset(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	firmware_reset();
	shell_print(sh, "Firmware update state reset");
	return 0;
}

#ifdef CONFIG_MCUMGR
/* Shell command to show firmware version and OTA status */
static int cmd_ota_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "=== OTA Status ===");
	shell_print(sh, "MCUmgr: Enabled");
	shell_print(sh, "Transport: Bluetooth LE");
	shell_print(sh, "Device Name: %s", CONFIG_BT_DEVICE_NAME);
	shell_print(sh, "");
	shell_print(sh, "To update firmware:");
	shell_print(sh, "1. Build new firmware: west build");
	shell_print(sh, "2. Sign image: west sign");
	shell_print(sh, "3. Upload via mcumgr:");
	shell_print(sh, "   mcumgr --conntype ble --connstring peer_name=%s image upload build/zephyr/zephyr.signed.bin", CONFIG_BT_DEVICE_NAME);
	shell_print(sh, "   mcumgr --conntype ble --connstring peer_name=%s image test <hash>", CONFIG_BT_DEVICE_NAME);
	shell_print(sh, "   mcumgr --conntype ble --connstring peer_name=%s reset", CONFIG_BT_DEVICE_NAME);
	return 0;
}
#endif

/* Shell command to show MCUmgr configuration status */
static int cmd_mcumgr_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "=== MCUmgr Configuration Status ===");
#ifdef CONFIG_MCUMGR
	shell_print(sh, "MCUmgr: ENABLED");
	shell_print(sh, "Transport: Bluetooth LE");
	shell_print(sh, "Device Name: %s", CONFIG_BT_DEVICE_NAME);
#else
	shell_print(sh, "MCUmgr: DISABLED");
	shell_print(sh, "Reason: CONFIG_MCUMGR not defined");
	shell_print(sh, "Check prj.conf configuration");
#endif
	return 0;
}

/* Register shell commands */
SHELL_STATIC_SUBCMD_SET_CREATE(led_cmds,
	SHELL_CMD(on, NULL, "Turn LED on", cmd_led_on),
	SHELL_CMD(off, NULL, "Turn LED off", cmd_led_off),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(led, &led_cmds, "LED control commands", NULL);
SHELL_CMD_REGISTER(blink, NULL, "Toggle LED blinking", cmd_blink_toggle);
SHELL_CMD_REGISTER(status, NULL, "Show system status", cmd_status);
SHELL_CMD_REGISTER(test_data, NULL, "Test data processing algorithm", cmd_test_data);
SHELL_CMD_REGISTER(mcumgr_status, NULL, "Show MCUmgr configuration status", cmd_mcumgr_status);
SHELL_CMD_REGISTER(firmware_status, NULL, "Show firmware update status", cmd_firmware_status);
SHELL_CMD_REGISTER(firmware_reset, NULL, "Reset firmware update state", cmd_firmware_reset);
#ifdef CONFIG_MCUMGR
SHELL_CMD_REGISTER(ota_status, NULL, "Show OTA update status and commands", cmd_ota_status);
#endif

int main(void)
{
	LOG_INF("Starting Zephyr Dual Console Blinky on %s", CONFIG_BOARD);

	if (!gpio_is_ready_dt(&led)) {
		LOG_ERR("Error: LED device %s is not ready", led.port->name);
		return -1;
	}

	int ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		LOG_ERR("Error %d: failed to configure LED pin", ret);
		return -1;
	}

	LOG_INF("LED configured successfully");

#ifdef CONFIG_MCUMGR
	/* Initialize MCUmgr subsystem */
	// img_mgmt_register_group();  // Disabled - requires bootutil
	os_mgmt_register_group();
	LOG_INF("MCUmgr OS management group registered");
#endif

	/* Initialize Bluetooth */
	ret = bt_enable(bt_ready_cb);
	if (ret) {
		LOG_ERR("Bluetooth init failed (err %d)", ret);
		printk("ERROR: Bluetooth initialization failed! ret = %d\n", ret);
	} else {
		printk("Bluetooth initialization started...\n");
	}

	LOG_INF("Console available on:");
	LOG_INF("  - UART: pins D6(TX)/D7(RX) at 115200 baud");
	LOG_INF("  - USB: CDC ACM device");
	LOG_INF("Shell commands: led on/off, blink, status");

	while (1) {
		if (blink_enabled) {
			ret = gpio_pin_set_dt(&led, (int)led_state);
			if (ret < 0) {
				LOG_ERR("Error %d: failed to set LED", ret);
				return -1;
			}

			// LOG_INF("LED turned %s", led_state ? "on" : "off");
			led_state = !led_state;
		}
		k_msleep(1000);
	}

	return 0;
}
