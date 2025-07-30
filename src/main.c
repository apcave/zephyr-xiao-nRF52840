/*
 * Copyright (c) 2016 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/usb/usb_device.h>


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
/* Bluetooth advertising data */
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

/* Bluetooth scan response data (optional additional info) */
static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_GATT_VAL)),
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

/* Register shell commands */
SHELL_STATIC_SUBCMD_SET_CREATE(led_cmds,
	SHELL_CMD(on, NULL, "Turn LED on", cmd_led_on),
	SHELL_CMD(off, NULL, "Turn LED off", cmd_led_off),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(led, &led_cmds, "LED control commands", NULL);
SHELL_CMD_REGISTER(blink, NULL, "Toggle LED blinking", cmd_blink_toggle);
SHELL_CMD_REGISTER(status, NULL, "Show system status", cmd_status);

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
