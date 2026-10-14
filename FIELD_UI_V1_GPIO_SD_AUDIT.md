# Field UI v1 and WT32-ETH01 GPIO / SD audit

Software implementation and validation: 12 October 2026. No commit, push, flash, OTA, new GPIO configuration, SD driver, or hardware modification was performed. Firmware version remains `0.7.0-dev`. Diagnostic engines, timing, transport, Web UI, Ethernet configuration, partitions, sdkconfig, dependencies, and build patches are unchanged.

## 1. Architecture found before implementation

The existing display driver is SH1106, 128 x 64 visible pixels, 132-column controller RAM with a two-column offset. It uses I2C0 at 100 kHz, address 0x3C, SDA32/SCL33, with no wired reset. The fixed 5 x 7 font occupies six columns per character and eight pixels per row: eight rows, 21 characters per row, no horizontal scroll. Lowercase is displayed as uppercase. These display characteristics are unchanged.

`ui_menu.c` owns screen state, selections and rendering in a 4096-byte task. Buttons enqueue into a 12-event queue without blocking. The task checks at 200 ms intervals and redraws only changed rows. OTA receiving/success overrides navigation. OLED failure disables display availability and prevents an invisible firmware-unlock confirmation.

Original home menu: AutoTest, Discover, Switch Info, Network Tools, Industrial, System. Five menu rows were visible, with scrolling for the sixth item. Home did not expose live link/IP. Network Tools was only a Coming soon screen.

Original interaction paths:

| Feature | Original flow / controls |
| --- | --- |
| AutoTest | Ready -> OK starts -> running -> seven result pages; UP/DOWN pages; OK returns home |
| Discover | Ready -> OK starts -> scan -> summary -> OK opens device list -> OK opens details; UP/DOWN on details changes device; OK returns list |
| Switch Info | Home -> five passive LLDP/CDP pages; UP/DOWN wraps pages; OK returns home |
| Industrial | PROFINET / Back -> Scan / Results / Back -> running -> results -> three pages per device; OK during scan requests existing cancel |
| System | Status / About / Firmware Update / Back |
| Firmware | Second explicit OK authorizes existing five-minute Web OTA window; navigation locked during receiving/success |
| SNMP | Existing asynchronous runtime accessible through Web only; no local screen |

Back was inconsistent: some ready pages used UP/DOWN to leave, details used OK, menus had Back rows, and results sometimes left on any non-OK event. There were no button combinations, long-press actions, auto-repeat, or menu idle timeout. Selected discovery hosts were read-only; actions could not be chained to that target.

## 2. Implemented Field UI map

```text
Home: live link / IPv4 and scrolling menu
  Quick Test -> Ready -> existing AutoTest -> Results -> Ready to repeat
  Discover -> Ready -> existing scan -> Results
               Results: Devices / Scan again / Back
               Devices -> selected host actions
                           SNMP -> Ready -> existing runtime -> paged result
                           Details -> selected host details
                           Back -> Devices
  Network Tools
      Switch Info -> existing passive connected-neighbor pages
      SNMP - select host -> existing device list, or Discover if no results
      Back
  Industrial -> existing PROFINET Scan / Results / Back
  System -> Status / About / Firmware Update / Back
```

Home reserves rows 0-2 for title/link/IP, row 3 as spacing, rows 4-6 for three menu items, and row 7 for controls. The menu scrolls to Industrial/System and wraps at the ends. Existing result data remains accessible without making the home screen dense.

Switch Info intentionally stays a connected-neighbor action. The current engine listens passively for LLDP/CDP; it does not query an arbitrary selected IP. Putting it in a host action menu would imply unsupported target behavior. Ping, Continuous Ping, Port Check, DNS Lookup and Save are omitted until implemented. Network Tools is now an extensible section containing only real existing capabilities.

The action lifecycle separates entry from execution. Selecting SNMP opens a ready screen; another short OK submits the existing runtime request. Result OK returns to Ready before repeating, never launches a request directly. Quick Test and Discover Scan again follow the same confirmation boundary. Future Save/Repeat/Back choices can attach to result screens without changing engine interfaces.

## 3. Old -> new menu mapping

| Old path | New path |
| --- | --- |
| Home / AutoTest | Home / Quick Test; all seven result pages retained |
| Home / Discover / device details | Home / Discover / Devices / host / Details |
| Home / Switch Info | Home / Network Tools / Switch Info |
| Home / Network Tools / Coming soon | Home / Network Tools / Switch Info or SNMP - select host |
| Web / discovered host / SNMP | Also local: Discover / Devices / host / SNMP; Web unchanged |
| Home / Industrial / PROFINET | Same path and engine actions |
| Home / System / Status | Same path; live essential status also on home |
| Home / System / About | Same path and unchanged firmware version |
| Home / System / Firmware Update | Same path and existing second-OK authorization |

