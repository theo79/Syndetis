# Syndetis validation

## Milestone 3 — firmware 0.3.0

Date: 2026-10-19. Implemented passive LLDP/CDP Switch Info, password `Syndetis`
and global debounced short/release versus 500 ms hold/Back behavior. The sections
below this milestone are historical. The original implementation session did
not flash or erase hardware. Owner-reported evidence added for the 0.3.0
checkpoint: LLDP reception tested with a Siemens S7-1200; CDP reception tested
with a Cisco device. No deployment organization or field network addresses are
recorded. Reception does not establish full field accuracy or lifecycle acceptance.

### Build and software checks

Clean build: **PASS**, `idf.py fullclean` then `idf.py build`, exit **0**.
SDK **ESP-IDF v6.1**, header/dependency version **6.1.0**, GNU **15.2.0**
(`esp-15.2.0_20251204`), Python **3.11.15**, esptool **5.4.0**, ESP32 target.
Official LAN87xx remains pinned to **1.1.0**. Firmware version is **0.3.0**.
Application image **0xD6800 / 878,592 bytes**, leaving **0x29800 / 169,984
bytes (16%)** in the 1 MiB factory application partition. Bootloader image
**0x6640 / 26,176 bytes**. Both size checks passed. Five existing SDK CMake
private-include dependency warnings between `esp_wifi` and `wpa_supplicant`
remain; no application compiler warnings/errors.

The following checks passed with synthetic data and stubbed hardware I/O:

- `tests/core_test.c` compiles the actual portable parser, table and button
  logic with host TinyCC **0.9.27**. Valid LLDP/CDP fields and missing optional
  fields; IPv4/IPv6; explicit versus fallback CDP addresses; LLDP mandatory
  ordering; CDP LLC/SNAP and checksum vectors (including Cisco odd-byte
  convention); truncated, malformed, oversized and tagged frames; long raw
  identity/text bounds; empty versus absent values; 10,000 bounded mutations;
  TTL expiry and zero-TTL withdrawal for both protocols; capacity/drop/reuse;
  late advertisements; link-session clearing and rejection of old-session or
  already-expired queued data. OK release, hold threshold, one-shot Back,
  suppressed short action, bounce and splash/reset release gating also pass.
- `tests/ui_test.c` compiles the actual OLED UI with test-only I/O stubs.
  Back changes one parent level and never calls Enable/Disable; main-menu Back
  does nothing. Five-item viewport, neighbor/detail navigation, full supported
  long-string scrolling and all drawn coordinates within 128 x 64 pass.
- `tests/status_json_test.c` and `tests/status_json.test.cjs` exercise the actual
  HTTP serializer: four worst-case escaped records fit the 16 KiB response;
  quotes, backslashes, controls and network markup round-trip as text, and
  absent/null stays distinct from present/empty.
- `node tests/dashboard.test.cjs`: serialized polling, text-only discovery
  rendering, clearing, age, stale/recovery, malformed JSON, timeout and
  Ethernet-error states pass.
- `node tests/dashboard.browser.cjs`: headless installed Chrome, 375 x 812
  and 1280 x 900. Long names wrap without horizontal overflow; hostile markup
  stays text; current/expired, unavailable/empty, link clearing and recovery
  pass. No extra/external requests or JavaScript errors. Screenshots in ignored
  `tmp/screenshots/` are synthetic fixtures, not device observations.

Host test stubs/tools are not firmware components. Commands are in README.

### SDK/lifecycle implementation review

The installed pinned SDK supports `esp_eth_update_input_path` and
`ETH_CMD_S_ALL_MULTICAST` for the existing ESP32 EMAC driver. All-multicast
reception is enabled; promiscuous mode is unnecessary. The SDK DMA path removes
the four-byte FCS before delivery. Existing driver/pins/physical link monitoring
remain in place; discovery adds no Ethernet netif, IP, DHCP client or transmit
call. The callback filters/copies into four fixed queue slots without waiting;
the worker parses outside the callback. Every callback path frees its SDK RX
buffer. Records and queue storage are bounded. Link changes clear records and
advance a session generation; the worker rechecks it after parsing.

OLED and HTTP read the same table. Four LLDP/CDP records stay separate by
protocol/source/raw identity/port. Capacity, queue and parsing limits are
reported. A 30-second observation interval does not stop listening. Expired
identities are marked Expired until reused; withdrawal removes matching data.
Link down clears identity immediately. Missing data is never inferred.

