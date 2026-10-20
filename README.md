# Syndetis

WT32-ETH01 Ethernet field tool, firmware **0.3.0**, native **ESP-IDF v6.1**.
A portable Ethernet field tool with OLED/button navigation, physical link status,
passive LLDP/CDP neighbor information and a physically enabled offline dashboard.
Short OK selects on release; a 500 ms hold goes back. Browser access is OFF at boot.

Hardware reception tested, as reported by the owner: **LLDP with a Siemens
S7-1200** and **CDP with a Cisco device**. This establishes reception only;
field-by-field accuracy, lifecycle and regression checks below remain pending.
No deployment organization, device identity or field network address is recorded.

No Ethernet IP configuration, Ethernet DHCP client, active discovery/scanning, ping, TCP probes,
SD, OTA, Wi-Fi station mode, routing or NAT is implemented.

## Wiring and board verification

| Connection | Assignment |
| --- | --- |
| Board | ESP32 WT32-ETH01 |
| OLED | SH1106, 1.3-inch, 128 x 64, I2C |
| OLED SDA / SCL | GPIO32 / GPIO33 |
| UP / DOWN / OK | GPIO36 / GPIO39 / GPIO5 |
| Each button | External 10 kOhm pull-up to 3.3 V; switch to GND; active LOW |
| Green power LED | Hardware powered; no GPIO assignment |
| Ethernet PHY | Onboard LAN8720A, address 1 |
| MDC / MDIO | GPIO23 / GPIO18 |
| Oscillator enable | GPIO16, HIGH enables the onboard external clock |
| RMII reference clock | External 50 MHz input on GPIO0 |
| Fixed ESP32 RMII | TX_EN21, TXD0 19, TXD1 22, RXD0 25, RXD1 26, CRS_DV27 |

