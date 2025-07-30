/*
 * Copyright (c) 2016 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/usb/usb_device.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>


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

/* Buffer for data processing */
#define MAX_DATA_SIZE 244  // MTU - overhead
static uint8_t input_data[MAX_DATA_SIZE];
static uint8_t output_data[MAX_DATA_SIZE];
static uint16_t data_length = 0;

/* Forward declaration of the service attribute */
static const struct bt_gatt_attr *data_output_attr;

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

/* Define the Data Stream GATT Service */
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
	
	/* CCC Descriptor for notifications */
	BT_GATT_CCC(data_output_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

/* Initialize the output attribute pointer after service definition */
static void init_gatt_service(void)
{
	/* The data output characteristic is at index 3 in the service attributes */
	data_output_attr = &data_stream_service.attrs[3];
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