Single 802.1Q tagging is supported. Stacked tags/QinQ, jumbo frames, alternate
LLDP LLC/CDP Ethernet-II framing, other discovery protocols and address families
beyond IPv4/IPv6 are unsupported. LLDP-MED/DCBX, capabilities, software/platform
and full VLAN lists are not extracted. README details these limits, field
lengths and text sanitization. Advertised VLAN is only PVID/native VLAN;
advertised addresses belong to the neighbor, not to Syndetis.

Web Access remains physical-only and OFF after boot; Back never changes it.
The previous default-AP/DHCP lifecycle correction is retained: no IP setter or
manual DHCP override, default AP address 192.168.4.1, default handlers manage
DHCP start/stop. HTTP and Wi-Fi cleanup/reuse and operation/name/hex error logs
remain unchanged. Repeated on-device enable/disable is still a hardware check.

### Hardware checks — reception tested; remaining acceptance pending

- [ ] Short OK selects only on release; stable 500 ms hold goes back once,
  never activates first, and requires release before another action.
- [ ] Back from Web Access leaves its enabled/disabled state unchanged.
- [ ] Forget saved credentials if necessary; reconnect using exactly `Syndetis`.
- [ ] Known LLDP/CDP switch identities/ports/advertised fields match configuration.
- [ ] Discovery works with Web Access OFF and no Ethernet IP; OLED/browser agree
  when Web Access is enabled.
- [ ] Unplug immediately clears identity; another port/switch cannot show the
  previous session as current, including queued-frame races.
- [ ] No-advertisement ports show Waiting then No advertisement after 30 seconds;
  a later advertisement is accepted. Check TTL expiry and withdrawal.
- [ ] Packet capture confirms no discovery advertisements/queries or Ethernet
  DHCP traffic from Syndetis; multicast reception works on the physical EMAC.
- [ ] Existing link speed/duplex/status, wiring, SH1106 rendering, three-second
  splash, navigation, physical Web Access, repeated enable/disable and boot-OFF
  behavior remain correct. Enabled hotspot supplies management DHCP leases.

Changed implementation: root/main CMake files; `main/access_config.h`,
`board.h`, `buttons.c/.h`, `button_logic.c/.h`, `neighbors.c/.h`,
`discovery.c/.h`, `ethernet.c`, `ui.c`, `display.c`, `dashboard.c/.html`.
Validation additions/updates are in `tests/`; README, VALIDATION and ROADMAP
describe the delivered milestone and pending hardware gate.

## Web Access startup correction — firmware 0.2.0

Hardware report: Web Access failed with `WiFi initialization: 0x5007`, HTTP
stopped and hotspot stopped. This section records the correction, not a hardware
success. The initial-build results below are historical.

### Exact root cause and pinned-SDK verification

The failing API was **`esp_netif_set_ip_info(ap_netif, &ip)`** in `prepare_wifi`,
after AP netif creation/attachment/default-handler registration and **before
`esp_wifi_init()`**. The outer WiFi initialization label obscured that API.

The installed ESP-IDF v6.1 / lock version 6.1.0 source was inspected directly:

- `components/esp_netif/include/esp_netif_types.h`: 0x5007 is
  `ESP_ERR_ESP_NETIF_DHCP_NOT_STOPPED`; DHCP INIT is zero, STARTED and STOPPED
  are distinct states.
- `components/esp_netif/lwip/esp_netif_lwip.c`, `esp_netif_new_api`: the AP
  object is zero-initialized, so a newly created DHCP server is in **INIT**,
  not STARTED. The original setter ran before Wi-Fi startup; the error does
  **not** demonstrate that it was actively serving leases.
- The same file, `esp_netif_set_ip_info_api`: with `ESP_NETIF_DHCP_SERVER`,
  **any state other than STOPPED**, including INIT, returns 0x5007.
- `components/esp_netif/esp_netif_defaults.c`, `_g_esp_netif_soft_ap_ip`,
  and `include/esp_netif_defaults.h`: the selected default AP configuration
  already supplies **192.168.4.1**, gateway **192.168.4.1**, mask
  **255.255.255.0**, DHCP server and AUTOUP flags.

The smallest functional fix removes the redundant IP setter and retains that
default. No manual DHCP stop/start, new interface, or Ethernet DHCP client was
added. Firmware version and dependency pins remain unchanged.

### Lifecycle and failure-path review

- The registered default `WIFI_EVENT_AP_START` handler in
  `components/esp_wifi/src/wifi_default.c` calls `wifi_start`, then
  `esp_netif_action_start`. `esp_netif_start_api` brings the AP netif up and
  starts its DHCP server from INIT.
- `WIFI_EVENT_AP_STOP` calls `esp_netif_action_stop`; `esp_netif_stop_api`
  stops DHCP and returns its normal state to INIT. The application never
  manually puts DHCP into STOPPED, which would suppress automatic restart.