No working local feature is removed. UP/DOWN device cycling is replaced by explicit list selection, so host actions and Details share one captured target.

## 4. Button model

| Control | GPIO | Electrical setup | Field UI behavior |
| --- | ---: | --- | --- |
| UP | 39 | Input only, active low; external 10k pull-up to 3.3 V | Previous item/page, one event per press |
| DOWN | 35 | Input only, active low; external 10k pull-up to 3.3 V | Next item/page, one event per press |
| OK | 36 | Input only, active low; external 10k pull-up to 3.3 V | Short press selects; event emitted on debounced release |
| Back gesture | Same OK36 | No added pin | Hold OK for 450 ms -> one logical Back event; short OK suppressed |

Polling remains 10 ms; debounce remains 30 ms stable candidate time. GPIO35/36/39 have no internal pulls; firmware disables both pull directions. None is a boot strap. A button held at startup must be released before its first event. UP/DOWN have no repeat, and there are no chords. Tick subtraction handles wraparound.

The small UX refinement reduces the original 700 ms threshold to 450 ms. HOLD OK: BACK appears on System, Network Tools, Industrial, PROFINET menus, host actions, SNMP ready/results, Quick Test ready, valid Discover ready, discovery results/current nonempty device lists, and completed PROFINET results with devices. It is omitted on Home, Status/About, Switch Info, host Details, active scans/tests, Quick Test results, PROFINET device details/empty results, empty or changed discovery lists, invalid Discover ready, firmware confirmation with its existing cancel hint, and firmware update states. Short OK already exits or cancels those simple views; Quick Test results retain a page/repeat hint. SNMP details are pages within the result screen and retain the Back hint. OTA receiving/success has no navigation hint.

Back returns one level, preserving the relevant menu selection. Home Back does nothing. While Quick Test, discovery or SNMP is active, leaving its screen does not cancel or alter that engine; bounded workers continue as before. PROFINET retains its existing cancel-on-exit mechanism. OTA receiving/success retains exclusive navigation priority. Legacy short-OK exits and ready-page UP/DOWN exits remain where useful. No navigation routes or electrical configuration change in the refinement.

A future physical UP/DOWN/OK/BACK layout can deliver the already defined `UI_BUTTON_BACK` event. No fourth button is initialized and no Back GPIO macro is assigned.

## 5. Exact eight-row OLED examples

These are rendered text examples; the OLED font displays them in uppercase. Blank lines are actual unused rows.

Home with known speed/IP:

```text
SYNDETIS
LINK 100M
IP 192.168.1.94

> QUICK TEST
  DISCOVER
  NETWORK TOOLS
UP/DOWN  OK: SELECT
```

Home without a cable:

```text
SYNDETIS
LINK DOWN
CHECK CABLE

> QUICK TEST
  DISCOVER
  NETWORK TOOLS
UP/DOWN  OK: SELECT
```

Unknown speed is honestly shown as LINK UP. Linked without IPv4 shows IP WAITING FOR DHCP; no blocking inspection is added to UI. Known speed becomes available through the existing network-status inspection performed by diagnostic workers.

Selected host:

```text
HOST ACTIONS
192.168.1.20
SIEMENS
> SNMP
  DETAILS
  BACK

HOLD OK: BACK
```

SNMP ready:

```text
SNMP
192.168.1.20

READY

OK: RUN

HOLD OK: BACK
```

SNMP successful name page:

```text
SNMP 1/5
192.168.1.20
NAME
PLC-01


UP/DOWN OK: REPEAT
HOLD OK: BACK
```

Other SNMP pages: Description (up to three text rows), Location, Contact, Uptime. Remote control/non-ASCII bytes are replaced with spaces. Timeout, busy, unavailable, network-changed, stale-result and error states are explicit. Long text is bounded to screen width; Web remains available for extended fields. A changed host shows HOST CHANGED: RESCAN; an invalid/replaced request shows RESULT EXPIRED.

Discovery result:

```text
DISCOVER

FOUND: 3
TIME: 4.2S
> DEVICES
  SCAN AGAIN
  BACK
HOLD OK: BACK
```

The list records its opening scan identifier. A refresh while it is open shows RESULTS CHANGED and blocks selecting refreshed row identities until returning to Discover. Selected-host views additionally validate IP, MAC presence/value, scan identifier, network revision and physical epoch. SNMP uses the existing runtime's independent identity/worker guards; protocol behavior is unchanged.

