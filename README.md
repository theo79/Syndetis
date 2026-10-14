# Syndetis One

Syndetis One is a handheld Ethernet diagnostic tool for field network technicians
and engineers, including industrial environments. Connect to a port, run a test,
read the result, and choose the next useful action using UP, DOWN, and OK.

This source snapshot targets WT32-ETH01 V1.4, ESP32, LAN8720 Ethernet, 4 MB flash,
and an SH1106 128 x 64 OLED. Its firmware version is **0.7.0-dev**, as defined in
`version.txt`. Reports named 0.8.0 describe SNMP development work; they do not
change the application version.

## Implemented workflow

The main menu contains **Quick Test, Discover, Network Tools, Industrial, System**.
UP/DOWN select items or pages. A short OK press selects or performs the displayed
action; holding OK for 450 ms generates Back. Button polling is 10 ms with 30 ms
debounce. Short OK events are generated on release.

| Function | Current source behavior |
| --- | --- |
| Quick Test | Checks link, speed/duplex negotiation, DHCP/IP configuration, gateway reachability, and DNS configuration. Results use PASS, WARN, FAIL, and SKIP with detail pages. IPv4 acquisition waits up to 10 seconds. DNS resolution is optional and disabled by default; no default Internet target is used. |
| Discover | Scans the local IPv4 subnet using ARP, then checks ARP-confirmed hosts with ICMP. Displays hosts, MAC addresses, available vendor/hostname information, and ping results. Subnets with more than 254 usable addresses are rejected. /31 and /32 yield no candidates. |
| Ping | Sends one ICMP echo to a selected discovered host. |
| Cont Ping | Repeats ping to a selected discovered host, showing loss and round-trip statistics. OK requests stop; holding OK requests stop and return. Each probe has a 750 ms timeout with nominal one-second pacing. |
| SNMP | Explicit query to a selected discovered host using SNMP v2c and community `public`. Reads sysName, sysDescr, sysObjectID, sysUpTime, sysContact, and sysLocation. One retry, with 750 ms receive wait per attempt. Available through OLED and browser. |
| Switch Info | Passive LLDP/CDP capture with OLED pages and a read-only HTTP snapshot. Located under Network Tools. |
| Industrial | PROFINET DCP Identify discovery, started from the physical menu. Sends one Identify All request, collects responses in a three-second window, and stores up to 32 devices. Starts have a ten-second cooldown. No configuration-changing DCP command is implemented. |
| System | Network status, firmware About, and physically authorized firmware update. |

Discover sends at most 25 ARP requests per second, allows a 500 ms settle period,
and uses up to four concurrent ICMP probes with 250 ms timeouts. Optional reverse
DNS enrichment has a 2.5-second budget, at most 16 attempts, and two concurrent
queries. Vendor labels come from the checked-in OUI subset; missing labels are
not evidence of a missing device. An ARP-visible host can remain discoverable
without answering ping.

Host actions revalidate the selected discovery identity and network generation.
Link/network changes invalidate affected results. Cancellation behavior differs
by operation: ping and PROFINET have explicit stop/cancel paths; leaving Quick
Test or Discover does not itself cancel the worker. An in-flight SNMP query
finishes its bounded I/O and rejects a stale result after network/identity change.
These are implementation behaviors, not measured hardware timing guarantees.

## Hardware configuration

Assignments below are taken from `main/app_main.c` and `main/board_pins.h`.

| Function | Assignment |
| --- | --- |
| LAN8720 PHY address | 1 (PHY address) |
| External oscillator enable | GPIO16 |
| MDC / MDIO | GPIO23 / GPIO18 |
| External RMII REF_CLK input | GPIO0 |
| SH1106 SDA / SCL | GPIO32 / GPIO33 |
| UP / OK / DOWN | GPIO39 / GPIO36 / GPIO35 |
| Reserved SD CLK / MOSI / MISO / CS | GPIO17 / GPIO14 / GPIO4 / GPIO15; unused |

GPIO16 is driven high before MAC initialization. No PHY reset GPIO is configured.
The OLED driver uses I2C address 0x3C and a two-column SH1106 offset; it does not
implement SSD1306 support. Buttons are active-low and require external 10 kohm
pull-ups to 3.3 V; internal pulls are disabled.

Battery and enclosure choices are not finalized. Test history, persistent result
logging, and SD storage are outside the current scope. Development logs emitted
by the firmware are not a test-history feature.

## Build

Use **ESP-IDF 6.1.0** with the ESP32 target and its installed toolchain. Activate
the ESP-IDF environment, open a terminal in this repository, then run:

```sh
idf.py build
idf.py partition-table
```

`dependencies.lock` pins `espressif/lan87xx` to **1.1.0**. Its checked-in component
and license are included in `managed_components/`. Both `sdkconfig` and
`sdkconfig.defaults` are supplied. The configuration selects 4 MB flash, two large
OTA application partitions, and bootloader application rollback.

