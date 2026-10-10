# Syndetis firmware foundation

Target: WT32-ETH01 V1.4 / ESP32 / 4 MB flash / ESP-IDF 6.1.0.
The firmware version is defined only in `version.txt`; CMake stores it in the
ESP-IDF application descriptor. Both OLED About and HTTP read that descriptor.

Development version: **0.5.0-dev - passive LLDP capture (Phase B)**. A bounded
receiver parses nearest-bridge, untagged LLDP into an internal snapshot and the
read-only /api/switch-info endpoint. The final OLED Switch Info screen and CDP
are deferred. Hardware reception and runtime regressions remain validation gates.
See [LLDP_PHASE_B_0.5.0_DEV.md](LLDP_PHASE_B_0.5.0_DEV.md) for implementation,
verification limitations, and the physical test procedure.

## Current release: 0.4.2 — Fast Discover

Discover issues a controlled ARP sweep at at most 25 requests/second, collects
stable entries continuously into its own table, settles for approximately
500 ms, and pings only ARP-confirmed hosts. OLED progress shows the sweep and
host checking phases. Existing HTTP discovery fields remain compatible.
See [FAST_DISCOVER_0.4.2.md](FAST_DISCOVER_0.4.2.md) for timing reconstruction,
cache freshness policy, tests, image/OTA sizes and hardware verification limits.

## Hardware preserved

| Function | GPIO |
| --- | --- |
| LAN8720 PHY address | 1 (address, not a GPIO) |
| Ethernet oscillator enable | 16 |
| MDC / MDIO | 23 / 18 |
| External RMII REF_CLK input | 0 |
| TX_EN / TXD0 / TXD1 | 21 / 19 / 22 |
| RXD0 / RXD1 / CRS_DV | 25 / 26 / 27 |
| SH1106 SDA / SCL | 32 / 33 |
| UP / OK / DOWN | 39 / 36 / 35 |
| Future SD CLK / MOSI / MISO / CS (reserved, unused) | 17 / 14 / 4 / 15 |
| UART0 TX / RX | 1 / 3 |

OLED: SH1106 only, 128x64, address 0x3C, 3.3 V power, two-column offset.
Every page is addressed independently. Initialization clears all 132 columns
on all eight pages. There is no SSD1306 support.
Buttons remain active-low inputs with internal pull-ups and pull-downs disabled;
external approximately 10 kohm pull-ups to 3.3 V are required. Debounce remains
30 ms with 10 ms polling and one event per press. No long-press behavior.

## Architecture and menu

- `ui_oled.*`: SH1106 initialization and eight text rows, up to 21 characters.
  Lowercase labels render as uppercase in the 5x7 font.
- `ui_buttons.*`: unchanged GPIO polling/debounce, nonblocking press callback.
- `ui_menu.*`: a separate task owns navigation and all OLED drawing; button events
  enter a queue. Changed screens are refreshed; Ethernet and HTTP never draw.
- `network_status.*`: short Ethernet/IP observers copy actual link, IPv4 and MAC
  status under a critical section. Disconnect/stop/lost-IP clears stale addresses.
- `web_server.*` and `web_index.html`: local, offline HTTP interface.
- `firmware_update.*`: physical authorization, streamed application OTA and boot
  confirmation. State snapshots are protected; only the menu can unlock updates.

Main menu: AutoTest, Discover, Switch Info, Network Tools, Industrial, System.
UP/DOWN move and wrap; the main menu scrolls to expose the sixth item. OK selects.
AutoTest and Discover run physical-button diagnostics. Switch Info, Network Tools
and Industrial remain Coming soon placeholders. OK, UP or DOWN returns.
System contains Status, About, Firmware Update, Back. Status reflects actual link
and address; About displays the firmware version. System/Back returns to main.
Firmware Update first shows an explicit confirmation: OK enables; UP/DOWN cancel.
Discover provides bounded IPv4 host discovery. Industrial protocols, packet
capture and SD features remain unimplemented.

OLED/menu/button failures are logged and do not stop Ethernet or HTTP. A failed
OLED prevents blind firmware authorization; there is no web unlock fallback.

## Flash and partitions

