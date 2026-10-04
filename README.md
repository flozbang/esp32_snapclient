# ESP32 Snapcast Client

A Snapcast audio client for the ESP32 based on ESP-IDF and ESP-ADF.

The project combines synchronized Snapcast audio playback with integrated DSP functionality and remote configuration.

## Features

- Snapcast audio client
- FLAC decoding using micro-flac
- 10-band equalizer
- Active crossover for subwoofer or bi-amping applications
- Web-based configuration interface
- JSON-RPC interface
- Persistent device configuration

## 10-Band Equalizer

The integrated 10-band equalizer provides digital frequency response adjustment directly on the ESP32.

EQ settings can be configured through the web interface and stored persistently on the device.

## Active Crossover

The integrated active crossover allows the ESP32 Snapcast Client to be used with different loudspeaker configurations.

Typical applications include:

- Subwoofer integration
- Bi-amping
- Active loudspeaker systems

Crossover processing is performed digitally on the ESP32.

## Web Interface

An integrated web server provides browser-based configuration and device control.

The web interface provides access to device, Snapcast and audio settings without requiring the firmware to be rebuilt or reflashed.

## JSON-RPC Interface

A JSON-RPC interface provides programmatic access to device configuration and status.

This allows external applications, control systems and automation software to communicate directly with the client without using the web interface.

## Hardware

The project currently supports the **AI-Thinker ESP32 Audio Kit** and multiple audio output configurations.

Supported audio hardware includes:

- **ES8388** audio codec
- **Texas Instruments PCM5102** I²S DAC

The AI-Thinker ESP32 Audio Kit with ES8388 is currently used as the primary development and test platform.

Support for additional ESP32 audio hardware and I²S DACs may be added in the future.

## Software

The project is based on:

- ESP-IDF
- ESP-ADF
- FreeRTOS
- Snapcast
- micro-flac
- ESP-DSP

Additional dependencies are managed through the ESP-IDF Component Manager.

## Dependencies

External ESP-IDF components are resolved automatically by the ESP-IDF Component Manager during the build process.

The resolved dependency versions are tracked in:

```text
dependencies.lock
```

The generated `managed_components/` directory is therefore not stored in the repository.

The project configuration used for the tested firmware build is tracked in:

```text
sdkconfig
```

This allows the firmware configuration and dependency versions to be reproduced from the repository.

## Building

A working ESP-IDF / ESP-ADF development environment is required.

Clone the repository:

```bash
git clone https://github.com/flozbang/esp32_snapclient.git
cd esp32_snapclient
```

Build the firmware:

```bash
idf.py build
```

Required managed components are downloaded automatically during dependency resolution.

Flash the ESP32:

```bash
idf.py -p /dev/ttyUSB0 flash
```

Monitor the device:

```bash
idf.py -p /dev/ttyUSB0 monitor
```

Or flash and start the monitor in one step:

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

The serial device may differ depending on the host system.

## Project Status

The project is under active development.

The core audio functionality is operational, including:

- Snapcast playback
- FLAC decoding
- Audio synchronization
- 10-band equalizer
- Active crossover

Development is currently focused on completing and refining the web interface, configuration handling and JSON-RPC interface.

## Repository Structure

```text
.
├── components/          Custom ESP-IDF / ESP-ADF components
├── html/                Web interface
├── main/                Main application
├── CMakeLists.txt
├── dependencies.lock    Resolved ESP-IDF dependencies
├── partitions.csv       Flash partition table
├── sdkconfig            ESP-IDF project configuration
└── README.md
```

## Security

Wi-Fi credentials and other device-specific secrets are not intended to be stored in the source code or committed to the repository.

For production and commercial applications, additional security hardening and product-specific configuration may be required.

## License

See [LICENSE](LICENSE) for licensing information.

Third-party components are subject to their respective licenses.
