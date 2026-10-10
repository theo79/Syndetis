# Phase C — OLED Switch Info implementation and validation

The existing Switch Info menu placeholder now opens five snapshot-driven OLED pages. Normal ESP-IDF 6.1 GCC build and all eight validation scripts passed. Firmware remains **0.5.0-dev**. No flash, OTA, or physical hardware test was performed.

## Files

Modified during Phase C:

- `main/ui_menu.c`: activate existing menu item, route navigation, obtain current copied snapshots.
- `main/CMakeLists.txt`: register the new presentation source.

Added:

- `main/ui_switch_info.h` and `main/ui_switch_info.c`: pure bounded presentation and page-navigation helpers.
- `tests/verify_switch_info_ui.py`: lightweight GCC constexpr render/navigation fixture.
- This report and generated build/test evidence (`phase_c_*.log`, `phase_c_test_results.json`).

The main menu already contained `AutoTest`, `Discover`, **`Switch Info`**, `Network Tools`, `Industrial`, and `System` in that order. Its order and item count are preserved. About remains unchanged, including its existing `by T.Anastopoulos` attribution. No separate attribution change was made.

## Navigation and refresh

Highlight Switch Info and press OK to enter at Summary (1/5). DOWN advances; UP goes back. Both wrap across five pages. OK returns to the main menu with Switch Info still selected. No long-press behavior or new button model was added.

Waiting/unavailable conditions show one status screen. UP/DOWN do not expose empty result pages in these states; the page index resets to Summary. A fresh valid neighbor therefore begins at Summary after waiting. Existing OTA receiving/success precedence still overrides navigation.

The existing menu task retrieves a snapshot while viewing Switch Info at its existing 200 ms queue/refresh cadence. OLED writes still occur only when the eight rendered rows differ from the previous rows. Unchanged TTL, counters, or receive timestamps do not cause redundant display writes.

## Status screens

Exact text rows for an available listener without a valid neighbor:

```text
SWITCH INFO

No neighbor found

Waiting for LLDP...

LLDP listener active
OK: BACK
```

The active hint appears only when `available` is true. Waiting does not imply a capture failure and is useful without DHCP or a transmitting switch.

Backend unavailable:

```text
SWITCH INFO

Unavailable

LLDP setup error
Code: 0x<actual code>

OK: BACK
```

The code is formatted from the actual setup status; the placeholder above documents layout, not an observed hardware error. Unavailable takes precedence over any neighbor data in the supplied snapshot.

## Valid neighbor pages

| Page | Displayed fields |
| --- | --- |
| Summary 1/5 | Protocol, system name, Port ID, advertised TTL in seconds |
| Identity 2/5 | Chassis ID, Port ID, source MAC |
| Network 3/5 | Management IPv4, PVID |
| Description 4/5 | System description and port description, up to two rows each |
| Capabilities 5/5 | Supported/enabled hexadecimal masks and abbreviated labels |

Every valid page has its title/count and `UP/DOWN PAGE OK BACK` footer. Fixed optional pages display explicit `N/A` values rather than blank pages. Capability value zero is shown as `None` when advertised; absence is shown as `N/A`. Reserved capability bits are retained in the mask and labelled `Unknown`. Full masks remain readable even if their label list is truncated.

Protocol text comes from the common enum: NONE, LLDP, CDP, and an UNKNOWN fallback. CDP is a presentation mapping only; no CDP reception/parser was added. The same pages can consume a future common-model CDP snapshot.

Missing system name, chassis/port text, descriptions, management address, PVID, and capabilities show `N/A`. TTL is the advertised backend value, not a UI countdown. No fields are fabricated or inferred from prior sessions.

## Safety and lifecycle

All lines are 22-byte buffers with at most 21 printable characters plus a terminator. Network fields are scanned only within their fixed model capacities, including a defensive nonterminated-array case. Control and non-ASCII bytes become `.`. No network string is a format string.