`sdkconfig` selects normal ESP-IDF 4 MB flash, the built-in Two large size OTA
partitions layout and bootloader application rollback. `sdkconfig.defaults`
reproduces these choices in a fresh configuration. No custom table is used.

| Partition | Offset | Size | Bytes |
| --- | --- | --- | --- |
| nvs | 0x9000 | 0x6000 | 24576 |
| otadata | 0xF000 | 0x2000 | 8192 |
| phy_init | 0x11000 | 0x1000 | 4096 |
| ota_0 | 0x20000 | 0x1A9000 (1700 KiB) | 1740800 |
| ota_1 | 0x1D0000 | 0x1A9000 (1700 KiB) | 1740800 |

The OTA layout does not contain a factory application partition. Serial flashing
initializes otadata and loads ota_0. Subsequent browser uploads alternate slots.

## HTTP and authorization

HTTP uses port 80, and starts after Ethernet driver/network-stack initialization.
It needs no internet, external JS, fonts, images or CDN.

- `GET /`: status and browser firmware upload page.
- `GET /update`: same page, alternate URL for firmware updating.
- `GET /api/status`: JSON with version, link, IPv4, netmask, gateway, MAC, uptime,
  running partition, update lock/window and last update result.
- `POST /api/firmware`: raw application binary; requires
  `Content-Type: application/octet-stream` and `X-Syndetis-Upload: 1`.

On the OLED choose System -> Firmware Update -> OK to confirm. This opens a
five-minute window measured using the monotonic ESP timer. Selecting the menu
entry alone does not unlock. There is no HTTP unlock endpoint. Leaving the page
does not cancel the window; it expires automatically, and restart always locks.
Every authorized upload attempt consumes the window, whether successful or failed.
The original five-minute deadline also bounds the entire upload/validation.
A locked upload returns HTTP 403 with instructions to enable it physically.
Malformed type/header, too-small/oversize, wrong project/chip, disconnect, timeout,
write failure or validation failure is rejected, logged and displayed without
changing the boot partition or restarting. Re-enable physically for another try.

The handler buffers at most 4 KiB, checks ESP32/application metadata including
exact `syndetis_finder` project name before starting OTA, checks IDF chip revision
and flash-mode compatibility, and writes only the next inactive app partition.
It uses esp_ota_begin/write/end/set_boot_partition in order. Boot selection only
changes after the complete image passes esp_ota_end validation. Success is sent
to the browser and OLED before a controlled restart after 1.5 seconds.
Bootloader and partition-table updates are never accepted.

The project-name check is a compatibility filter, not a cryptographic signature.
During the physically opened window any reachable LAN client can attempt an upload.
This milestone uses local HTTP and does not configure secure boot, HTTPS or eFuses.

## Rollback

`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` uses normal ESP-IDF application rollback.
An OTA image is marked NEW, then PENDING_VERIFY on its first boot. Startup confirms
it only after NVS, network stack/event loop, Ethernet MAC/PHY driver/start and HTTP
server/route initialization succeed. It does not wait for link, cable or DHCP.
OLED/buttons/menu are optional and initialized after confirmation.
A reset/crash before confirmation causes the next boot to reject that pending image
and return to the previous valid slot. After confirmation, later faults do not
trigger automatic rollback. No custom rollback or anti-rollback eFuse policy.

## Build and read-only checks

In the existing installed ESP-IDF environment:

```powershell
Set-Location 'C:\Users\theoc\Documents\ChatGPT\Syndetis'
. 'C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1'
idf.py build
idf.py partition-table
python tests/verify_release.py
node tests/web_ui.test.cjs
```

CMake/Ninja rebuild the bootloader, app and table when configuration changes;
fullclean is not necessary for this change. No generated files are manually edited.
The release checker verifies app metadata, checksum/SHA256, OTA fit, partition MD5,
bounds and serial flash offsets. Browser tests run the actual embedded JavaScript
with mocked DOM/network objects. These checks do not replace real-device OTA testing.