Verified against the [Wireless-Tag WT32-ETH01 V1.4 schematic (mirror)](https://github.com/egnor/wt32-eth01/blob/main/WT32-ETH01_V1.4.schematic.pdf)
and [manufacturer datasheet hosted by Seeed](https://files.seeedstudio.com/products/102991455/WT32-ETH01%20datasheet.pdf).
The schematic labels GPIO16 as `IO16_OSC_EN` and shows the oscillator feeding
GPIO0 and the LAN8720A. GPIO16 is the board's clock enable, not a substitute
PHY reset wire. The PHY address strap is pulled HIGH. ESP-IDF's ESP32 RMII IOMUX
mapping was also checked in the installed v6.1 source. No alternative wiring
is selected or tried automatically.

Ethernet uses `EMAC_CLK_EXT_IN`; it does not generate a clock on GPIO0/16/17.
PHY `reset_gpio_num` is explicitly **-1**: the board has hardware reset wiring,
and the SDK default GPIO5 must never drive the OK button. Keep GPIO0 released
from the programming strap for normal operation. Do not connect peripheral
signals to Ethernet/clock pins.

Use a common ground and 3.3 V compatible I2C/button levels. Check the OLED's
external SDA/SCL pull-ups to 3.3 V; weak internal I2C pulls are supplementary.
Button internal pulls remain disabled; GPIO36/39 have no internal pull-ups.
GPIO5 is a strapping input: retain the cold boot/reset test with OK held.

All board/display settings are in `main/board.h`. The working settings remain
address auto-probe, orientation 0, column offset 2, I2C 400 kHz. Both possible
SH1106 7-bit addresses (0x3C/0x3D) are probed, requiring one ACK. A configured
address is still probed. An ACK confirms presence, not controller identity.
Missing/ambiguous addresses log a clear error and stop the UI. The renderer
uses SH1106 page addressing, clears 132 RAM columns and draws 128 visible ones.

## Build, USB-UART flash and serial monitor on Windows

Exact SDK: **ESP-IDF v6.1**, header 6.1.0, GNU **15.2.0**, Python **3.11.15**.
Target `esp32`, configured flash 4 MiB. See `VALIDATION.md` for actual results.

ESP-IDF 6.1 moved this PHY driver out of the SDK. The only managed component is
the official [Espressif LAN87xx driver](https://components.espressif.com/components/espressif/lan87xx/versions/1.1.0),
pinned to **1.1.0** in `main/idf_component.yml` and `dependencies.lock`.
The first build needs registry access to download it; the device/dashboard
needs no internet. Keep the manifest and lock file; generated `managed_components/`
is ignored. No Arduino component or generic Ethernet initialization wrapper.

Open ESP-IDF PowerShell, or activate it as below. All commands run from the
project root; adjust only the SDK path if installed elsewhere:

```powershell
Set-Location 'C:\path\to\syndetis' # replace with your checkout path
$env:PYTHONUTF8 = '1'
. 'C:\esp\v6.1\esp-idf\export.ps1'
idf.py --version
idf.py set-target esp32
idf.py build
```

For a clean build, from the same project root:

```powershell
idf.py fullclean
idf.py build
```

Use an external USB-UART adapter with **3.3 V logic**, adapter TX to board RX0,
adapter RX to board TX0, and common GND. Power the board adequately according
to its markings. Hold IO0 LOW during reset/power-on to enter the ROM loader.
After flashing, release IO0 and reset for normal boot. Manual boot/reset is
needed when the adapter lacks automatic control.

**COM_PORT is a required placeholder**: replace it with the adapter's Windows
port, for example COM7. No flash or erase has been run as part of this work.

```powershell
$SyndetisPort = 'COM_PORT' # REQUIRED: replace with your actual COM number
idf.py -p $SyndetisPort flash
# Release IO0 and reset the board for normal boot.
idf.py -p $SyndetisPort monitor
```

Monitor baud is 115200; exit with Ctrl+]. No erase command is needed. An NVS
initialization error is reported rather than automatically erasing storage.

## OLED interaction

The unchanged splash fills five code-drawn RJ45 contacts at 180/360/540/720/900 ms,
reveals Syndetis at 1050 ms and the subtitle at 1350 ms, then keeps the composition
until the 3000 ms deadline from the first displayed frame. Scheduler/transfer
latency adds a small transition tolerance. Service initialization starts after
the menu opens, so it cannot lengthen the splash.

Splash presses are discarded. All buttons must be released together for 30 ms
before navigation arms. Each press/release is debounced for 30 ms. UP/DOWN act
once on a stable press and do not repeat while held.

Short OK selects/opens/activates **on debounced release**, never on initial press.
Holding OK for **500 ms after the debounced press** goes back one level once,
suppresses the short action, and requires release before another OK action.
The main menu ignores long OK. This uses nonblocking monotonic timing; polling
and OLED transfers add a small timing tolerance. Back has priority if UP/DOWN
coincides with the long event; simultaneous short/navigation events are ignored.

The five main-menu items are **Link & IP**, **Switch Info**, **Device Info**,
**Web Access**, **About**, shown in four scrolling rows. UP/DOWN stop at the ends.
Information screens scroll four text rows with a scrollbar; short or long OK
returns one level. Explicit Back items remain in submenus.

- Link & IP shows actual PHY up/down, negotiated speed/duplex when available,
  Ethernet MAC and `IP: Not configured`. Initialization failures show the failed
  step and error code, not a false link-down result.
- Device Info retains chip/runtime information and OLED/button assignments,
  adding the actual configured PHY/clock assignments. Factory base MAC and
  Ethernet MAC are explicitly distinguished.
- Switch Info shows discovery state, Status/details (including limits), separate
  LLDP/CDP neighbor entries and Back. Select a neighbor to scroll its full fields,
  age and TTL. Text wraps into 20-column lines without horizontal clipping;
  all supported 255-byte names and identifiers remain inspectable. Unknown
  network-address ID formats are shown as full hex with the LLDP subtype.
- About reads firmware 0.3.0 from the same application descriptor as Device Info
  and the status API.
- Web Access OFF: **Enable**, **Back**. ON: **Connection details**, **Disable**,
  **Back**. Details is a scrolling information screen; OK returns to the submenu.
  During an operation Back remains available. An error exposes **Access error**
  details and either **Enable** to retry or **Retry cleanup** if a service remains.

The title bar always reads Syndetis. `EUP` = actual PHY link up, `EDN` = link down,
`E..` = initializing, `ERR` = Ethernet initialization error. These are separate
from `WEB ON`, `WEB ..` (starting), or `WEB ER` (access error). No link is described
as a cable certification/pass result. Navigation refreshes live state every
250 ms; the driver polls physical link every 500 ms.

## Physical Web Access boundary

**Browser access is OFF after every boot/reset, regardless of its prior state.**
No enable flag is persisted. The only enable call comes from the physical
OLED's Web Access -> Enable action. Returning to the main menu leaves access
running. No HTTP listener or hotspot starts during boot.

Enable starts a WPA2-Personal/AES hotspot and the ESP-IDF HTTP server. Only when
both start successfully does the OLED show `WEB ON`. Connect a phone/laptop to
the SSID shown in Connection details, use the password shown there, then open
**http://192.168.4.1/**. SSID is `Syndetis-XXXX`, using the last two bytes of the
stable device SoftAP MAC. The configured prototype password is exactly
**Syndetis** (capital S, then lowercase), defined in `main/access_config.h`.
This intentionally retained prototype password is public: **change it for your
own builds before deployment or distribution**.
WPA2 validation accepts 8–63 characters. Routine logs and the JSON API never
include it; the OLED displays the same configured password with SSID/URL.
Clients with the old password may need to **forget the saved hotspot and reconnect**.
If a phone reports no internet, remain connected to this local hotspot.

Long OK or selecting Back from Web Access never changes access state. Only
selecting Enable or Disable (or Retry cleanup after a stop error) changes services.

The hotspot's DHCP **server** leases management addresses to clients. Ethernet
has no `esp_netif`, IP attachment or DHCP **client**. Its received frames are
checked for passive LLDP/CDP and released without feeding an IP stack. Physical link monitoring therefore
works independently of browser access and addressing, including with WEB OFF.
There is no station interface, bridging, forwarding/NAT or internet dependency.

Disable stops the real HTTP server, closes its active sessions, then stops
Wi-Fi and disconnects hotspot clients. It always attempts both shutdowns even
if one fails. Startup failures roll back any started services. Service flags are
retained on failed shutdown so the UI never falsely claims OFF. Retry cleanup
from the device.
The existing Ethernet driver and OLED are not stopped by any access operation.
Serial errors name the failing API, `esp_err_to_name()` and its hexadecimal code;
the OLED shows one short operation/code plus separate HTTP/hotspot state lines.
Wi-Fi/AP infrastructure is initialized lazily and retained stopped for reuse;
no radio or listener remains active after a successful disable.

The pinned v6.1 default AP netif already supplies 192.168.4.1/24. Firmware retains
that address without calling `esp_netif_set_ip_info()`: that setter requires an
explicitly STOPPED DHCP server, even when the new server is only in INIT state.
Default AP start/stop event handlers start DHCP and restore its restartable INIT
state on stop; no manual DHCP stop disables the next enable cycle.

The server exposes only GET `/` and GET `/api/status`. It has no enable, disable,
configuration or diagnostic API. Every enabled period has **no separate dashboard
login**. Currently only hotspot clients can reach it because Ethernet has no IP.
If future firmware exposes it on Ethernet, reachable LAN users will also be able
to access it during that enabled period. Physical disable must stop the entire
server regardless of interface. No browser authentication or HTTPS is implemented.

## Dashboard and small-module design

HTML/CSS/JavaScript is embedded directly from `main/dashboard.html` in firmware
flash. All assets use local/system resources; no fonts, CDN, external scripts
or internet requests. Layout supports phones and laptops. Displayed fields are
firmware version, uptime and actual Ethernet initialization/link/speed/duplex/MAC.
Ethernet IP is always **Not configured**; 192.168.4.1 is labeled management only.
Switch Info reads the same locked neighbor table as the OLED. It includes
protocol/source, advertised fields, age/TTL and Current/Expired labels. Missing
fields are JSON `null`/Unavailable; an advertised empty string is `(empty)`.
Network-provided text is JSON-escaped and rendered only with `textContent`.
The entire status response is bounded to 16,384 bytes; there is no new listener.

The read-only JSON endpoint is polled about once per second, scheduling the next
request only after the previous one settles. Requests time out; failures mark
existing values stale and say **Tester unreachable**. Successful communication
restores live status. An age check also detects stale data after tab suspension.
There are no fake activity LEDs or diagnostic controls.

`status.c` holds Ethernet/Web status and `discovery.c` holds the shared neighbor
table, each protected by a short critical section. OLED and HTTP copy data before
rendering. `neighbors.c` is the portable parser/lifecycle; a dedicated worker
parses the four-frame queue outside the raw receive callback. `ethernet.c`
keeps the existing driver and short event callbacks. `button_logic.c` holds the
testable debounce/short/long state. `services.c` runs initialization/access changes
on one worker with a nonblocking physical request queue. `dashboard.c` serves
flash content/status. Only the main UI task draws on the OLED. No operation waits
for a cable, an IP lease, a Wi-Fi client or a browser connection.

Software dashboard checks (Node.js required; no npm dependencies):

```powershell
node tests/dashboard.test.cjs
```

Parser/table/button checks compile the actual portable firmware C, with no SDK
or hardware needed. From the project root, using an available host C compiler:

```powershell
New-Item -ItemType Directory -Force tmp/checks | Out-Null
cc -std=c11 -Wall -Wextra -Imain main/neighbors.c main/button_logic.c tests/core_test.c -o tmp/checks/core-tests.exe
./tmp/checks/core-tests.exe
cc -std=c11 -Itests/host_stubs -Imain main/neighbors.c tests/status_json_test.c -o tmp/checks/json-tests.exe
node tests/status_json.test.cjs
cc -std=c11 -Itests/host_stubs -Imain main/neighbors.c tests/ui_test.c -o tmp/checks/ui-tests.exe
./tmp/checks/ui-tests.exe
```

`cc` is a host compiler placeholder, not the ESP cross-compiler. On this Windows
host these checks used portable TinyCC 0.9.27 in ignored `tmp/checks/host-tools/`
(`tcc -Imain ...` instead of `cc`); no system compiler or firmware dependency was
installed. JSON checks compile the actual HTTP serializer with test-only SDK I/O
stubs and verify worst-case size, escaping and null/empty handling.
OLED checks compile the actual UI with test-only I/O stubs and check navigation,
Back without Web Access activation, long-field scrolling and display bounds.

Optional browser integration/layout checks require Playwright plus installed
Chrome: `node tests/dashboard.browser.cjs`. They intercept all requests with
explicit test fixtures; they do not connect to a tester or run diagnostics.

## Passive Switch Info: behavior and supported limits

Discovery runs whenever physical Ethernet monitoring runs, including with WEB OFF,
no IP and no lease. There are **no transmitted discovery queries, scans or
advertisements**. The existing raw `esp_eth_config_t.stack_input` path owns each
SDK receive buffer; it filters destinations, copies eligible frames into a
four-entry queue without waiting and frees **every** buffer, even on rejection,
oversize or a full queue. Parsing occurs on the discovery worker.

The pinned v6.1 ESP32 EMAC implements `ETH_CMD_S_ALL_MULTICAST`. It is enabled
once after driver installation to admit LLDP/CDP multicast destinations. Unicast
filtering remains enabled; **promiscuous reception is not enabled or required**.
The SDK DMA receive code strips the four-byte FCS before the application callback.
Neither Web Access nor discovery replaces/reinitializes the working driver.

Supported input:

- LLDP Ethernet II, EtherType 0x88CC, destinations 01:80:C2:00:00:0E/03/00.
  Chassis, port, TTL must occur once in order, followed by bounded optional TLVs
  and a valid End TLV. Supports system name, port description, management IPv4/
  IPv6, and IEEE 802.1 OUI 00:80:C2 subtype 1 port PVID.
- CDP versions 1/2, destination 01:00:0C:CC:CC:CC, valid 802.3 length, LLC/SNAP
  AA-AA-03 / OUI 00:00:0C / PID 0x2000, and valid checksum (including Cisco's
  odd-final-byte checksum convention). Supports Device ID/name, port ID,
  IPv4 NLPID/IPv6 SNAP address entries, management-address TLV 0x0016, and
  native VLAN TLV 0x000A. A normal CDP Address TLV fallback is labeled
  **Advertised IP (CDP address TLV)**, not asserted to be a management-only address.
- Untagged or **one 802.1Q tag** (TPID 0x8100), up to **1518 received bytes**.
  The framing tag's VLAN is never inferred to be an advertised PVID/native VLAN.
- Full 255-byte identity keys, names and descriptions. Printable ASCII is kept;
  control, NUL and non-ASCII bytes display as `?` with an explicit text note.
  Matching uses full raw identity bytes, not sanitized/truncated display strings.
- Only one supported address is displayed: prefer an explicit management TLV
  over CDP Address TLV; within that class prefer the first IPv4, else first IPv6.
  CDP does not supply an LLDP chassis ID/port-description TLV; these stay
  Unavailable. IPv6 is displayed in uncompressed text form.

Unsupported/limited input is not converted to fake results: stacked tags/QinQ
(including TPID 0x88A8), jumbo frames, LLDP over LLC/SNAP, CDP over Ethernet II,
FDP/EDP, other address families/encodings, text/IDs beyond 255 bytes, LLDP-MED,
DCBX, capabilities/platform/software details and VLAN-name/full VLAN-list TLVs.
Unknown TLVs with valid lengths are skipped; malformed supported fields or
lengths reject the advertisement. Unsupported size/tag/text cases increment
Limited frames. Malformed candidates increment Invalid frames; unrelated
traffic is ignored. This is not a general packet analyzer.

There are **four total records** across LLDP/CDP. Matching uses protocol, source
MAC and complete chassis/device and port identity (plus LLDP subtypes). LLDP/CDP
are never merged. Existing identities update their own slot, replacing optional
fields rather than inheriting old values. A new identity uses the first free or
expired slot; otherwise it is rejected and Capacity drops increases. Queue and
size limits are also visible under Status/details and in the browser. Slot
selection/order is deterministic. Expired records may be reused; no live record
is unpredictably evicted.

States are Link down, Waiting for advertisement, No advertisement received after
**30 seconds**, Received/current, and Expired. Listening continues indefinitely
after 30 seconds: a later advertisement still updates immediately. Initialization
errors are shown as unavailable/error rather than a fabricated switch result.
Each record expires from its **receive timestamp + advertised TTL**; retained
expired details are explicitly marked Expired, not current. TTL zero immediately
removes matching identity data and does not create a new neighbor.

On link-down events, records are cleared synchronously and the link-session
generation changes. Every queued frame is stamped with its receive session;
that generation is checked again after parsing, preventing old queued data from
reappearing after reconnect. OLED/browser update at their normal refresh/poll
intervals. Link down also clears discovery counters for the new session.

An advertised management address belongs to the neighbor, **not Syndetis**.
PVID/native VLAN is advertised port information, **not a complete configured VLAN
list**; value 0 means unspecified. No advertisement is never labeled No switch,
Cable failed or Cable passed.

## Milestone 3 hardware acceptance checklist

These checks are pending on the actual device. Build/script checks do not prove
PHY negotiation, RF operation, GPIO startup, physical timing or memory stability.

1. [ ] Short OK selects on release. Hold 500 ms goes back without activating the
   selected item first; at the main menu it does nothing. Check Enable/Disable
   specifically to ensure a hold cannot briefly start/stop services.
2. [ ] Holding OK produces only one Back until released, including through a
   submenu and information page. UP/DOWN debounce and non-repeat remain correct.
3. [ ] Long OK/Back from Web Access leaves its enabled/disabled state unchanged.
   Only short-select Enable/Disable changes it; WEB ON remains visible everywhere.
4. [ ] Forget saved hotspot credentials if needed; reconnect with **Syndetis**.
   Verify displayed password matches, stable SSID, default AP address, and DHCP
   leases. Test real disable with active browser/API sessions, then re-enable.
5. [ ] On known LLDP/CDP-capable switch ports, compare advertised name, chassis/
   device identity, port, address, PVID/native VLAN and TTL against configuration.
   Check long text by scrolling and unavailable versus empty fields.
6. [ ] Discovery works with WEB OFF and no Ethernet IP/DHCP lease. If capture is
   available, confirm no LLDP/CDP advertisements/queries originate from Syndetis.
7. [ ] With Web Access enabled, OLED/browser show the same separate protocol
   records, ages, expiry and limits. No external resources or executable markup.
8. [ ] Unplug clears identities. Reconnect to a different port/switch: no old
   queued identity reappears. With link held up, stop advertisements and verify
   advertised TTL expiry; check withdrawal if the switch sends TTL zero.
9. [ ] An advertisement-free port waits, then says No advertisement received at
   30 seconds. Enable advertisements later and verify listening never stopped.
10. [ ] Recheck actual link/speed/duplex, 3-second splash/input suppression,
    five-item/four-row menu scrolling, page return and GPIO5 OK-held cold boot.
    Reset while WEB ON must boot OFF with no hotspot/listener. Repeat web cycles
    and monitor free/minimum heap and serial errors; discovery stays independent.

Stop at Milestone 3 until this device checklist passes. The remaining roadmap
and the requirement that DHCP failures never hide physical/L2 state are in
`ROADMAP.md`.

## SDK references

- [ESP-IDF v6.1 Ethernet](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-reference/network/esp_eth.html)
- [ESP-IDF v6.1 Wi-Fi](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-reference/network/esp_wifi.html)
- [ESP-IDF v6.1 HTTP server](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-reference/protocols/esp_http_server.html)
- [Cisco CDP v2 protocol/TLV guide](https://www.cisco.com/c/en/us/td/docs/routers/ios-xe/system-management/system-management/m_nm-cdp-discover-xe.html)
- [lldpd LLDP decoder reference](https://github.com/lldpd/lldpd/blob/master/src/daemon/protocols/lldp.c)
- [lldpd Cisco checksum convention reference](https://github.com/lldpd/lldpd/blob/master/src/daemon/frame.c)

## Firmware 0.3.0 checkpoint and repository hygiene

The annotated `v0.3.0` tag identifies this firmware checkpoint. Generated builds,
local `sdkconfig`/`sdkconfig.old`, downloaded components, editor caches, temporary
tools, field logs, captures and screenshots are excluded from version control.
Source, embedded dashboard assets, `sdkconfig.defaults`, `dependencies.lock` and
the component manifest are retained; custom partition tables should also be committed.
Test addresses and identities in `tests/` are synthetic fixtures. The documented
hotspot address is a firmware management default, not a field network address.
Review staged content before each upload; ignore rules do not sanitize file contents.