Long identifiers/names retain their first 18 characters followed by `...`. Descriptions use 21 characters on the first row, then at most 18 characters plus `...` on the second if more data remains. There is no scrolling or unbounded concatenation. IPv4 and full source MAC fit their rows. Capability labels may shorten with ellipsis, while their full 16-bit masks remain visible.

`switch_info_get()` copies current backend state and releases its lock before returning. Formatting and OLED drawing occur afterward; the UI never accesses mutable backend context or holds backend synchronization while drawing. A UI-task-owned static copy (1,432 bytes in the link map) avoids placing this model on the existing 4 KiB task stack. The page index adds four static bytes; the task stack size and priority are unchanged.

The backend remains authoritative for expiry and link generation. On every render, unavailable/invalid snapshots clear all rows and select a status screen, regardless of any residual strings. Link loss and TTL expiry therefore remove current neighbor information on the next UI refresh after backend invalidation. Reconnection waits for a fresh publication. No second TTL timer or LLDP parsing exists in the UI.

Runtime stack/heap margins and physical OLED appearance have not been measured; the static-copy choice limits new stack pressure but is not a hardware watermark result.

## Build and tests

Normal toolchain: ESP-IDF 6.1, GCC 15.2.0 (`esp-15.2.0_20251204`), official Python 3.11.15. Final `idf.py build` completed with exit 0, including application image generation, bootloader check, and OTA slot check.

| Measurement | Result |
| --- | --- |
| Application | **472,544 bytes** |
| Existing OTA slot | **1,740,800 bytes** |
| OTA headroom | **1,268,256 bytes**, about 73% |
| Change from pre-Phase-C GCC image | +2,432 bytes |
| Bootloader | 26,240 bytes, size check PASS |
| Embedded firmware version | **0.5.0-dev**, independently verified |
| Application binary | `C:/Users/theoc/Documents/ChatGPT/Syndetis/build/syndetis_finder.bin` |
| Application SHA-256 | `7ddde4f17b55f7fe4af5775a48b3529f92ff10102006e486054240a0430cebab` |
| Partition SHA-256 | `41d8ef7960bf03647249e26ca1ace2b8d35f317a87f6c2ff5bc9f4bdbdbf602b`, unchanged |

Build issues preserved for review: four attempts encountered GNU `ar` archive-copy errors (`Invalid argument`) in different SDK libraries: HTTP server, bootloader support, mbedTLS, and MCPWM HAL. Previously failed archives succeeded on retry; reducing jobs did not eliminate the issue immediately. The fifth attempt, with supported process setting `IDF_PY_BUILD_JOBS=1`, completed. No source/configuration/security workaround was applied; the underlying host cause is unverified. Failed logs are saved as `phase_c_build_first_attempt.log` through `phase_c_build_fourth_attempt.log`; final output is `phase_c_build.log`.

Configuration retained ESP-IDF private-include dependency warnings and four SDK boolean-default Kconfig notices. Git was detected in this run. No C/C++ compiler warnings were found in the build attempt logs. An optional sandboxed `nm` query failed with a DLL relocation error; static storage was verified from the generated map instead, without requiring that tool.

| Test | Result | Scope |
| --- | --- | --- |
| `verify_switch_info_ui.py` (new) | **PASS** | Actual formatter and extracted actual menu branches, evaluated by GCC constexpr |
| `verify_switch_info_parse.py` | **PASS** | Parser/RX ownership fixtures and architecture source invariants |
| `web_ui.test.cjs` | **PASS** | Node execution of embedded web UI fixtures, including existing OTA behavior |
| `verify_release.py` | **PASS** | Final image descriptor, checksum/SHA-256, partitions, bounds and slot checks |
| `verify_autotest_rules.py` | **PASS** | Compile-time policy assertions |
| `verify_discovery_logic.py` | **PASS** | Production discovery logic constexpr evaluation |
| `verify_fast_discovery.py` | **PASS** | Callback fixture and configured ARP cache preprocessing |
| `verify_link_loss.py` | **PASS** | Lifecycle/DMA fixtures, source invariants and patch routing |