- A successful disable stops HTTP and Wi-Fi but retains the initialized Wi-Fi
  driver, AP netif and one set of handlers. Subsequent enable reuses them and
  starts Wi-Fi/HTTP again; ordered AP stop/start events manage DHCP each cycle.
- If HTTP start/route registration fails after AP start, cleanup stops any
  retained HTTP handle and then Wi-Fi. Both cleanup operations are attempted
  independently. Each handle/running flag is cleared only on successful stop;
  failed shutdown remains an error with Retry cleanup. These paths are preserved.
- All Web Access startup/stop failures now log operation names,
  `esp_err_to_name()` and hexadecimal codes, including individual HTTP API
  failures. Passwords are not logged. OLED keeps a short operation/code plus
  separate HTTP/hotspot state lines; cleanup failures take display priority.
- Physical menu remains the only requester of enable/disable; boot state is
  still OFF. Ethernet monitoring, board wiring, display/splash, buttons and
  navigation were not edited. No Ethernet netif/client was added.

This is an implementation and SDK-source review of repeated-cycle behavior,
not an executed on-device cycle or fault-injection test.

### Clean build — PASS

`idf.py --version` reported **ESP-IDF v6.1**. From the project root, after SDK
activation, **`idf.py fullclean` followed by `idf.py build` succeeded**, final
exit code **0**, `Project build complete`. GNU **15.2.0**, Python **3.11.15**,
esptool **5.4.0**, ESP32 target; LAN87xx **1.1.0** remains pinned.

Application image: **0xD3C10 / 867,344 bytes**. Application partition free:
**0x2C3F0 / 181,232 bytes (17%)**. Image and bootloader size checks passed.
No application compiler warnings/errors; five existing SDK CMake private-include
dependency warnings between `esp_wifi` and `wpa_supplicant` remain. Build logs
are in `build/log/`.

Changed files: `main/services.c`, `main/dashboard.c`, `README.md`, `VALIDATION.md`.
No hardware flash, erase or publication was performed. Hardware verification is
still required: physical Enable obtains a management lease at 192.168.4.x and
opens the dashboard; Disable closes active access; repeated re-enable works;
reset returns OFF; Ethernet remains operational with Web Access OFF. Also check
that any future failure prints the precise API/name/code and a readable OLED error.

## Milestone 2 initial build — firmware 0.2.0 (historical)

Date: 2026-10-19. Scope: Ethernet physical link monitoring without Ethernet IP,
physical-only Web Access, WPA2 hotspot and flash-served read-only dashboard.

### Clean build — PASS

- Exact SDK: **ESP-IDF v6.1**, header/lock version **6.1.0**, confirmed by
  `idf.py --version`; installed at `C:\esp\v6.1\esp-idf`.
- Compiler: Xtensa GNU **15.2.0**, tool package `esp-15.2.0_20251204`.
- SDK Python **3.11.15**; esptool **5.4.0**. Target **esp32**, flash **4 MiB**,
  FreeRTOS tick **1000 Hz**. Project/app descriptor version **0.2.0**.
- Official managed PHY component: **espressif/lan87xx 1.1.0**, pinned in
  `main/idf_component.yml` and `dependencies.lock`. ESP-IDF 6.1 no longer bundles
  this PHY-specific driver. No generic Ethernet initialization wrapper or Arduino.
- Final commands, after ESP-IDF activation, from the project root:

  ```powershell
  idf.py fullclean
  idf.py build
  ```

- Both final commands succeeded; build exit code **0**, `Project build complete`.
  All eight application C files, embedded HTML, PHY driver and bootloader built.
- Application `build/Syndetis.bin`: **0xD39D0 / 866,768 bytes**.
  Default application partition: **0x100000 / 1,048,576 bytes**;
  **0x2C630 / 181,808 bytes (17%) free**. Partition size checks passed.
- Bootloader: **0x6640 / 26,176 bytes**; partition table: **3,072 bytes**.
- No application compiler warnings/errors in the final build logs. ESP-IDF's
  CMake component validation emitted five private-include/dependency warnings
  between its own `esp_wifi` and `wpa_supplicant` components. SDK files were not
  modified. Logs are under `build/log/` (ignored generated output).
- The first attempt failed because the migrated PHY header/component was missing
  and GCC detected a potentially truncated SSID copy. Both were fixed, then the
  complete clean build above was rerun successfully. A stale RMII Kconfig symbol
  was also removed; external input clock selection is explicit in the MAC config.

### Software checks — PASS within the limits below

- `node tests/dashboard.test.cjs`: executes the actual embedded JavaScript in
  a DOM/fetch/timer harness. Passed live field rendering, serialized requests,
  disconnect/stale retained values, automatic recovery, tab-age stale detection,
  malformed JSON rejection, timeout and explicit Ethernet initialization errors.
