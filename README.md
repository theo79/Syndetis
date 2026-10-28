# Syndetis

**A compact Ethernet diagnostic tool for technicians in the field.**

**Plug in. Check the port. Understand the network.**

Syndetis is an ESP32-based handheld network troubleshooting prototype for IT, networking, SCADA and industrial field work. Its purpose is to answer common connectivity questions using a small OLED screen and three buttons—without opening a laptop just to check a switch or PLC connection.

> **Project status — 28 October 2026:** Functional hardware prototype. Firmware **v0.5.0** was installed using the initial USB dual-OTA migration. A **v0.5.1 Wi-Fi OTA update via the local browser has been confirmed successful by the developer**. Ethernet OTA is implemented and software-tested but awaits a physical update test using a strictly newer version. Enclosure/productization remains in progress. This is not a production-certified network tester.

## Features

| Feature | What it does |
| --- | --- |
| Ethernet link | Shows link up/down and negotiated 10/100 Mbps speed and duplex |
| Passive neighbor discovery | Decodes LLDP and Cisco CDP identity/port information when advertised |
| DHCP test | Explicitly requests an IPv4 lease, reports addressing and handles timeout/cancel |
| Manual IPv4 | Temporary address/prefix/gateway with conflict checks |
| Ping | Four ICMP requests to the gateway or a custom IPv4, with replies/loss/average RTT |
| OLED and buttons | 128×64 SH1106 display, UP / DOWN / OK and minimal task-oriented menus |
| Wi-Fi hotspot | Local diagnostics dashboard at `http://192.168.4.1/` when enabled |
| Network firmware update | Physically enabled HTTP browser updater over Wi-Fi or Ethernet, raw `.bin` upload, OLED progress and reboot |
| OTA rollback | Dual application slots, new-image trial confirmation and rollback support |
| USB recovery | Serial ROM programming remains available if needed |

Syndetis reports observed facts rather than guessing the cause of every failure. For example, an unanswered ping does not by itself prove the target is offline, and missing LLDP/CDP advertisements do not prove nothing is connected.

## Hardware

- **Controller:** WT32-ETH01 / classic ESP32; inspected prototype ESP32-D0WD rev v1.1 with 4 MiB SPI flash
- **Ethernet:** LAN8720A, 10/100 Mbps
- **Display:** SH1106 128×64 monochrome OLED
- **Controls:** three physical buttons — UP, DOWN, OK
- **Firmware:** native Espressif **ESP-IDF v6.1**

The prototype has been exercised with Cisco networking equipment and Siemens industrial Ethernet devices. It is intended to become a compact enclosed field tool, rather than remain a breadboard with loose wiring.

## Firmware updates — Wi-Fi and Ethernet

The intended update process is deliberately simple:

1. On the device, select **Settings → Firmware Update** to open a temporary maintenance session.
2. Connect a laptop or phone to the Syndetis Wi-Fi hotspot, or use the device's current Ethernet IPv4 address on the permitted local network.
3. Open the browser updater and choose a **newer raw Syndetis application `.bin`**.
4. Upload; the OLED shows **Preparing update → Uploading XX% → Verifying → Update complete / Rebooting**, or a short error.
5. After reboot, confirm the new version in Device Info.

| Interface | Firmware update page |
| --- | --- |
| Wi-Fi hotspot | `http://192.168.4.1:8080/update` |
| Ethernet | `http://<current-device-ip>:8080/update` |

**Confirmed hardware result:** The developer reports that Wi-Fi browser OTA successfully updated the device on 28 October 2026, using the prepared v0.5.1 release. Ethernet browser OTA is implemented and passes software/browser regressions; a real Ethernet upload has **not** yet been reported as successful.

**Important version transition:** The old v0.5.0 updater still required a one-time code for the upload that installed v0.5.1. The code was **removed in the v0.5.1 firmware**; newer sessions on v0.5.1 use physical authorization without a code. Do not confuse the behavior of the uploading version with the candidate version.

The updater restricts access to a short, physically enabled maintenance window, validates image format/compatibility/length/version/hash, writes the inactive OTA slot and supports rollback. Ordinary browser dashboard access on customer Ethernet remains blocked.

**Prototype security boundary:** Updates use local **HTTP**, not HTTPS, and images are **not cryptographically signed**. Hash checks detect unintentional corruption but do not authenticate the publisher. An attacker with permitted local network access during an active update window could potentially substitute firmware. This tradeoff is intentional for the current KISS prototype, not a claim of production-grade secure OTA.