The new fixture covers menu entry; unavailable/waiting/valid LLDP summary; all five pages; MAC/IP/PVID/capabilities; absent optionals; long and nonterminated fields; control bytes; UP/DOWN wrapping; OK exit; existing menu routes; future CDP enum presentation; invalid/expired snapshot hiding retained fixture fields; getter/page-reset/refresh/OTA-priority integration invariants. Only constexpr qualifiers and SDK-independent declarations are supplied; formatter logic and the tested navigation branches come from production source.

These are compile-time, mocked, static-artifact, and browser-fixture checks. They are **not physical OLED, network reception, or runtime hardware regression tests**.

## Scope integrity

Comparison with the before-build integrity evidence identifies only `main/ui_menu.c` and `main/CMakeLists.txt` as changed pre-existing firmware files. New production files are the presentation module above.

Ethernet RX/backend/parser, DHCP, HTTP/API source, AutoTest, Discover, OTA source, DMA/ping patches, OLED driver/font/address/dimensions, button GPIO configuration, root CMake, sdkconfig, sdkconfig.defaults, dependency lock and version file are unchanged during Phase C. Generated partition bytes match baseline. Existing unrelated working-tree edits were preserved.

## Manual OLED test after separately approved flashing

No update was performed in this phase. After approval and updating with the normal GCC application above:

1. Confirm boot/version `0.5.0-dev` and ordinary Ethernet/OLED startup. Check the real LLDP multicast filter result in serial logs.
2. On the main menu, verify the existing order. Press DOWN twice from AutoTest to highlight Switch Info. Press OK: Summary or a status screen appears. Press OK again: the menu returns with Switch Info selected.
3. With an active backend but no transmitting neighbor, verify `No neighbor found`, `Waiting for LLDP...`, and the active hint. UP/DOWN must not reveal old values or empty result pages. If a genuine backend setup failure occurs, verify the distinct Unavailable/code screen and continued menu operation; do not force a failure by changing RX configuration for this test.
4. Connect directly to a managed switch transmitting untagged nearest-bridge LLDP. Wait 2–3 minutes or the known transmit interval. Compare serial/API data and all five OLED pages against the actual switch hostname, port, chassis, management address, PVID and advertised TTL. Missing advertised optionals should show `N/A`.
5. Enter afresh to confirm Summary 1/5. Press DOWN four times to reach Capabilities 5/5; DOWN wraps to Summary. UP from Summary wraps to Capabilities. Press OK on each page to confirm return to the menu.
6. If a switch/test advertiser supplies long names/identifiers/descriptions, inspect ellipsis and two-row description layout. Check every row remains within the screen and no control characters appear. Do not treat a fixture value as an observed switch field.
7. While viewing any neighbor page, unplug Ethernet. Verify the waiting screen replaces the old neighbor. Reconnect and verify Summary returns only after a fresh advertisement; repeat on a different port if available and verify its new Port ID.
8. With a valid neighbor and link maintained, temporarily disable LLDP TX on the test port, wait past its advertised TTL, and verify waiting state with no stale fields. Re-enable TX afterward and confirm fresh Summary data. Compare `/api/switch-info` validity/expiry counters with the screen.
9. Verify AutoTest and Discover entry/results/navigation still work, the main HTTP/status and Switch Info API respond, and About and the existing OTA authorization screens remain intact. Any actual OTA regression update requires its own approval; do not intentionally interrupt an update.
10. Record physical OLED legibility, button response, heap and task stack margins during ordinary use and diagnostics. No such measurements are claimed here.

Phase C implementation is ready for review before flashing. CDP remains unimplemented, and physical LLDP reception still awaits validation.