- `node tests/dashboard.browser.cjs`: Playwright with installed Chrome, at
  **375 x 812** and **1280 x 900**. Passed no horizontal overflow, link/speed/duplex
  changes, stale display and recovery, one request at a time, no external resource
  requests and no page JavaScript errors. Phone/live and laptop/stale screenshots
  were visually inspected. These tests use explicit intercepted fixtures, not
  measured network results or a real device. They start no device HTTP listener.
- Source review: unchanged SH1106 configuration/renderer, button module and splash
  drawing/timing. New Ethernet pins agree with the manufacturer schematic mirrored
  in README; PHY address is 1, GPIO16 enables the oscillator, clock input is GPIO0,
  and the PHY reset GPIO is explicitly -1 so the SDK default cannot drive GPIO5.
- Physical-access boundary review: only OLED input calls the Web Access request
  function. Boot starts Ethernet monitoring but does not initialize/start Wi-Fi
  or HTTP. The only HTTP routes are read-only GET `/` and GET `/api/status`.
  No enabled state is persisted. HTTP is really started/stopped rather than gated
  behind a page flag. Disable attempts HTTP and AP stop independently; error state
  preserves failed shutdown flags and offers Retry cleanup. A partial startup
  rolls back services that already started.
- Concurrency review: one brief critical-section status snapshot is copied by
  OLED/HTTP. Ethernet callbacks only copy cached negotiated state. Service work is
  queued to a worker; OLED drawing remains on the main task. No cable/IP/client
  or browser-connection wait is used.
- Ethernet has no netif/IP attachment; its receive callback frees frames without
  delivering them to an IP stack. Only the AP netif exists when prepared. AP-only
  mode is explicit, and hotspot DHCP server support is enabled. Generated config
  has IPv4 forwarding and IPv6 disabled. No station interface, NAT, discovery,
  ping, TCP probe, SD, OTA or Ethernet DHCP client is started.
- Password is defined once in `main/access_config.h`, displayed only through the
  physical details screen while enabled, and excluded from routine logs/status
  JSON. Read-only dashboard consistently labels Ethernet IP Not configured.

Builds ran outside the restricted process environment because the installed
Windows toolchain cannot execute correctly within it. No SDK installation or
system configuration was changed.

These are compilation, browser-fixture and source-review results. They do not
prove RF/PHY operation, actual shutdown behavior, debounce timing or heap stability
on the board. Startup/shutdown failure handling was reviewed in source; it has
not been fault-injected on hardware.

### Milestone 2 hardware checklist — ALL PENDING

1. [ ] Boot preserves splash/menu; WEB OFF and no hotspot.
2. [ ] Ethernet link monitoring works with browser OFF; negotiated values clear
   when down, and initialization errors appear explicitly.
3. [ ] Physical Enable starts WPA2 hotspot and offline dashboard. Verify readable
   SSID/password/URL in scrolling Connection details on OLED.
4. [ ] OLED/browser agree on unplug/replug, speed, duplex and actual Ethernet MAC;
   Ethernet IP stays Not configured.
5. [ ] Main-menu navigation leaves access enabled; WEB ON persists without
   colliding with the Ethernet indicator.
6. [ ] Disable with dashboard/API sessions open: HTTP connections close, hotspot
   disappears, clients disconnect, requests fail and cached dashboard becomes
   Tester unreachable/stale. Ethernet/OLED continue operating.
7. [ ] Re-enable without reboot and recover live browser data.
8. [ ] Reset while enabled: returns OFF, no hotspot or listener until physical Enable.
9. [ ] Repeat enable/disable at least 20 times; monitor free/minimum heap and serial
   errors for crashes/leaks/failing starts. Check cleanup/error retry where feasible.
10. [ ] Existing UP/DOWN/OK, debounce/hold behavior, scrolling/page return, splash
    duration and splash input suppression remain correct. Test cold boot/reset
    with GPIO5 OK held LOW and released.

Full Windows build, COM-port flash/monitor instructions, wiring and access-boundary
notes are in README. Stop here until these device checks pass.

## Milestone 1 historical record — 0.1.0

The previous milestone clean-built with ESP-IDF v6.1 / GNU 15.2.0 on 2026-10-19.
Application size was **160,096 bytes**; OLED/buttons/splash/navigation were then
implemented without networking. Its hardware testing has since **passed as
reported by the owner** in the Milestone 2 request. That report does not imply
Milestone 2 hardware acceptance.

No hardware was flashed, erased, monitored or operated during this implementation.
No changes were published. No later networking milestone was implemented.
