# Syndetis 0.4.0 — Discover implementation report

## Architecture and active algorithm

`discovery.c/.h` owns one asynchronous 4096-byte-stack FreeRTOS worker, a
mutex-protected status/result table, four permanent ping callback contexts,
binary completion semaphores and one permanent ARP callback job. HTTP and UI
read snapshots only. `diagnostic.c/.h` supplies an atomic operation reservation
shared with AutoTest. Overlapping scans and overlapping major diagnostics are
rejected. Initialization/resource failures are nonfatal to Ethernet, HTTP or UI.

1. Snapshot Ethernet link, IPv4/mask and network revision. Convert addresses to
   numeric host byte order. Validate unicast IP and contiguous, nonzero mask.
2. Calculate network = IP & mask, broadcast = IP | ~mask, prefix, usable range
   and counts from the actual mask. Reject networks with over 254 usable hosts.
   `/24` has 254 usable addresses and 253 candidates excluding this device.
   `/31` and `/32` have zero candidates because network/broadcast are excluded.
3. Traverse usable addresses numerically, skipping own IP, network and broadcast.
   The gateway is treated like any other candidate. Create at most four active
   Ethernet-bound ICMP sessions per batch, one echo each, eight payload bytes,
   250-ms receive timeout, zero inter-packet interval.
4. Wait using semaphores and a 1500-ms batch completion deadline. Check link/IP
   revision and OTA during waits. Stop delayed sessions, allow a bounded 500-ms
   cleanup wait per session, retain permanent storage for any later callback.
5. Submit the four target records to `tcpip_try_callback`. On the TCP/IP thread,
   look up stable ARP entries for the Ethernet netif and copy raw six-byte MACs.
   Wait at most 1000 ms. No ARP/netif pointer escapes the callback. A timed-out
   callback job cannot be reused until its completion semaphore is observed.
6. Record a device when ARP is present OR the expected IPv4 source replies to
   ICMP. ARP-only hosts remain present; missing ICMP is not an offline verdict.
   RTT is recorded only for ICMP replies, including a legitimate zero-ms value.
   Batch order and ascending candidate traversal produce numerically sorted IPs.
7. Retire completed ping sessions and wait 1100 ms before another batch. The
   installed IDF implementation frees deleted session tasks after a 1000-ms idle
   check. This avoids accumulating retiring tasks and heap objects across batches.
8. On completion record monotonic uptime and elapsed milliseconds. Link loss,
   IPv4 revision changes, active OTA, resource failures, callback timeouts or
   scan deadline produce `error` and clear the device count. A new run clears
   previous results immediately. No aborted scan presents old successful results.

Maximum: 254 usable addresses; 253 actual candidates on a normal /24.
Concurrency: four probes; one discovery worker; one submitted ARP job at a time.
Normal silent-/24 estimate: `ceil(253/4) * (250 + 1100) ms = 86.4 s`, plus
callback/scheduling overhead. A 120-s overall deadline is checked between
batches. An in-progress batch/cleanup can add a few seconds to that deadline.
This deliberately trades speed for conservative RAM/task use with the installed
ping implementation. Network traffic is modest: at most one ICMP attempt per
candidate, with lwIP's normal ARP resolution/retry traffic as needed.

No TCP/UDP port scanning, application-service connections, reverse DNS, mDNS,
NetBIOS, SSDP, SNMP, LLDP, CDP, industrial requests, OUI database or SD use.

## Exact locally verified APIs

ICMP header: `ping/ping_sock.h`.
APIs: `esp_ping_new_session`, `esp_ping_start`, `esp_ping_get_profile`,
`esp_ping_stop`, `esp_ping_delete_session`; profiles `ESP_PING_PROF_IPADDR`
and `ESP_PING_PROF_TIMEGAP`. `esp_netif_get_handle_from_ifkey("ETH_DEF")` and
`esp_netif_get_netif_impl_index` select the Ethernet interface.

