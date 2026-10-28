# Syndetis

### Connect → Diagnose → Move on.

**Syndetis** is a portable, ESP32-based Ethernet diagnostic tool designed for field technicians, network engineers, and industrial maintenance teams.

Built for quick troubleshooting at network cabinets, industrial installations, remote stations, and control panels, Syndetis provides essential Ethernet and IP diagnostics without requiring a laptop.

A compact OLED display and three physical buttons make it straightforward to use: connect to a port, inspect the network, run a test, and move on.

**Firmware:** v0.5.2 | **Platform:** ESP32 / ESP-IDF v6.1 | **Development:** Feature-frozen release candidate

---

## Features

### Ethernet diagnostics
- Ethernet link detection and connection status.
- Negotiated 10/100 Mbps speed and duplex information.
- Automatic LLDP/CDP neighbor discovery.
- Switch identity, port and VLAN information when advertised.
- Passive Ethernet operation until an IPv4 test is explicitly started.

### Network testing
- DHCP discovery and IPv4 configuration testing.
- Display of assigned IP address, subnet, gateway and DNS information.
- Manual IPv4 configuration with ARP address-conflict detection.
- Gateway and custom IPv4 ping.
- Four ICMP requests with reply count, packet loss and response time.
- Cancellation, cleanup and retry without restarting the device.

### User interface
- 128×64 monochrome OLED.
- Three-button navigation: UP, DOWN and OK.
- Simple, task-oriented menus.
- Clear diagnostic results and error messages.
- Automatic invalidation of outdated results after cable disconnection.

### Connectivity and maintenance
- Optional Wi-Fi access point and browser-based status dashboard.
- Ethernet and Wi-Fi firmware updates.
- Dual-slot OTA with image validation and trial-boot rollback.
- USB-UART firmware flashing and recovery.

Syndetis is designed for quick, targeted diagnostics—not continuous monitoring, network scanning or replacing a full network workstation.

## How it works

1. **Connect:** Plug Syndetis into an Ethernet port.
2. **Inspect:** Check physical link status, negotiated speed and discovered neighbors.
3. **Test:** Run DHCP, configure temporary IPv4 addressing or ping a target.
4. **Diagnose:** Use the results to identify likely physical, addressing or connectivity problems.
5. **Move on:** Release the test configuration and continue to the next port.

The interface uses UP/DOWN for navigation, short OK to select, and holding OK for approximately 500 ms to return or cancel.

Ethernet remains passive by default. Network tests requiring IPv4 traffic begin only after explicit technician action.

Syndetis reports observable network conditions rather than making unsupported assumptions. For example, a missing ping reply does not necessarily indicate an offline host.

## Hardware

| Component | Specification |
|---|---|
| Microcontroller | ESP32 WT32-ETH01 |
| Ethernet | LAN8720 RMII, 10/100 Mbps |
| Display | SH1106, 128×64 monochrome OLED |
| Display interface | I²C, SDA GPIO32 / SCL GPIO33 |
| Display address | 0x3C or 0x3D, auto-detected |
| UP button | GPIO36 |
| DOWN button | GPIO39 |
| OK button | GPIO5 |
| Flash | 4 MB |
| Firmware layout | Two 1,966,080-byte OTA application slots |
| Firmware framework | Espressif ESP-IDF v6.1 |

Hardware configuration and GPIO definitions are maintained in [`main/board.h`](main/board.h).

## Building the firmware

Syndetis uses **Espressif ESP-IDF v6.1**, pinned to SDK commit:

`fff9895c82d744c7237be8847347bdd1b07c6643`

Development is supported on Linux and Windows with a correctly configured ESP-IDF environment.

From the project directory:

```bash
idf.py -B build-final build
python3 scripts/verify_build.py build-final
```

The resulting application image is:

`build-final/Syndetis.bin`

### Automated tests

The project includes host-based C tests, Python verification scripts, Node.js tests, and browser-based dashboard and OTA interface tests.

```bash
npm ci --ignore-scripts
npx playwright install chromium

python3 scripts/run_tests.py
python3 scripts/run_ota_crypto_tests.py --idf-path "$IDF_PATH"
python3 tests/ota_identity_test.py --idf-path "$IDF_PATH"
```

Additional build requirements and setup instructions are available in [`LINUX_SETUP.md`](LINUX_SETUP.md). The build environment and serial port should be adapted to the host operating system.

## Firmware updates

Syndetis supports occasional firmware maintenance over Ethernet or Wi-Fi.

From the device:

**Settings → Firmware Update**

Connect to the displayed Wi-Fi hotspot or use the device's active wired IPv4 address.

| Connection | Firmware update interface |
|---|---|
| Wi-Fi | `http://192.168.4.1:8080/update` |
| Ethernet | `http://<device-ip>:8080/update` |

Upload a trusted, strictly newer Syndetis application `.bin` file.

The updater validates the application image, writes to an inactive OTA partition, and supports trial-boot rollback.

Firmware updates require a controlled local network and stable power. HTTP transport does not provide encryption or cryptographic firmware-authorship authentication.

For USB recovery, see [`RESTORE_USB.md`](RESTORE_USB.md). Historical factory images and partition layouts must not be mixed with the current dual-slot firmware.

## Verification and release

**Firmware v0.5.2**

- Automated firmware build and regression verification: PASS.
- Hardware acceptance: 9/9 PASS.
- USB recovery readiness: PASS.
- Feature development: Frozen.
- Final release tag: Pending installed-image identity confirmation.

The current release candidate, build identity, verification records and documented maintenance limitations are available in:

- [`docs/RELEASE_MANIFEST.md`](docs/RELEASE_MANIFEST.md)
- [`docs/FINAL_FIELD_ACCEPTANCE.md`](docs/FINAL_FIELD_ACCEPTANCE.md)
- [`docs/FINAL_RELIABILITY_REPORT.md`](docs/FINAL_RELIABILITY_REPORT.md)

## Project philosophy

Syndetis follows three principles:

**Simple.** A technician should understand how to use the device without reading a manual.

**Practical.** Every function should address a real troubleshooting need.

**Reliable.** The device should perform repeated diagnostic operations without unnecessary configuration, restarting or intervention.

Syndetis is deliberately focused. It does not aim to replace a laptop or provide every possible network troubleshooting feature.

Its purpose is to give technicians the essential information they need, where they need it, with minimal effort.

---

**Syndetis — Connect. Diagnose. Move on.**
