# Changelog

All notable changes to ESP32 SelfConfig are documented in this file.

The project follows [Semantic Versioning](https://semver.org/). Firmware releases should be tagged as `vMAJOR.MINOR.PATCH`, with prereleases such as `v1.1.0-beta.1` reserved for the Beta channel.

## [Unreleased]

### Changed
- Documented the current `0.1.0` framework baseline so release history and the compiled `APP_VERSION` have an explicit source of record.

## [0.1.0]

### Added
- Wi-Fi self-provisioning with a setup access point and captive DNS flow.
- Persisted configuration using ESP32 NVS.
- Stable and Beta GitHub OTA channels.
- OTA-capable partition layout and USB recovery path.
- Release manifest metadata with firmware version, size, URL, and SHA-256 digest.
- Arduino IDE and PlatformIO build support.

### Notes
- `0.1.0` is the initial reusable framework baseline. No GitHub release should be backfilled unless the corresponding firmware artifact and manifest can be reproduced from the tagged source.