ARP header: `lwip/etharp.h`; `etharp_find_addr` returns stable entries and MAC.
`netif_get_by_index` in `lwip/netif.h` selects the matching lwIP interface.
`tcpip_try_callback` in `lwip/tcpip.h` runs both calls on the TCP/IP thread;
the callback copies MAC data before signaling its completion semaphore.
This dependency is isolated in `lookup_arp()` in `discovery.c`. No private
structures, raw Ethernet hooks, lwIP internal table layouts or unsynchronized
ARP access are used. Headers and implementation were inspected in the locally
installed `C:\esp\v6.1\esp-idf\components\lwip` tree before selecting APIs.

## RAM and UI

Linker-map-confirmed record size: 20 bytes. Discovery table: 5080 bytes.
HTTP snapshot table: 5080 bytes, avoiding a large HTTP stack allocation or
holding the discovery mutex during network sends. Discovery metadata: 140 bytes.
Additional allocations: 4096-byte discovery task stack, up to four 3072-byte
ping task stacks, small ping/socket objects and six semaphore/mutex objects.
The result storage is fixed; no per-device allocation or vendor/name strings.
The dedicated device record can be extended later without changing scan/UI
ownership. No future identification fields are implemented in this release.

OLED: Discover entry -> actual subnet/count -> OK starts. Invalid IPv4/link and
oversized subnets show an explanatory Back screen. Progress uses the existing
200-ms UI loop and changed-row rendering. Completion -> OK opens devices.
UP/DOWN navigates; OK opens details. Final Back row returns to completion;
UP/DOWN on completion returns to main. Details show IP, MAC/Unknown, ARP and
RTT or ICMP: NO REPLY; OK returns. Zero results still have a Back entry.
Leaving the scan page leaves the worker running; entering and OK rejoins it.
Physical OK is the only start path; browser controls remain read-only.

## GET /api/discovery

JSON fields:

```json
{
  "state": "complete",
  "network": "192.168.1.0",
  "mask": "255.255.255.0",
  "prefix": 24,
  "broadcast": "192.168.1.255",
  "usable_hosts": 254,
  "candidate_hosts": 253,
  "processed": 253,
  "found": 1,
  "start_uptime_ms": 10000,
  "completion_uptime_ms": 96400,
  "duration_ms": 86400,
  "max_hosts": 254,
  "limit_note": "Active discovery is limited to 254 usable hosts (/24 max)",
  "error": "",
  "devices": [
    {
      "ip": "192.168.1.10",
      "mac": "00:11:22:33:44:55",
      "arp_present": true,
      "ping_response": false,
      "rtt_ms": null
    }
  ]
}
```

States: `idle`, `running`, `complete`, `error`. Unknown MAC and RTT are JSON
`null`. Results are RAM-only and clear on reboot. The API streams a consistent
snapshot as chunked JSON. The fully offline browser polls every two seconds,
pauses during firmware upload and renders values with textContent. It clearly
distinguishes never run, progress, error and zero completed results. No scan-start
route/button exists. No additional managed components are required.

## Preserved verified foundation

AutoTest changes are limited to acquiring/releasing the diagnostic reservation;
its tests, grading, gateway three-packet sequence, DNS logic and timing remain
unchanged. Hardware pin constants, Ethernet setup, SH1106 driver, button behavior,
network event handling, OTA handler and firmware validation code are unchanged.
`sdkconfig`, `sdkconfig.defaults` and managed dependencies are unchanged.
Both OTA slots remain 1,740,800 bytes at 0x20000 and 0x1D0000. Rollback,
physical authorization, second-OK confirmation, five-minute window and
POST `/api/firmware` remain unchanged. Application descriptor supplies 0.4.0
to About and web status; project name remains `syndetis_finder`.

No physical device was flashed, erased or contacted. Hardware acceptance is
still needed for actual ARP-only devices, cable interruption, buttons and OTA.

## Added and edited source files

Added: `main/discovery.c`, `main/discovery.h`, `main/discovery_logic.h`,
`main/diagnostic.c`, `main/diagnostic.h`, `tests/discovery_logic_test.c`,
`tests/verify_discovery_logic.py`, and this report `DISCOVERY_0.4.0.md`.

