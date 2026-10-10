# Syndetis 0.6.0-dev — Enhanced Discover Phase A

Implemented and normal ESP-IDF 6.1 / GCC build validated on 2026-10-10. No flashing, OTA or hardware validation was performed. Existing working-tree changes were preserved.

## Files

Added firmware: `main/oui_vendor.c`, `main/oui_vendor.h`.
Added tests/evidence: `tests/oui_sources.json`, `tests/verify_enhanced_discovery.py`, `tests/verify_enhanced_api_ui.py`, this report, `phase_a_baseline.json`, `phase_a_test_results.json`, individual test logs and build logs.
Modified for this phase: `main/discovery_logic.h`, `main/discovery.c`, `main/CMakeLists.txt`, `main/ui_menu.c`, `main/web_server.c`, `main/web_index.html`, `tests/fast_discovery_fixture.py`, `tests/web_ui.test.cjs`, `version.txt`. Generated build artifacts also changed. Git diffs include pre-existing changes; they must not all be attributed to this phase.

## Model and ownership

`discovery_device_t` is 128 bytes on ESP32 GCC (asserted by the test). It retains existing names `ip`, `rtt_ms`, `mac[6]`, `mac_valid`, `arp_present`, `icmp_responsive` and adds:

- `valid`, `vendor[32]`, `vendor_present`;
- `hostname[64]`, `hostname_present` (reserved, empty in Phase A);
- `last_seen_ms`, `flags`, `discovery_source` (1 = ARP);
- `locally_administered`, `ping_measured`, `packet_loss` (0 or 100 for the existing single echo probe).

Numeric IPv4 uses host byte order. Last-seen is uptime milliseconds at the last accepted ARP cache observation, not wall-clock time. Flags reserve future metadata without implementing industrial protocols. No device role is inferred from a vendor.

The existing rolling TCP/IP-thread callback still uses supported `etharp_get_entry`, copies six MAC bytes into application storage, and retains hosts through cache eviction. Duplicate IPs update the same record; MAC changes invalidate any old vendor. Null MAC observations are supported without inventing an address. Each new run replaces records as they are observed. Completed snapshots and device getters reject an old network revision or link loss; running scan cancellation retains the existing coordinator, callback quarantine and network-generation checks.

Vendor work runs in the discovery worker after ARP collection, outside the state lock. It adds no packets and no sleeps. Records remain in numeric IPv4 order during enrichment. Switch Info stays independent.

## Offline vendor table

Four sorted MA-L prefixes, two vendors:

| Prefix | Display vendor | Verification |
| --- | --- | --- |
| 00:00:0C | Cisco | Cisco-hosted reproduction of IEEE registry rows |
| 00:01:42 | Cisco | Same registry reproduction |
| 00:01:43 | Cisco | Same registry reproduction |
| 00:1B:1B | Siemens | Siemens training document, page 42, actual CPU MAC |

Primary-source URLs and evidence are retained in `tests/oui_sources.json`:

