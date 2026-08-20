# Updating ESP32 SelfConfig

## Automatic updates

After the first USB installation, a configured device can check GitHub Releases for a newer firmware version and install it over HTTPS.

Automatic updates are controlled from the setup portal. A device should only update from the selected Stable or Beta channel.

Before accepting an image, the device verifies the SHA-256 value published in the release manifest. If verification or installation fails, the device must keep its current bootable firmware and should be recovered through the manual path below.

## Manual update and recovery

1. Clone or update this repository:

   `git pull --ff-only origin main`

2. Open `main.ino` in Arduino IDE.
3. Confirm that the same OTA-capable `partitions.csv` remains beside the sketch.
4. Select the correct ESP32 board and serial port.
5. Compile and upload over USB.
6. Reset the device and confirm that the setup portal or application starts normally.

USB flashing is also the recovery path if an OTA update cannot complete.

## Creating a firmware release

Use semantic version tags:

- Patch: `v1.0.1` for compatible fixes.
- Minor: `v1.1.0` for backward-compatible features.
- Major: `v2.0.0` for incompatible framework changes.
- Pre-release: `v1.1.0-beta.1` for Beta-channel testing.

Pushing a matching `v*` tag triggers the release workflow, which builds `firmware.bin`, calculates SHA-256, creates `manifest.json`, and publishes a GitHub Release.

## Safety notes

Do not manually replace a release manifest checksum. Build the firmware and manifest together through the release workflow so the checksum, size, version, and download URL describe the same binary.

Before rolling out a new framework release to many devices, test the exact release tag on at least one representative device and verify both normal boot and recovery by USB.