## Firmware layout

The inspected device has **4 MiB flash**. The Milestone 6 partition layout is:

| Partition | Offset | Size |
| --- | --- | --- |
| `nvs` | `0x9000` | `0x6000` |
| `phy_init` | `0xF000` | `0x1000` |
| `otadata` | `0x10000` | `0x2000` |
| `ota_0` | `0x20000` | `0x1E0000` |
| `ota_1` | `0x200000` | `0x1E0000` |
| `ota_keys` | `0x3E0000` | `0x20000` (reserved/unused in HTTP prototype) |

The first conversion from the original factory partition to this layout required USB programming. Normal future application upgrades should use the network updater. Do not use a full-chip erase as a normal update workflow.

## Build and test — OS-independent

Development is **not tied to Kubuntu or any absolute filesystem path**. Supported development hosts include Windows and compatible Linux distributions such as Ubuntu/Kubuntu, Fedora and Arch. Install the official **ESP-IDF v6.1** toolchain and activate its environment using the instructions appropriate for the host.

The project root is the directory containing `CMakeLists.txt`, `main/`, `scripts/`, `tests/` and `partitions.csv`.

```sh
idf.py -B build-m6 build
idf.py -B build-m6 size
python scripts/verify_build.py build-m6
python scripts/run_tests.py
```

Tests need Node.js 20+, Python with cryptography (available in the activated SDK environment), and a native C compiler (GCC or Clang). The browser regression suite uses Playwright and project Node dependencies. From the directory containing `package-lock.json`:

```sh
npm ci --ignore-scripts
npx playwright install chromium
```

On Linux, install Chromium OS dependencies if Playwright requests them (`npx playwright install --with-deps chromium`); on Windows use its host-specific installation. Run tests in the activated SDK Python environment. See [Linux setup](LINUX_SETUP.md) and [v0.5.1 release evidence](docs/milestone6/M6D_WIFI_OTA_0.5.1.md).

Use the host-specific serial port for inspection or emergency USB recovery (`COMx` on Windows, often `/dev/ttyUSB*` on Linux). Serial wiring and manual ESP32 ROM boot entry may be required on WT32-ETH01 hardware. Do not make `idf.py flash` part of the normal OTA update workflow.

**Pinned reference SDK:** ESP-IDF v6.1, commit `fff9895c82d744c7237be8847347bdd1b07c6643`. See `docs/milestone6/` for migration, build and hardware acceptance evidence. Paths and activated virtual environments differ by host.

## Development principles

- **KISS:** Simple, usable, working; no feature bloat.
- **Three buttons:** Clear field workflows without a user manual.
- **Passive discovery:** LLDP/CDP listening, not broad active scanning.
- **Explicit address operations:** DHCP or Manual IPv4 on demand, with resource ownership and cleanup.
- **Practical OTA:** Physical authorization → select `.bin` → progress → reboot.
- **No mandatory secrets infrastructure:** The prototype does not require one-time codes (v0.5.1 onward), production signing certificates, CA provisioning or a fixed wired IP.
- **Preserve recovery:** Keep a verified flash backup and USB ROM access during enclosure development.

Continuous monitoring, packet capture, long-term storage and additional diagnostic menus are outside the current scope.

## Validation and next steps

- Hardware field checks for link, neighbor discovery, DHCP, Manual IPv4 and Ping: **performed**.
- Initial v0.5.0 USB migration to dual OTA layout: **user-confirmed successful**.
- v0.5.1 Wi-Fi browser OTA: **user-confirmed successful**.
- OLED progress, browser flow, validation and rollback: **automated tests pass**; detailed real-device progress/rollback measurements may still need recording.
- **Next:** prepare a strictly newer firmware release and perform the first real **Ethernet OTA** upload; verify reboot, version and preserved diagnostics.
- **Later:** finish enclosure and field reliability checks, keeping USB as emergency recovery.

## Repository and preservation

This repository is the **private source repository** for Syndetis. Never commit raw device flash backups, customer-network captures, generated build directories, credentials or other private material. The checked-out repository and `docs/milestone6/` contain the authoritative implementation and validation evidence; keep the portable handoff beside a complete copy when migrating development machines.

---

**Syndetis is a focused field tool: plug in, test, understand—and update it without opening the case.**