The internal CMake project name is `syndetis_finder`; the OTA compatibility check
requires that name. Firmware version metadata comes from `version.txt`.

Top-level CMake replaces two SDK source units with project-local copies: EMAC DMA
receive handling and the SDK ping socket implementation. Read
[patches/README.md](patches/README.md) for the changes and upstream source hashes.
CMake rejects SDK major/minor versions other than 6.1; that guard does not establish
compatibility with every 6.1 patch release. Review the copies against the exact SDK
before upgrading. The installed SDK is not modified by this build configuration.

## Local browser interface

After Ethernet obtains an address, open `http://<device-ip>/`. The embedded page
uses no CDN or external assets. HTTP listens on port 80.

| Method | Path | Purpose |
| --- | --- | --- |
| GET | `/`, `/update` | Embedded browser interface and firmware upload |
| GET | `/api/status` | Device/network status and OTA state |
| GET | `/api/autotest` | Latest Quick Test state/results |
| GET | `/api/discovery` | Latest discovery state/hosts |
| GET | `/api/switch-info` | Passive switch information |
| GET | `/api/profinet` | PROFINET discovery state/devices |
| POST | `/api/snmp/query` | Explicit asynchronous SNMP query |
| GET | `/api/snmp/status` | Latest SNMP request state/result |
| POST | `/api/firmware` | Authorized application OTA upload |

Quick Test, Discover, and PROFINET start from the physical menu; their HTTP routes
are read-only. Ping tools currently have no HTTP endpoint. SNMP POST requires an
empty body, `X-Syndetis-SNMP: 1`, and a discovered numeric IPv4 target in the `ip`
query parameter. When that record has a MAC, supply the matching twelve-digit hex
`mac` parameter. Queries are explicit, serialized, and shared across clients.
SNMP SET, WALK, v3, and user-configurable communities are not exposed.

## Firmware update and recovery

Select **System -> Firmware Update** and confirm with OK to open a five-minute,
single-use upload window. Upload an application binary using the browser. The
HTTP request requires `Content-Type: application/octet-stream` and
`X-Syndetis-Upload: 1`. Each authorized attempt consumes the window.

The handler validates compatibility and writes the inactive application slot.
Boot selection changes only after image validation succeeds. Successful upload
schedules a restart. Bootloader/partition-table uploads are not accepted.
Physical authorization permits reachable LAN clients to attempt an upload during
the window; the project-name check is not a cryptographic signature. This source
uses HTTP and does not configure secure boot or HTTPS.

On first boot after OTA, the application confirms rollback eligibility after core
network and HTTP initialization. Confirmation does not require cable link, DHCP,
OLED/buttons, or successful optional diagnostic worker initialization. A reset
before confirmation permits bootloader rollback; later faults after confirmation
do not automatically roll back.

For initial serial installation, connect the board in its supported download mode
and use the actual serial port:

```sh
idf.py -p YOUR_PORT flash monitor
```

The two-OTA layout has no factory application partition. Migrating firmware with
a different partition layout requires serial flashing of the full build rather
than application-only OTA. Serial flashing was not performed for this package.

## Verification and status

The table above describes **implemented source behavior**. This README update and
GitHub packaging did **not** run a firmware build, host tests, or hardware tests.
The included development reports contain earlier validation claims and procedures;
they are not certification of the current snapshot on hardware.

After building, representative checks are:

```sh
python tests/verify_release.py
python tests/verify_field_ui.py
python tests/verify_ping_runtime.py
python tests/verify_snmp_runtime.py
node tests/web_ui.test.cjs
node tests/snmp_runtime_ui.test.cjs
```

Additional `tests/verify_*.py` scripts cover discovery, diagnostic rules, link loss,
protocol parsers, and API/UI behavior. Many require generated `build/` metadata
and the configured compiler. Some contain machine-specific SDK/toolchain paths;
inspect and adapt those before running on another machine. Fixtures and browser
mocks do not prove physical Ethernet reception, timing, heap stability, or OTA
recovery.

Hardware validation remains necessary for OLED/buttons, DHCP and link recovery,
responsive ping stop, passive LLDP/CDP reception, PROFINET identification, SNMP
responses/timeouts, repeated-operation memory use, and successful/failed OTA plus
rollback. Record the firmware revision, SDK, wiring, network conditions, and actual
results when performing those checks.

## Repository contents

- `main/`: application, diagnostics, protocol handling, OLED/buttons, and web UI.
- `patches/`: SDK replacements, patch diffs, and compatibility notes.
- `managed_components/`: checked-in LAN87xx dependency and its license.
- `tests/`: verification scripts, protocol fixtures, and browser tests.
- Root milestone reports: historical implementation notes and validation procedures.
- [GITHUB_SETUP.md](GITHUB_SETUP.md): GitHub upload instructions and packaging notes.

Build outputs and generated validation evidence are excluded. No license for the
original Syndetis code has been selected; included upstream notices remain in place.