## 6. GPIO allocation and physical resource audit

Evidence: repository `board_pins.h`, `app_main.c`, OLED/buttons implementation; Wireless-Tag manufacturer drawing titled WT32-ETH01 v1.4 schematic (drawing date 2022-01-04), and manufacturer datasheet with v1.4 interface revision (2023-01-30), mirrored at [schematic](https://github.com/egnor/wt32-eth01/blob/main/WT32-ETH01_V1.4.schematic.pdf) and [datasheet](https://github.com/egnor/wt32-eth01/blob/main/WT32-ETH01-datasheet-v1.4-en.pdf). The datasheet cover still says v1.3; its revision table records v1.4. These are manufacturer documents hosted by a third-party archive, not measurements of the user's unit.

| GPIO/resource | Current allocation / restriction | Audit decision |
| --- | --- | --- |
| 0 | RMII external 50 MHz reference clock; boot-mode strap; programming header | Reserved for Ethernet/boot |
| 1 / 3 | UART0 TX/RX, programming and console | Preserve programming and logs |
| 2 | Exposed spare; boot-mode strap | Avoid Back/CD with default pull-up: high can prevent UART download when GPIO0 is low |
| 4 | Exposed; reserved SD MISO | Retain for SD; not a classic ESP32 boot strap |
| 5 | Exposed RXD2 label; onboard LED3 from 3.3 V through LED and 1k to GPIO5; SDIO-slave timing strap | Back candidate, with qualifications below |
| 6-11 | Module flash bus | Unavailable for peripherals |
| 12 | Exposed; MTDI flash-voltage strap, default low | Do not casually add pull-up; high at reset can select 1.8 V for 3.3 V flash |
| 13 | Module pad connected to LAN8720 RX_ER/PHYAD0 in schematic | Reserve for PHY; not a safe spare, even if current RMII driver does not configure RX_ER |
| 14 | Exposed; reserved SD MOSI; JTAG alternate | Retain for SD, not JTAG simultaneously |
| 15 | Exposed; reserved SD CS; MTDO strap | Retain; idle-high CS pull-up agrees with normal strap |
| 16 | Oscillator enable; hardware 10k pull-down | Firmware drives high before EMAC init; not a free PHY-reset pin |
| 17 | Exposed TXD2 label; reserved SD SCK; onboard LED4 + 1k load | Retain; matrix-routed SPI clock, LED load must be validated |
| 18 | MDIO, LAN8720 | Reserved |
| 19 | RMII TXD0 | Reserved |
| 21 | RMII TX_EN | Reserved |
| 22 | RMII TXD1 | Reserved |
| 23 | MDC | Reserved |
| 25 | RMII RXD0 | Reserved |
| 26 | RMII RXD1 | Reserved |
| 27 | RMII CRS_DV | Reserved |
| 32 | OLED SDA, I2C0 | Preserve |
| 33 | OLED SCL, I2C0 | Preserve |
| 34 | Input-only module pad, not exposed on normal board header | Not a header Back candidate |
| 35 | DOWN; input only, no internal pulls | Preserve external 10k pull-up |
| 36 | OK; input only, no internal pulls | Preserve external 10k pull-up |
| 37 / 38 | Input-only module pads, not normal board header pins | Not spare exposed header resources |
| 39 | UP; input only, no internal pulls | Preserve external 10k pull-up |
| 20 / 24 / 28-31 | No corresponding usable GPIOs on classic ESP32 | Unavailable |
| EN | Chip enable/reset, not a GPIO | Preserve |
| LINK header / RJ45 LEDs | PHY LED2/nINTSEL and PHY LED signals | Not spare ESP32 GPIOs |
| Power LED1 | Power rail, no firmware GPIO | No reusable GPIO |
| PHY reset | nRST RC/pull-up circuit, `reset_gpio_num=-1` | No firmware reset allocation |

The application's only additional peripheral pin assignments are OLED and buttons. SD macros are reservations only: no SPI bus/SD initialization. UART2 labels describe alternate use, not an initialized UART2 peripheral in this firmware. Default SPI IO_MUX pin sets collide with Ethernet; use a free general-purpose SPI host through the GPIO matrix instead. Do not reuse flash SPI.

ESP32 input-only pins and straps are confirmed by the [Espressif datasheet](https://documentation.espressif.com/esp32_datasheet_en.html), sections 2.3 and 3. GPIO5 and GPIO15 affect SDIO **slave** timing; this is distinct from an SPI **host** driving a microSD card. GPIO15 additionally controls boot UART printing. No eFuse changes are needed or proposed.

## 7. Extra Back + SPI microSD feasibility

**Outcome A for the audited stock v1.4 design: existing OLED/buttons, one dedicated Back button, and SPI microSD can coexist electrically, subject to physical revision and bench validation. This is a schematic-level conclusion, not a hardware-tested approval.**

Proposed SD signal candidates preserve the existing reservations:

| SD signal | Candidate GPIO | Electrical considerations |
| --- | ---: | --- |
| SCK / CLK | 17 | Output capable; SPI matrix routing; LED4/1k adds load, start conservatively and check edge quality |
| MOSI / CMD / DI | 14 | Output capable; pull-up on card CMD; no JTAG use simultaneously |
| MISO / DAT0 / DO | 4 | Input capable; pull-up on card DAT0 |
| CS / DAT3 | 15 | Output capable; external 10k to 3.3 V, held deselected at reset; preserves default MTDO-high strap |
| Card detect | Omit | No additional GPIO necessary; avoid using remaining boot-sensitive resources casually |
| Back | GPIO5 candidate only | Does not overlap any SD or OLED/button signal; no code assignment made |

GPIO5 is the practical remaining header candidate: active-low switch to ground with a real 10k pull-up to 3.3 V. The LED path is not a substitute for that resistor. Pressing it also sinks the small LED3 current through its existing 1k resistor and lights that indicator. Holding Back at reset changes GPIO5's SDIO-slave edge strap, which is unused by this firmware; it does not select boot mode or flash voltage. This is the basis for schematic-level feasibility. Preserve idle high and verify boot/reset/download with Back held and released on the actual board before wiring approval. UART2 RX and Back cannot share GPIO5 simultaneously; UART2 is currently unused.

GPIO12 is rejected as a default active-low pull-up button because of the flash-voltage strap. GPIO2 could be used only with explicit download-mode accommodation, so it is not the preferred Back candidate. GPIO34 would be attractive at chip level but is not available on the standard header. No GPIO is newly configured by this task.

The OLED is I2C, so it cannot share SPI wires with SD and does not consume SD bus bandwidth. A future second SPI device could share SCK/MOSI/MISO with its own CS, but there is no currently configured second SPI device and few spare safe CS pins; no shared-bus design is required for this task.

Use a 3.3 V socket/module and 3.3 V-compatible signals. Provide short wiring, local bypass/bulk capacitance and a supply capable of Ethernet plus SD transient current. The manufacturer board's supply-current guidance is not a verified spare-current budget for SD. Check regulator thermal/current margin, rail droop, card-in/out boot states and clock integrity, particularly GPIO17's LED branch. Avoid generic 5 V modules that drive MISO at 5 V.

Per [Espressif SD pull-up requirements](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/sd_pullup_requirements.html), provide 10k pull-ups on CMD and DAT0-DAT3 even in SPI mode, including card-side DAT1/DAT2 that are otherwise unconnected to ESP32. CLK does not require the data-line pull-up. Keep DAT2 away from GPIO12; this mapping avoids its flash-voltage conflict. Ensure the chosen SD module does not force GPIO15 low during reset. GPIO17 is not used as an Ethernet clock output here: Ethernet uses an external clock input on GPIO0 and GPIO16 oscillator enable.

If the user's board differs from this drawing or GPIO5 is already connected to another accessory, Outcome A must be revisited; do not silently consume an SD reservation for Back. No physical board revision, resistor values, supply headroom or waveforms were measured in this software-only task.

## 8. Files and regression coverage

Added:

- `tests/verify_field_ui.py`: target GCC compile-time evaluation of production navigation, render, completion handling and button polling with deterministic SDK fixtures.
- `FIELD_UI_V1_GPIO_SD_AUDIT.md`: this report, UI examples, migration map, pin inventory and feasibility evidence.

Modified:

- `main/ui_menu.c`: five-section home/status, Network Tools, selected-host actions, existing SNMP runtime routing, result/ready boundaries, parent Back, refresh/identity guards and OLED presentation.
- `main/ui_buttons.c`: short OK on release; held OK generates Back once and suppresses short OK. Electrical configuration unchanged.
- `main/ui_buttons.h`: logical Back event and interaction contract.
- `tests/verify_switch_info_ui.py`: adapt legacy Switch Info parent fixture to Network Tools; all formatter cases retained; full menu routes now covered by new fixture.
- `tests/verify_enhanced_api_ui.py`: extract Details from render rather than first matching Back switch case, adapt Details to captured host and return to Host Actions; existing API/vendor/hostname/ICMP rendering cases retained.

No production files outside these three UI files changed. Generated evidence is under ignored `build/`.

All **19 commands pass**: 17 `verify_*.py` scripts and both Node suites, `web_ui.test.cjs` and `snmp_runtime_ui.test.cjs`. Final summary: `build/field_ui_tests.json`; individual outputs: `build/field_ui_verify_*.log` and `build/field_ui_*.test.log`.

New production-body coverage includes boot home, known/unknown link speed, link up/down, five-section reachability, cursor wrap, ten repeated entry/exit rounds, selection without execution, Quick Test start/completion/repeat boundary, discovery completion/list/host actions/empty lists, refresh while open/running/completed, changed MAC/run/network, menu behavior during link loss, SNMP ready/run/page/back/timeout/invalid request/text sanitization, every screen's row bounds, System Status/About/firmware route and second-OK authorization, OTA navigation priority, PROFINET completion/cancel-parent behavior. Button cases include bounce, release-triggered OK, 449 ms without Back, exactly one Back at 450 ms and during longer holds, suppressed short OK on release, repeat entry, held-at-boot, UP and tick wrap. Contextual Back hints are checked for every screen enum and the relevant discovery, PROFINET and OTA states.

These tests execute adapted production bodies under target GCC constexpr, with SDK IO/getters mocked and bounded formatting adapted; they are not hardware/button-timing tests. Existing parser, worker/runtime, lifecycle, Web fixtures and generated release-image validation remain in the complete regression gate.

## 9. Normal build / resources

ESP-IDF v6.1, classic ESP32, normal GCC 15.2 toolchain. Final `idf.py build` and `idf.py size`: exit 0. Build log: `build/field_ui_build_verified.log`; size log: `build/field_ui_size_verified.log`; result codes: `build/field_ui_build_results.json`.

| Resource | Preserved task-start baseline | Field UI v1 | Delta |
| --- | ---: | ---: | ---: |
| Application binary | 509856 bytes | 513232 bytes | +3376 |
| Unpadded image from linker map | 509694 bytes | 513106 bytes | +3412 |
| Static DRAM | 142849 bytes | 143569 bytes | +720 |
| DRAM remaining | 37887 bytes | 37167 bytes | -720 |
| IRAM | 51003 bytes | 51003 bytes | 0 |
| Menu task allocation | 4096 bytes | 4096 bytes | 0 |
| Button task allocation | 2048 bytes | 2048 bytes | 0 |
| Button queue | 12 enum events | Same | 0 |

Existing OTA slot: 1740800 bytes. New binary headroom: 1227568 bytes (about 70.5%). Bootloader remains 26240 bytes. Version remains `0.7.0-dev`. This baseline is the binary/map preserved at task start, not the older SNMP report's binary.

No new task, queue, explicit UI heap allocation, or storage is added. New host/SNMP snapshots reside in static storage, included in the +720 DRAM total. Button state gains timing/flags (+24 bytes across three entries, included in total). Existing engine allocations do not change; using local SNMP invokes the same existing worker as Web.

Additional compilation with `-fstack-usage`, same production flags, writes only scratch objects in `build/`. Evidence: `build/field_stack_audit.log`. Individual function frames:

| Function | Before | After |
| --- | ---: | ---: |
| menu_task | 1200 bytes | 1200 bytes |
| render | 560 bytes | 560 bytes |
| navigate | 432 bytes | 432 bytes |
| button_task | 48 bytes | 48 bytes |
| selected_current | Absent | 592 bytes |
| back | Absent | 32 bytes |

Frames are not whole call-chain high-water measurements. `menu_task -> render -> selected_current` contributes 2352 bytes before SDK/libc callers; the existing 4096-byte allocation is preserved. Hardware stack high-water, heap minima, screen legibility and actual button operation are unmeasured because flashing was explicitly excluded. The SNMP runtime already owns its instrumentation; no worker changes were made.

## 10. Review boundary

Implementation and software validation are complete. Remaining hardware caveats are verification of the physical board revision, GPIO5 LED/strap behavior, SD clock load, card power transients, and runtime UI stack margin. SD remains a future field-to-office bridge; no Save item pretends storage exists. OLED and buttons provide the diagnostic workflow independently of a laptop; Web remains optional extended detail and firmware upload.

`git diff --check` passes. The exact tracked diff stat is recorded below; Git omits new untracked files from this command, so the two added files are separately listed above.

```text
 main/ui_buttons.c               |  20 +++-
 main/ui_buttons.h               |   5 +-
 main/ui_menu.c                  | 255 +++++++++++++++++++++++++++++++++-------
 tests/verify_enhanced_api_ui.py |  14 ++-
 tests/verify_switch_info_ui.py  |  23 ++--
 5 files changed, 249 insertions(+), 68 deletions(-)
```