- [Cisco registry reproduction](https://community.cisco.com/t5/lan-%E3%82%B9%E3%82%A4%E3%83%83%E3%83%81%E3%83%B3%E3%82%B0/cisco-systems-inc-%E3%81%AEmac-address/m-p/1556358/highlight/true)
- [Siemens training document](https://www.automation.siemens.com/sce-static/learning-training-documents/tia-portal/hw-config-s7-1500/sce-012-100-unspecific-hardware-configuration-s7-1500-r1703-en.pdf)

IEEE direct downloads failed (web fetch 418, local network DNS unavailable). The deliberately small table avoids guessing the broader requested examples. It is incomplete even for these two vendors; other prefixes return unavailable.

`oui_lookup_vendor(mac,out,out_len)` uses binary search, at most three prefix comparisons for this table, fixed strings, no heap, and bounded NUL-terminated output. Valid output is cleared on failure. A one-byte buffer succeeds for a known prefix and contains only NUL. Null MAC/output and zero capacity fail safely. Zero, multicast, broadcast and locally administered addresses never receive an OUI label. Private addresses display `Local/Private`; unknown globally administered addresses display `Unknown` on OLED/browser and `null` in JSON.

## Presentation

OLED keeps the existing result summary, list and Back routes. Device details now show `DEVICE n/count`, IPv4, complete uppercase colon-separated MAC, vendor/private/unknown, and existing ping result. Vendor is limited to 21 characters (126 pixels with the existing six-pixel cell). UP/DOWN wraps between hosts; OK returns to the existing list. Zero-host navigation keeps the Back path. SH1106 and GPIO drivers were unchanged.

The web table adds Vendor, using `textContent`. `/api/discovery` retains all existing fields and adds per-host `vendor`, `locally_administered`, `reachable`, `latency_ms`, `discovery_source`, and `last_seen_ms`. Missing MAC/vendor/latency use null; hostname is omitted unless present. `reachable` means ARP-observed or ICMP-responsive; `ping_response` continues to distinguish an actual echo reply. `latency_ms` aliases existing `rtt_ms`. All strings are escaped, per-host chunks are checked against the existing 1024-byte body, and the complete response is streamed from one fixed HTTP-owned snapshot. The existing HTTP task has 12 KiB stack; no task stack was increased.

## Timing and memory

ARP rate remains 25 requests/second, 40 ms pacing, 500 ms settling, four concurrent pings only for discovered hosts, 250 ms timeout, and existing 1100 ms session retirement. The deterministic actual-callback fixture verifies 253 requests / 10-entry cache with a logical 10,120 ms emission sweep and 253 retained observations in its dense case. This is simulated timing, not measured LAN discovery duration. Sparse fixture: four hosts.

Runtime logs now include total duration/host count, aggregate vendor lookup microseconds, free heap and discovery-worker stack watermark (bytes on ESP-IDF). These actual hardware measurements are pending. Lookup has no per-device network delay; runtime lookup cost has not been measured.

| Item | Size / effect |
| --- | ---: |
| OUI table | 144 bytes in read-only storage (four 36-byte entries) |
| One device record | 128 bytes, previously 20 |
| Main 254-record table | 32,512 bytes |
| HTTP 254-record snapshot | 32,512 bytes |
| Four probe records | 512 bytes |
| Total device-record storage | 65,536 bytes, previously 10,240 |
| Static record storage increase | 55,296 bytes |
| Per-device dynamic allocation | None |
| Application binary increase | 1,600 bytes |

The larger static storage reduces the available RAM budget; actual boot/free-heap impact and stack margin must be measured on hardware. Other bookkeeping/scratch and SDK allocations are not included in the device-record total. The table-size assertion and record-size assertion use the target GCC layout.

## Validation

New tests evaluate production OUI/model/getter bodies, JSON handler and OLED/navigation branches with target GCC constexpr fixtures. Only SDK calls and formatting are adapted; these are not device execution. Coverage includes known industrial/network vendor, multiple Cisco prefixes, invalid MACs, truncation, null arguments, MAC copy/update, vendor invalidation, missing MAC, duplicate IP, full table, stale generation/link loss, escaped strings, missing/optional metadata, 254 maximum-escaped hosts, device navigation and long OLED labels. Existing callback/lifecycle/order tests and browser fixtures complement this coverage. Browser tests exercise vendor text safely, private/unknown cases, 254 rows, legacy fields and existing OTA/AutoTest behavior.

| Script | Result |
| --- | --- |
| verify_switch_info_parse | PASS |
| verify_switch_info_cdp | PASS |
| verify_switch_info_api | PASS |
| verify_switch_info_ui | PASS |
| verify_release | PASS |
| verify_autotest_rules | PASS |
| verify_discovery_logic | PASS |
| verify_fast_discovery | PASS |
| verify_link_loss | PASS |
| verify_enhanced_discovery | PASS |
| verify_enhanced_api_ui | PASS |
| web_ui | PASS |

All 12 scripts exited zero. Final release integrity was rerun after the final build. Tests do not establish live OLED, HTTP or hardware behavior.

## Build and integrity

Normal `idf.py build`, ESP-IDF v6.1.0, GCC 15.2.0 / esp-15.2.0_20251204: PASS, exit 0. GCC selected explicitly. First sandboxed attempt stalled at the Unicode/environment stage; it was interrupted, and the build was run with `PYTHONUTF8=1` outside the sandbox. No Windows system settings changed. The successful configure/build is in `phase_a_build_gcc.log`; final incremental build is in `phase_a_build_final.log`.

- Firmware: 0.6.0-dev (verified in image descriptor).
- Application: 477,328 bytes; baseline 475,728 bytes.
- Bootloader: 26,240 bytes; 2,432 bytes below the existing allowance.
- OTA slots: 1,740,800 bytes each; headroom 1,263,472 bytes each (72.58%).
- Image SHA-256: `80924e8096392b1b41180124aac8db6b1332f11a9c6bad4137d7f0969546e702`.
- C/C++ compiler warnings: none in successful build logs. Errors: none.
- Configuration warnings: five existing SDK WiFi/WPA private-include warnings, plus four SDK Kconfig invalid boolean `default 0` notices treated as `n`.
- Partition SHA-256: `41d8ef7960bf03647249e26ca1ace2b8d35f317a87f6c2ff5bc9f4bdbdbf602b`, matching prior evidence.

All protected baseline hashes match: root CMake, sdkconfig/defaults, AutoTest, network status, RX/LLDP/CDP/selection backend, OLED/button/OTA drivers and patches. No partitions, DMA patch, ping patch, GPIO configuration or OTA authorization were changed. Discovery getter lifecycle validation is the only behavior fix beyond metadata/presentation enrichment.

## Manual home-LAN validation — only after separately approved flashing

1. After a separately approved firmware installation, start Syndetis on the normal wired LAN with DHCP. Confirm About/web reports 0.6.0-dev. Open a serial log monitor using the normal known device port; this phase does not issue a flash command.
2. Record the device IPv4, subnet and current free heap. Open the router DHCP/client list and note active IPv4/MAC pairs. DHCP leases may include sleeping hosts, while Discover excludes its own IP and requires ARP response; counts need not exactly match.
3. On OLED select Discover, OK, then OK to start. Wait for results. Record total duration, found hosts, vendor lookup microseconds, free heap and worker stack watermark from DISCOVERY logs.
4. Open Devices, select a host, compare IPv4 and all six MAC bytes with the router. UP/DOWN should wrap between host details; OK should return to the list, then Back to results. Verify no text overflows.
5. If available, verify one of the four included prefixes. Other vendors/prefixes should remain Unknown. Vendor is manufacturer evidence, not a PLC/device-role classification.
6. Check at least one private MAC (first octet bit 0x02 set) displays Local/Private and never Cisco/Siemens. A globally administered unknown prefix should show Unknown. Do not change a real device MAC merely to produce a fixture.
7. Repeat Discover at least three times. Record counts/durations/heap/watermarks. Compare numeric IPv4 ordering and device associations; investigate disappearing devices against their sleep/link state. Heap should recover after the existing ping-retirement period; record any repeated decline.
8. During one scan disconnect Ethernet. Verify ABORTED_LINK_DOWN / no published hosts. Reconnect, get DHCP, verify stale completed results are not offered as current, then run a fresh Discover. Same MAC on a new context must belong only to fresh scan records.
9. Run AutoTest afterward and compare link/DHCP/gateway/DNS results with the previous firmware. Ensure discovery and AutoTest still serialize through the coordinator.
10. Open Switch Info and exercise its existing UP/DOWN/OK pages. A home switch without LLDP/CDP may correctly show No neighbor; use a known advertising switch to validate actual neighbor data. ARP hosts must not appear as direct-neighbor evidence.
11. At `http://<device-ip>/`, verify Vendor/IP/MAC/ICMP/RTT rows and 2-second refresh. Fetch `http://<device-ip>/api/discovery`, parse JSON, compare every record with OLED/router; verify uppercase MACs, null unknown vendor, private flag, absent hostname, and both old/new latency fields. Confirm zero results and link-loss states are sensible where observed.
12. Save serial logs and JSON for all runs with router comparison notes. Report measured heap minimum, final free heap, stack watermark, per-run duration/host count/vendor time and any mismatches before treating runtime validation as passed.

## Deferred

Hostname resolution (including reverse DNS/mDNS/NBNS), a broader independently verified vendor table, protocol correlation, device roles and service metadata remain future work. No PROFINET DCP, Modbus/S7/EtherNet/IP, SNMP, TCP/UDP/port scanning or other probes were added. Hardware performance and runtime regression validation are pending.