Edited: `version.txt`, `main/CMakeLists.txt`, `main/app_main.c`,
`main/autotest.c`, `main/ui_menu.c`, `main/web_server.c`, `main/web_index.html`,
`tests/web_ui.test.cjs`, `README.md`.

Build-generated file changes and final release measurements are listed below.

## Final automated verification and OTA artifact

All final checks passed:

- `idf.py build`: ESP-IDF 6.1, ESP32, application version 0.4.0.
- `python tests/verify_release.py`: descriptor/version/project/chip checks,
  ESP image checksum and SHA256, partition MD5/bounds, two OTA slots and fit.
- `python tests/verify_autotest_rules.py`: all 39 existing policy assertions.
- `python tests/verify_discovery_logic.py`: compile-time evaluation of actual
  firmware pure functions and test loops, including all requested subnet cases.
- `node tests/web_ui.test.cjs`: existing OTA/AutoTest checks plus discovery
  idle/progress/errors/zero results, ARP-only hosts, MAC, zero-ms RTT and no
  remote discovery control.
- Preservation comparison against Git HEAD: OTA sources, board pins, OLED,
  buttons, network observers, both sdkconfig files, dependency lock, bootloader
  binary and partition binary unchanged (text normalized for CRLF only).

Firmware size: **461,040 bytes** (`0x708F0`).
Remaining per OTA partition: **1,279,760 bytes**, approximately
73.52% free.
Application SHA256: `3a9874840c673ae7d2fe8ee2a6d8c1ed8c7b85cfd31de0d8a24e52d360b4cd7d`.
Bootloader: 26,240 bytes; identical to the existing verified bootloader.
Exact application path for browser OTA upload:

`C:\Users\theoc\Documents\ChatGPT\Syndetis\build\syndetis_finder.bin`

The build emitted upstream ESP-IDF Kconfig/dependency warnings, without project
compilation errors. The installed Windows compiler needed execution outside the
sandbox; the approved commands built and verified local files only.
Actual discovery timing, ARP-only behavior and OTA need hardware acceptance;
no physical hardware was flashed or erased.

## Generated files changed or added by the build

These are generated outputs, not manual source changes.

Changed:

- `build/.bin_timestamp`
- `build/.ninja_deps`
- `build/.ninja_log`
- `build/CMakeFiles/CMakeConfigureLog.yaml`
- `build/bootloader/.ninja_log`
- `build/build.ninja`
- `build/compile_commands.json`
- `build/esp-idf/esp_app_format/CMakeFiles/__idf_esp_app_format.dir/esp_app_desc.c.obj`
- `build/esp-idf/esp_app_format/libesp_app_format.a`
- `build/esp-idf/esp_system/ld/sections.ld.fingerprint`
- `build/esp-idf/main/CMakeFiles/__idf_main.dir/__/__/web_index.html.S.obj`
- `build/esp-idf/main/CMakeFiles/__idf_main.dir/app_main.c.obj`
- `build/esp-idf/main/CMakeFiles/__idf_main.dir/autotest.c.obj`
- `build/esp-idf/main/CMakeFiles/__idf_main.dir/ui_menu.c.obj`
- `build/esp-idf/main/CMakeFiles/__idf_main.dir/web_server.c.obj`
- `build/esp-idf/main/libmain.a`
- `build/project_description.json`
- `build/syndetis_finder.bin`
- `build/syndetis_finder.elf`
- `build/syndetis_finder.map`
- `build/web_index.html.S`

Added:

- `build/component_manager_run_counter.15912`
- `build/esp-idf/main/CMakeFiles/__idf_main.dir/diagnostic.c.obj`
- `build/esp-idf/main/CMakeFiles/__idf_main.dir/discovery.c.obj`
- `build/log/idf_py_stderr_output_20292`
- `build/log/idf_py_stderr_output_7956`
- `build/log/idf_py_stdout_output_20292`
- `build/log/idf_py_stdout_output_7956`