Verified build result for the delivered 0.2.0 milestone:
- `idf.py build`: passed with ESP-IDF 6.1.0.
- `idf.py partition-table`: passed; table matches the layout above.
- Application binary: 437600 bytes (0x6AD60), approximately 75% of each slot free.
- Bootloader: 26240 bytes (0x6680), fits below the table at 0x8000.
- Release-artifact and embedded-browser checks: passed.
- No device was flashed, erased or contacted by the verification commands.

## First-time serial migration (for the user to run)

The old single-partition firmware cannot install the new bootloader and partition
table through application OTA. Perform one complete serial flash using CH341.
Earlier project monitor logs used COM4. Replace COM4 if the CH341 port changed; do not use app-flash for this migration.
Put the board in its usual serial download mode, using the already verified wiring.

```powershell
Set-Location 'C:\Users\theoc\Documents\ChatGPT\Syndetis'
. 'C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1'
idf.py build
idf.py partition-table
idf.py -p COM4 flash monitor
```

The generated flash command writes bootloader at 0x1000, partition table at 0x8000,
initial OTA metadata at 0xF000 and this application at 0x20000 with 4 MB flash settings.
NVS remains at 0x9000; no whole-chip erase is needed. The old app at 0x10000 is no
longer referenced by the new table. These commands have NOT been run on hardware.
Exit monitor with Ctrl+] after checking boot, menu, DHCP and the status page.

An explicit equivalent flash command (after selecting the correct port) is:

```powershell
python -m esptool --chip esp32 -p COM4 -b 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 4MB --flash-freq 40m 0x1000 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0xf000 build/ota_data_initial.bin 0x20000 build/syndetis_finder.bin
```

## Second firmware update through Ethernet

1. Keep this source/configuration and OTA APIs for future versions. For an easily
   observable second test, change only version.txt to 0.2.1, then run idf.py build.
   The delivered milestone remains 0.2.0; no 0.2.1 firmware has been pre-created.
2. Connect Ethernet, and find the actual DHCP IP under System -> Status.
3. Open http://<device-ip>/ in a browser on the local network. Check version and
   running partition (initially ota_0).
4. On the OLED select System -> Firmware Update; release OK, then press OK again
   on the confirmation screen. The countdown begins at 300 seconds.
5. Select exactly:
   C:\Users\theoc\Documents\ChatGPT\Syndetis\build\syndetis_finder.bin
   Do not select bootloader.bin, partition-table.bin or a merged flash image.
6. Click Install firmware. Observe upload progress, then device validation.
   Keep power connected. OLED shows UPDATING and DO NOT POWER OFF.
7. On Update OK the device restarts. Refresh/reopen the status page and verify
   the new version and ota_1. The window must be locked again. DHCP may assign
   another address; check the OLED Status screen if the page is unreachable.

## Hardware acceptance checks still required

- Boot without Ethernet cable and with OLED absent: neither should prevent core
  startup or cause rollback. Reconnect cable and verify DHCP/web status.
- Navigate every main/system entry; verify scrolling and one action per press.
- While locked, POST an image and verify 403 with no slot change/reboot.
- Enable physically, then let the five-minute timer expire; POST must return 403.
- Try a non-Syndetis image, truncated image, corrupted image and disconnected
  upload. Each attempt must lock again and retain the original running slot.
- Upload valid firmware, verify restart, version and alternating OTA slot.
- Test rollback with a deliberately failing test image only as a separate hardware
  test with serial recovery available; that test image is not included here.


## Syndetis 0.3.0 AutoTest

The verified Ethernet OTA foundation now includes a physically started, local
network diagnostic. Main -> AutoTest -> OK opens Ready; another OK starts.
Progress runs in a separate FreeRTOS task. Completion shows a summary; UP/DOWN
cycle seven pages (summary, link, IP/gateway, DHCP/mask, ping, DNS, notes).
OK returns to the main menu. Leaving a running test does not cancel it; selecting
AutoTest and OK again rejoins the existing run. Concurrent runs are prevented.
About uses the application descriptor version and credits T.Anastopoulos.
The existing OLED font renders text in uppercase.

