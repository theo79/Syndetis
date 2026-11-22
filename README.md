# Syndetis

**Connect → Diagnose → Move on.**

Syndetis is a handheld Ethernet diagnostic tool for technicians. It provides quick port and addressing checks through a 128×64 OLED and three buttons. It is not a laptop replacement, network scanner or continuous monitoring appliance.

**Current accepted release: v0.5.2.** The released image passed user-reported hardware acceptance and post-flash smoke testing. See the [v0.5.2 release manifest](https://github.com/theo79/Syndetis/blob/9717f1745724a11a64864a1c5627e393f7d49eca/docs/RELEASE_MANIFEST.md).

## Technician workflow

1. Connect Ethernet; read link up/down and negotiated 10/100 Mbps speed/duplex.
2. Read LLDP/CDP switch and port advertisements when available.
3. Explicitly start DHCP, or apply temporary Manual IPv4/prefix/optional gateway with ARP conflict checks.
4. Ping the gateway or a custom IPv4 target with four ICMP requests.
5. Stop/release addressing as needed and move to the next port.

UP/DOWN navigate; short OK selects; hold OK for about 500 ms goes back/cancels. Back retains an acquired address as indicated on screen. Stop / Release or USE DHCP clears the current addressing session. Disconnect invalidates old addresses, neighbors and Ping results; reconnect does not silently restart IPv4 tests. Ethernet stays passive until an explicit IPv4 action. Wi-Fi starts only when requested for web access or maintenance.

Missing advertisements do not prove a bad port; no DHCP lease does not prove a broken cable; no Ping reply does not prove a host is offline. The 10/100 PHY reports its negotiated speed, not a switch port's maximum capability.

## Diagnostic Reports

The M7 dashboard export and OLED menu entry are unreleased development features. Physical hardware acceptance is pending.

1. On the OLED main menu, choose **Web Access → Enable** and wait for Web Access to start.
2. Open **Web Access → Connection details** for the hotspot name and password. Join that Wi-Fi network and open <http://192.168.4.1/> in your browser.
3. In the dashboard's **Diagnostic report** section, wait for the first status update, then select **Download diagnostic report** to save a JSON file.

The report includes firmware version and uptime, Ethernet initialization and link status, negotiated speed and duplex, wired addressing mode and state, DHCP status and timing, and LLDP/CDP discovery status, counters, neighbor protocol, TTL, age, and expiry. It includes Ethernet and DHCP error details when available. Ping statistics and device wall-clock timestamps are unavailable in the report.

Network identifiers are omitted by default. Select **Include network identifiers** to include available MAC and IP addresses, subnet, gateway and DNS settings, and switch, chassis, port and VLAN details. These fields can reveal network topology when you share the file.

The report uses the last valid dashboard snapshot and records browser UTC receipt and export times, snapshot age, and whether the data is stale. A failed status poll or a snapshot older than four seconds marks the data stale. You can still download it after disconnecting if the dashboard tab stays open and has received a valid update. Reloading or closing the page discards the cached snapshot.

The OLED **Diagnostic Report** item follows **Settings** in the main menu. It shows the dashboard URL when Web Access is on, asks you to enable Web Access when it is off, and shows **Check Web Access** during startup, shutdown, or an error. Short or held OK returns to the main menu with the item selected. Wi-Fi activation stays in Web Access, and the download happens in the browser.

## Supported hardware

- ESP32 **WT32-ETH01**, onboard **LAN8720** 10/100 RMII Ethernet.
- **SH1106 128×64** I2C OLED: SDA32 / SCL33; address 0x3C or 0x3D, detected at startup.
- UP GPIO36, DOWN GPIO39, OK GPIO5; board wiring/options in [main/board.h](main/board.h).
- Configured 4 MiB flash, two **1,966,080-byte OTA slots**. Historical single-factory firmware needs a separately planned USB migration; an application update cannot change that layout.

Hardware results are [user-reported](https://github.com/theo79/Syndetis/blob/9717f1745724a11a64864a1c5627e393f7d49eca/docs/FINAL_FIELD_ACCEPTANCE.md); they do not establish a particular board serial number, flash readback or destructive rollback result.

## Build and verify

Activate **ESP-IDF v6.1**, exact SDK commit `fff9895c82d744c7237be8847347bdd1b07c6643`, with its installed ESP32 compiler/Python tools. The component lock pins LAN87xx 1.1.0. A fresh build copies the reviewed `sdkconfig.pinned`; keep its target/configuration and partition layout.

```bash
idf.py -B build-final build
python3 scripts/verify_build.py build-final
python3 scripts/verify_checksums.py --manifest recovery/SHA256SUMS.txt
```

The raw application is `build-final/Syndetis.bin`. Build timestamps/ELF metadata can change its bytes; verify the recorded hash rather than inferring identity from version. Build directories and local release copies are ignored. See [Linux setup](LINUX_SETUP.md) for prerequisites; adapt environment activation to the host.

Host tests require a C compiler, Python with cryptography, Node/npm and Playwright Chromium:

```bash
npm ci --ignore-scripts
npx playwright install chromium
python3 scripts/run_tests.py
python3 scripts/run_ota_crypto_tests.py --idf-path "$IDF_PATH"
python3 tests/ota_identity_test.py --idf-path "$IDF_PATH"
```

Use `BROWSER_CHANNEL=chrome` with the runner only when installed Chrome is the chosen browser. The full runner includes browser tests; `--skip-browser` is partial coverage. Signature/identity scripts test retained historical tooling, not a provisioning requirement for current firmware. Historical `SOURCE_SHA256SUMS.txt` and `docs/VERIFICATION.*` describe the preserved 0.4.0 checkpoint, not current sources.

## Occasional maintenance and recovery

On an already migrated device, physically select **Settings → Firmware Update**. Use the displayed hotspot and `http://192.168.4.1:8080/update`, or the current explicitly acquired wired address at `http://<wired-ip>:8080/update` from the same subnet. Upload only a trusted **strictly newer raw application .bin**, never a bootloader, table, merged image or old signed package. No device identity provisioning is required. The hotspot port-80 dashboard is separate and remains blocked on diagnostic Ethernet.

OTA uses an inactive slot, ESP image validation and trial-boot rollback. HTTP does not encrypt or authenticate firmware; use a controlled local connection. Keep power stable and recovery available. Socket pressure, slow rejected uploads and immediate reopen during teardown are retained noncritical maintenance limitations; allow cleanup to finish before retrying and avoid concurrent dashboard clients. Detailed boundaries are in the [reliability report](docs/FINAL_RELIABILITY_REPORT.md) and [maintenance guide](docs/milestone6/README.md).

[RESTORE_USB.md](RESTORE_USB.md) describes verified historical recovery sets and UART wiring. Keep legacy factory images/tables separate from dual-slot artifacts. Never erase flash as a routine update or modify eFuses/provisioning. Repository verification performs no device operations.

[Cleanup record](docs/REPOSITORY_CLEANUP.md) explains the external archive, retained historical material and Git exclusions.

**The accepted v0.5.2 release is feature-frozen for its intended portable Ethernet diagnostic role. Future changes require a demonstrated technician need or a confirmed reliability defect.**
