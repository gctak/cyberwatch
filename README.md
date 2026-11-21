# CyberWatch

A DIY smartwatch project powered by the ESP32-C6, featuring a custom AMOLED interface, Wi-Fi connectivity, and real-time clock synchronization.

> **Status:** Work in progress.

## Hardware

This project is being developed for the **Waveshare ESP32-C6-Touch-AMOLED-2.06**.

- **Microcontroller:** ESP32-C6
- **Display:** 2.06-inch AMOLED touchscreen
- **Development environment:** Arduino IDE
- **Programming language:** C++

**Official documentation:** [Waveshare ESP32-C6-Touch-AMOLED-2.06](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-2.06)

## Dependencies

The project uses the following libraries:

| Library          | Version |
| ---------------- | ------- |
| Arduino_DriveBus | 1.0.1   |
| Arduino_GFX      | 1.6.0   |
| SensorLib        | 0.3.1   |

These versions correspond to the libraries currently used during development.

For hardware setup, library installation, and configuration instructions, refer to the [official Waveshare documentation](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-2.06).

## Setup

1. Install the required libraries and configure the ESP32-C6 board in Arduino IDE.
2. Copy `secrets.example.h` to `secrets.h`.
3. Open `secrets.h` and fill in your Wi-Fi credentials.
4. Compile and upload the firmware to the ESP32-C6.

> [!WARNING]
> `secrets.h` is ignored by Git and should never be committed to the repository.

## Display Configuration

The interface is specifically designed for the **ESP32-C6-Touch-AMOLED-2.06** display.

UI elements are manually positioned using `setCursor(x, y)` from the Arduino_GFX library.

**Important:** Cursor coordinates, text sizes, and spacing are tailored to the display's resolution and orientation. Using a different display may require layout adjustments.

## Current Features

- Digital clock interface
- Date and weekday display
- Wi-Fi connectivity
- NTP time synchronization
- Custom AMOLED interface
- Double-tap to wake the display
- Automatic screen timeout

## Development

CyberWatch is developed using the Arduino framework and C++.

The project is currently under active development, with a focus on building a lightweight, customizable smartwatch interface.