`network_status.*` observes events and provides protected snapshots. Its worker
inspection uses read-only `esp_eth_ioctl` with `ETH_CMD_G_SPEED` and
`ETH_CMD_G_DUPLEX_MODE`, `esp_netif_dhcpc_get_status`, and
`esp_netif_get_dns_info`. `autotest.*` owns execution and a structured result;
OLED navigation and the new GET `/api/autotest` read snapshots only. The browser
has no diagnostic run endpoint/button. The last result survives the next run but
is RAM-only and clears on reboot. Ethernet disconnect/IP changes and active OTA
interrupt diagnostics safely. AutoTest startup failure is non-fatal.

Policy is centralized in `main/autotest_rules.h`:
- FAIL: link down, no valid IPv4/mask after up to 10 seconds, invalid gateway,
  configured gateway with no ICMP replies, or interrupted/unavailable gateway test.
- WARN: unknown negotiation, half duplex, valid IP without a started DHCP client,
  missing gateway, partial ICMP loss, missing DNS, failed/timed-out optional DNS.
- PASS: valid local connection, known full-duplex negotiation (10 or 100 Mbps),
  valid DHCP/IP, all gateway echoes returned, configured DNS. Internet is irrelevant.
- SKIP: checks whose prerequisites failed; DNS resolution without a configured
  hostname. Any FAIL wins; otherwise any WARN wins; SKIP adds no penalty.

IPv4 masks must be contiguous and nonzero. Loopback, multicast, unspecified,
network and broadcast addresses are rejected; /31 and /32 are supported. A
configured gateway must be a unicast host in the local subnet (/32 permits an
explicit off-subnet gateway). A missing gateway produces WARN, never a public
fallback ping. Only the current gateway is pinged: three echoes, 1-second receive
timeout, 500-ms interval. lwIP's `ping/ping_sock.h` ESP-IDF `esp_ping_*` session
API supplies counts and millisecond min/average/max latency plus loss.

DNS resolution is optional: ESP-IDF menuconfig -> Syndetis AutoTest -> Optional
site-local DNS test hostname. The default is empty; DNS server configuration is
checked and resolution is SKIP. Set a site-provided hostname to use lwIP's async
`dns_gethostbyname_addrtype` submitted through `tcpip_try_callback`, with a
5-second worker wait. Persistent callback storage handles late completions.
No public hostname, Internet check, discovery, SD card, or run history is added.
Only the built-in `lwip` dependency is added; no managed dependency changes.

Offline checks after `idf.py build`:

```powershell
node tests/web_ui.test.cjs
python tests/verify_autotest_rules.py
python tests/verify_release.py
```

The policy test compiles 39 C static assertions against the actual firmware rules
with the configured ESP-IDF compiler; no target execution is needed. Browser tests
exercise PASS/WARN/FAIL presentation, retained results and the existing raw OTA
upload/authorization behavior. Release checks verify image/version/checksums,
4-MB layout and OTA slot fit. Actual cable/DHCP/ping/button behavior needs hardware
verification after upload. No flashing is performed by these checks.

Upload only `C:\Users\theoc\Documents\ChatGPT\Syndetis\build\syndetis_finder.bin`
through the existing web updater. The verified OTA partition layout, bootloader,
rollback, physical authorization, five-minute window and upload endpoint are
unchanged. Ethernet configuration, SH1106 driver and all GPIO assignments are
unchanged. Earlier sections document the historical 0.2.0 foundation; the current
release is 0.4.1. The 0.3.0 AutoTest foundation was verified on hardware.

## Syndetis 0.4.0 Discover

Main -> Discover shows the actual local subnet and candidate count. OK starts
asynchronous IPv4 discovery; UP/DOWN returns. A /24 has 254 usable addresses and
253 candidates after excluding Syndetis itself. Larger networks are rejected.
Network and broadcast addresses are always excluded; /31 and /32 have zero
candidates under this release's exclusion policy.

Discovery uses four concurrent one-packet ICMP probes, each with an 8-byte
payload and 250-ms receive timeout, bound to Ethernet. After each batch, a
TCP/IP-thread callback reads stable lwIP ARP entries and copies available MACs.
ARP confirmation OR an ICMP response counts as present; blocked ICMP is not
an offline verdict. Results are stored in ascending numeric IP order.

