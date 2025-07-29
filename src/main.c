/*
 * Copyright (c) 2016 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)

/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

int main(void)
{
	bool led_state = true;

	LOG_INF("Starting Zephyr Blinky application on %s", CONFIG_BOARD);

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

	while (1) {
		ret = gpio_pin_set_dt(&led, (int)led_state);
		if (ret < 0) {
			LOG_ERR("Error %d: failed to set LED", ret);
			return -1;
		}

		LOG_INF("LED turned %s", led_state ? "on" : "off");
		led_state = !led_state;
		k_msleep(1000);
	}

	return 0;
}
