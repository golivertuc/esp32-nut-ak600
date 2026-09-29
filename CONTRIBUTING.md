# Contributing

Thanks for contributing to **esp32-nut-ak600**!

This repository is a fork of [`maverick1982/esp32-nut`](https://github.com/maverick1982/esp32-nut) (v1.6.1) with added support for **TBCin AK-600** and similar UPS models (Megatec Q1 protocol) via the `KraulerDriver`.

## 📚 Sources & Attribution

To keep the provenance clear and avoid attribution conflicts, the code in this repository comes from the following upstream sources:

| Source | Version | What it provides | License |
|--------|---------|------------------|---------|
| [maverick1982/esp32-nut](https://github.com/maverick1982/esp32-nut) | v1.6.1 | Base firmware: USB host bridge, NUT server, Web UI, `DriverRegistry`, `GenericDriver`, quirks system, build system (PlatformIO) | MIT |
| [Network UPS Tools (NUT) — `blazer_usb` driver](https://github.com/networkupstools/nut/blob/master/blazer.c) | master (reference) | Reference implementation of the Krauler USB protocol used to design `KraulerDriver` (HID string descriptors `Q1`/`Info`/`Rating`) | GPL-2.0 (reference only, not copied) |

### Files added/modified in this fork

- `lib/USBHostUPS/src/KraulerDriver.h` — **new** driver for TBCin AK-600 / Megatec Q1 UPS chips
- `lib/USBHostUPS/src/DriverRegistry.cpp` — registration of the new driver
- `docs/ak600-tbcin-integration.md` — integration guide and reverse-engineering notes
- `docs/adr/adr-0009-*.md` — Architecture Decision Record for the new driver
- `firmware/esp32-s3-standard/` — prebuilt binaries (see [FLASHING.md](docs/FLASHING.md))
- `README.md` — TBCin AK-600 added to the supported models list

The upstream MIT license (Pasquale Cuorpo, 2026) applies to the base code and remains in force. See [LICENSE](LICENSE).

## 📀 Flashing the prebuilt firmware

A prebuilt firmware for **ESP32-S3 (8 MB, DIO mode)** is included in [`firmware/esp32-s3-standard/`](firmware/esp32-s3-standard/).

**Requirements:**
- Linux (or WSL on Windows) with `python3` and a `dialout` group (or use `sudo`)
- USB-C cable connected to the ESP32-S3's **COM/UART** port

**Flash command:**

```bash
cd firmware/esp32-s3-standard
sg dialout -c "python3 -m esptool --chip esp32s3 --port /dev/ttyACM0 --baud 460800 \
  write-flash 0x0 bootloader.bin 0x8000 partitions.bin 0x10000 firmware.bin"
```

> **Download mode (ESP32-S3):** hold the **BOOT** button, tap **RESET**, then release **BOOT**. The board will enumerate as `/dev/ttyACM0` (CH340) or `/dev/ttyUSB*`.

Full step-by-step instructions (including Wi-Fi setup after first boot) are in [firmware/FLASHING.md](firmware/FLASHING.md).

## 🛠 Building from source

1. Install [PlatformIO](https://platformio.org/) (VS Code extension or CLI).
2. `pio run -e esp32-s3-standard`
3. `pio run -e esp32-s3-standard -t upload --upload-port /dev/ttyACM0`

The `native` environment (`pio test -e native`) runs the unit tests on the host — no hardware required.

## 🧩 Adding a new UPS driver

1. Study `lib/USBHostUPS/src/GenericDriver.h` and the protocol the UPS chip exposes (dump HID string descriptors with esptool/`upspd` if unknown).
2. Create `<Vendor>Driver.h` (or `.cpp/.h`) in `lib/USBHostUPS/src/`.
3. Implement `buildPollLists()` / the parse hooks the registry expects, following `KraulerDriver.h` as a minimal example.
4. Register the driver in `DriverRegistry.cpp`.
5. Add unit tests in `test/` (native environment) that parse recorded descriptor payloads.
6. Document the model in `README.md` (supported models) and add a `docs/` integration note with the reverse-engineering steps.

## 📝 Commit & PR guidelines

- Imperative, concise commits: `Add KraulerDriver: TBCin AK-600 (Megatec Q1) support`.
- One logical change per commit; reference the affected driver/area.
- PRs: describe the UPS model, the chip (VID/PID or "generic"), and how you verified (real hardware, mock, or both).
- No secrets, no build artifacts outside `firmware/`.

## 🤝 Reporting issues

Include: UPS model + chip label, `lsusb` output, the exact error log from the ESP32 web console, and the firmware version (`README.md` / `firmware/esp32-s3-standard/SHA256SUMS`).

Thanks for helping! 🎩