The local ESP-IDF ping implementation retires deleted tasks on a one-second
idle check. A conservative 1100-ms pause prevents task accumulation between
batches. A silent /24 therefore takes approximately 86.4 seconds plus scheduling
and ARP-callback overhead. A 120-second deadline is checked between batches;
cleanup may add a few seconds. This favors bounded RAM/task use over scan speed.

Discovery and AutoTest share a diagnostic coordinator. AutoTest's existing
packets, timing and PASS/WARN/FAIL rules are unchanged. Discovery stops safely
on link/IP revision changes or active OTA. Aborted scans report an error and
clear the device list. Permanent callback storage prevents late-callback reuse.

Completion shows found count and duration. OK opens the device list; UP/DOWN
navigates, OK selects a device. The list ends with Back (also for zero results).
Device details show IP, MAC or Unknown, ARP status, and RTT or ICMP: NO REPLY;
OK returns to the list. UP/DOWN on the completion page returns to the main menu.
Leaving the scanning page does not cancel discovery; entering Discover and
pressing OK rejoins it.

`GET /api/discovery` is read-only; the offline browser polls it every two seconds
and shows a compact IP/MAC/ICMP/RTT table after completion. There is no HTTP scan
start route or browser start button. No ports, services, industrial protocols,
hostnames or vendors are queried. No dependencies or GPIO assignments change.

Run `python tests/verify_discovery_logic.py` alongside the existing checks.
It evaluates the actual pure firmware functions as C++ constexpr, covering
/24, /25, /26, /30, rejected /16, /31-/32 exclusion policy, invalid masks/IPs,
host exclusions, numeric sorting and ARP/ICMP presence. The discovery result
table is 5080 bytes (254 x 20); the fixed HTTP snapshot uses another 5080 bytes.
Full implementation and release details are in `DISCOVERY_0.4.0.md`.

## Syndetis 0.4.1 Link Loss / Recovery

This stability release preserves Discover's four probes, 250-ms timeouts,
1100-ms retirement pauses and approximately 86-second /24 pacing.

The installed ESP-IDF 6.1 EMAC DMA receive/flush implementation contains
unbounded circular-descriptor walks when a terminal marker is missing. A local
patch bounds these walks; a regression evaluates the actual flush function on
a synthetic partial ring. The original routine fails to terminate; the patch
returns after one ring. This is the strongest source-level freeze candidate,
but confirmation that it caused the observed hardware freeze still requires
cable-removal testing or a captured task backtrace.

Discover metadata/device getters now use short protected copies instead of an
indefinitely waited task-owned mutex. Link/IP observers publish generations
before the SDK's default netif handlers and queue logs without waiting. AutoTest
inspection runs in a TCP/IP callback with a bounded worker wait. Coordinator
reservations carry tokens; callbacks reject old network revisions/operations.
An additional local ping patch observes stop requests and a receive budget even
under unrelated ICMP traffic. Packet counts and normal timeouts are preserved.
The installed SDK remains untouched; top-level CMake substitutes two checked-in
compilation units. Review these patches before changing ESP-IDF versions.

Disconnect clears Status IPv4 (`IP: -`). Discover reports `ABORTED_LINK_DOWN`
or an explicit network-change/OTA error, clears partial results, retires sessions
with bounded waits, and releases the coordinator. AutoTest records Link FAIL
and an interrupted-network note. Late callback storage remains valid and is
reaped before reuse. Reconnection uses the existing Ethernet/esp-netif DHCP
flow, without resetting the ESP32 or restarting HTTP.

Run all existing checks and `python tests/verify_link_loss.py` after building.
See `LINK_LOSS_0.4.1.md` for the full investigation, lock audit, test evidence,
artifact measurements and hardware acceptance checklist. No hardware was flashed.


## PROFINET Identify prototype (0.7.0-dev)

Industrial -> PROFINET -> Scan sends one Identify request and collects replies for three seconds, up to 32 devices. Ethernet link is required; DHCP/IPv4 is not. OK cancels. Results provides advertised device fields. The web panel and GET /api/profinet are read-only. No DCP configuration operations exist. Hardware acceptance remains pending. See PROFINET_DCP_PHASE_B_0.7.0_DEV.md for implementation, build evidence and manual acceptance. Run python tests/verify_profinet.py alongside existing checks. No flash or OTA was performed.
