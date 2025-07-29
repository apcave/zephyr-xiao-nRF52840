# Copilot Instructions

<!-- Use this file to provide workspace-specific custom instructions to Copilot. For more details, visit https://code.visualstudio.com/docs/copilot/copilot-customization#_use-a-githubcopilotinstructionsmd-file -->

This is a Zephyr RTOS project for the nRF52840 Seeed Xiao BLE board with default MCUboot bootloader configuration.

## Project Context
- Target: nRF52840 Seeed Xiao BLE board
- RTOS: Zephyr 3.7.99
- Bootloader: MCUboot with default RSA signature verification
- Build System: west + CMake
- Console: USB CDC ACM (default)

## Key Components
- Main application: Simple LED blinky with logging
- Flash layout: MCUboot + dual application slots
- Default configurations for USB console and Bluetooth LE
- Standard Zephyr device tree and configuration patterns

## Development Guidelines
- Use Zephyr APIs and coding conventions
- Follow Nordic Semiconductor and Zephyr documentation
- Use device tree overlays for board-specific configurations
- Maintain compatibility with west build system and sysbuild
- Use proper logging levels and structured logging
