# ESP32 SelfConfig

Reusable ESP32 firmware framework for self-provisioning and automatic GitHub OTA updates.

## Important: first upload is USB + Arduino IDE

This repository is intentionally structured so the **first firmware upload can be done with Arduino IDE** after cloning the repository with GitHub Desktop.

### PC setup

1. Clone `binesheb/esp32-selfconfig` with GitHub Desktop.
2. Open the cloned folder.
3. Install **ESP32 by Espressif Systems** in Arduino IDE Boards Manager.
4. Install **ArduinoJson 7.4.2** from Arduino IDE Library Manager.
5. Open `main.ino` from the cloned folder.
6. Select your ESP32 board. For a normal ESP32 Dev Module, use `ESP32 Dev Module`.
7. Select the correct COM port.
8. Compile first.
9. Upload over USB.

The repository contains `partitions.csv`. Arduino's build system uses a `partitions.csv` placed with the sketch, so the first USB upload is prepared with the OTA-capable partition layout. ESP32 OTA requires OTA application slots and an OTA data partition. citeturn0search0

> **Important:** Do not delete `partitions.csv`. Future OTA firmware must use the same compatible partition layout.

### Required Arduino library

The only external library used by the framework is:

- ArduinoJson 7.4.2

The file `libraries.txt` records this dependency.

## First boot

After USB upload:

1. Power/reset the ESP32.
2. The ESP32 starts a Wi-Fi access point named `ESP32-SETUP-XXXX`.
3. Connect your phone or PC to that Wi-Fi.
4. Open `http://192.168.4.1` if the captive portal does not open automatically.
5. Select the Wi-Fi network.
6. Enter the Wi-Fi password.
7. Enter the GitHub owner and repository.
8. Select Stable or Beta.
9. Enable or disable automatic update checking.
10. Press **Save & Reboot**.

The settings are stored in ESP32 NVS and survive normal reboots and firmware OTA updates.

## Normal boot

```text
Power on
   |
Load saved configuration
   |
Connect to configured Wi-Fi
   |
Check GitHub release
   |
New firmware?
  / \
Yes  No
 |    |
OTA  Start application
 |
Verify SHA-256
 |
Install OTA
 |
Reboot
```

If the saved Wi-Fi cannot be reached, the ESP32 returns to its setup access point so the Wi-Fi settings can be corrected.

## Re-enter setup mode

GPIO 0 is the default setup button.

- Hold **5 seconds** → start configuration portal.
- Hold **10 seconds** → factory reset saved configuration and reboot.

## GitHub OTA

The configured device points to a **public GitHub repository** containing firmware Releases.

Each release contains:

- `firmware.bin`
- `manifest.json`

The GitHub Actions release workflow creates both automatically when a version tag such as `v1.0.0` is pushed.

The manifest contains:

- firmware version
- release channel
- firmware URL
- firmware size
- SHA-256 checksum

The ESP32 verifies the SHA-256 before accepting the downloaded firmware.

## Stable and Beta

Stable releases use normal tags:

```text
v1.0.0
v1.1.0
v1.2.0
```

Beta releases use pre-release tags:

```text
v1.2.0-beta.1
v1.2.0-beta.2
```

## Reusing the framework

Application-specific code belongs in `AppHooks.h`.

```cpp
inline void applicationSetup() {
  // Initialize your application.
}

inline void applicationLoop() {
  // Run your application.
}
```

This allows the same self-configuration and OTA framework to be reused for CLIMORA, bus controllers, sensors, displays, relays, and other ESP32 projects.

## Project structure

```text
esp32-selfconfig/
├── .github/
│   └── workflows/
│       ├── build.yml
│       └── release.yml
├── AppHooks.h
├── libraries.txt
├── main.ino
├── partitions.csv
├── platformio.ini
└── README.md
```

`main.ino` is the Arduino IDE entry point. `platformio.ini` is provided for PlatformIO/GitHub Actions builds.

## PlatformIO

```bash
pio run
pio run --target upload
pio device monitor
```

PlatformIO is configured to use the repository root as the source directory so it builds the same `main.ino` used by Arduino IDE.

## OTA security

The firmware uses HTTPS for GitHub requests and validates the downloaded image using SHA-256. The current framework deliberately uses `setInsecure()` for TLS certificate validation so GitHub certificate rotation does not require a firmware rebuild. This is acceptable for the initial framework but should be replaced with CA validation before high-security production deployment.

Private GitHub repositories are not supported by default because embedding a GitHub access token in every ESP32 would be a poor security architecture.
