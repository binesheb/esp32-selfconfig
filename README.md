# ESP32 SelfConfig

Reusable ESP32 firmware framework for self-provisioning and automatic GitHub OTA updates.

## What it does

- Starts its own `ESP32-SETUP-XXXX` Wi-Fi network on first boot.
- Provides a captive configuration portal at `192.168.4.1`.
- Scans nearby Wi-Fi networks.
- Stores Wi-Fi credentials and device settings in ESP32 NVS.
- Lets the same base firmware point to any public GitHub firmware repository.
- Checks GitHub Releases automatically after boot.
- Supports stable and beta/pre-release channels.
- Downloads `manifest.json` and `firmware.bin` from the selected release.
- Verifies the firmware SHA-256 before installing it.
- Reboots automatically after a successful update.
- Returns to setup mode if the saved Wi-Fi cannot be reached.
- Supports a physical setup/reset button.
- Separates framework code from the application through `AppHooks.h`.

## First boot

1. Flash the firmware over USB.
2. Power the ESP32.
3. Connect your phone or laptop to `ESP32-SETUP-XXXX`.
4. Open `http://192.168.4.1` if the captive portal does not appear automatically.
5. Select the Wi-Fi network and enter its password.
6. Enter the GitHub owner and repository containing the firmware releases.
7. Select Stable or Beta.
8. Save and reboot.

After reboot, the ESP32 connects to the configured Wi-Fi and performs an OTA check.

## GitHub release format

Every firmware release should contain:

- `firmware.bin`
- `manifest.json`

The included `release.yml` workflow creates these automatically when a tag such as `v1.0.0` is pushed.

The generated manifest contains the firmware version, release channel, firmware URL, file size, and SHA-256 checksum.

## Button behaviour

Default GPIO: **GPIO 0**.

- Hold for 5 seconds: enter Wi-Fi/setup portal.
- Hold for 10 seconds: erase saved configuration and reboot.

## Project structure

```text
esp32-selfconfig/
├── .github/workflows/
│   ├── build.yml
│   └── release.yml
├── include/
│   └── AppHooks.h
├── src/
│   └── main.cpp
└── platformio.ini
```

## Reusing the framework

Application-specific code belongs in `include/AppHooks.h`.

`applicationSetup()` runs after Wi-Fi provisioning and the boot OTA check.

`applicationLoop()` runs continuously during normal operation.

This allows the same SelfConfig base to be reused for CLIMORA, a bus controller, sensor devices, displays, relays, and other ESP32 products.

## Build

```bash
pio run
pio run --target upload
pio device monitor
```

## OTA security

The firmware validates the downloaded image using SHA-256 before calling `Update.end()`.

The initial reusable implementation uses HTTPS with certificate verification disabled for GitHub requests. This avoids embedding a CA bundle into every device. For production deployments, CA verification or certificate pinning should be added.

Private GitHub repositories are intentionally not supported by the base firmware because storing a GitHub access token on a device is not a safe default architecture.

## Versioning

Use semantic release tags such as:

```text
v1.0.0
v1.1.0
v2.0.0
```

Beta releases can use tags such as:

```text
v1.2.0-beta.1
```

The release workflow is triggered by tags beginning with `v`.
