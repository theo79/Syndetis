# Phase D — Passive CDP implementation and validation

Phase D is implemented. Normal ESP-IDF 6.1.0 GCC build and all ten validation scripts pass. Firmware remains `0.5.0-dev`. No flashing, OTA upload, SNMP, or PROFINET work was performed. Physical LLDP/CDP validation remains pending.

## Files changed in this phase

Modified production files:

- `main/CMakeLists.txt`: register the CDP parser and selection helpers.
- `main/switch_info_parse.h`: minimal common-model extensions and CDP parser declarations.
- `main/switch_info.h`: CDP availability, setup error, and three counters.
- `main/switch_info.c`: shared RX routing, worker dispatch, filter setup, independent observations, selection, lifecycle, and logs.
- `main/web_server.c`: selected CDP protocol and optional fields in the existing endpoint.
- `main/ui_switch_info.c`: CDP labels, platform, native VLAN, peer duplex, and raw capabilities in existing pages.

Added production files:

- `main/switch_info_cdp.c`: bounded classifier and parser.
- `main/switch_info_select.h` and `main/switch_info_select.c`: internal dual observations and common selection/lifecycle helpers.

Modified tests: `tests/verify_switch_info_parse.py`, `tests/verify_switch_info_ui.py`.

Added tests: `tests/switch_info_cdp_test.cpp`, `tests/verify_switch_info_cdp.py`, `tests/verify_switch_info_api.py`.

This report and `phase_d_*` logs/evidence are added documentation and validation artifacts. Generated build files also changed. The workspace already contained changes from earlier phases; the list above describes this phase rather than the entire Git diff.

## Filter, classification, and ownership

Setup retains the existing LLDP destination filter (`01:80:C2:00:00:0E`), installs the shared RX callback, then adds `01:00:0C:CC:CC:CC` using supported `esp_eth_ioctl(..., ETH_CMD_ADD_MAC_FILTER, ...)`. CDP filter failure increments filter failures and publishes `cdp_available=false` plus `cdp_setup_error`; LLDP availability, worker, and normal Ethernet remain active. CDP filter addition is the last fallible setup operation. No CDP filter deletion by address is performed: the installed SDK deletes the first matching entry, which could belong to another caller. No promiscuous mode, all-multicast mode, or L2 TAP is enabled.

CDP classification requires all of:

- At least 26 bytes: Ethernet, LLC/SNAP, and four-byte CDP header.
- Exact destination MAC above.
- IEEE 802.3 length at bytes 12–13 between 12 and 1500, no larger than received bytes after the Ethernet header.
- Exact eight-byte LLC/SNAP signature `AA AA 03 00 00 0C 20 00`.

`0x2000` is never treated as an Ethernet II EtherType. Wrong PID/OUI/LLC, tagged frames, and invalid length declarations fail classification and follow normal `esp_netif_receive()` routing. Consequently, rejected classification envelopes are not counted as CDP malformed; malformed CDP counters describe candidates reaching the parser. The parser stops at `14 + declared_length` and ignores Ethernet padding.

LLDP and CDP reuse four 1600-byte slots, a four-entry queue, and one priority-one worker with the existing 4096-byte stack. RX performs a bounded copy and zero-wait enqueue; it does not allocate, parse, or log. Candidate originals are freed exactly once even on oversized/pool/queue drops. Other packets retain the existing `esp_netif_receive()` ownership and return path. Copied buffers survive callback return and are released after worker processing. Protocol metadata travels with each slot.

## Parser behavior and mapping

Versions 1 and 2 are accepted; unknown versions are rejected. Checksum validation covers only the CDP header, checksum, and TLVs. It uses one's-complement 16-bit big-endian words, including Cisco's historical odd-final-byte treatment: a low octet, with `0xff00 + byte - 1` for final bytes at least 128. Invalid checksums are rejected. Generic RFC-style high-octet odd-byte padding is deliberately not accepted. Fixed literal odd/even checksum vectors and an independent fixture producer cover this distinction. Real switch packet compatibility remains a hardware gate.

Every TLV must have length at least four, including its header, and fit completely within remaining declared payload. Maximum TLV count is 64. Unknown structurally valid TLVs are skipped. Parsed singleton duplicates are rejected; address lists may repeat. Device and Port IDs are required, nonempty, and limited to 256 raw bytes each. Raw identity remains complete for withdrawal matching even when display text truncates. Display strings are bounded, terminated, and replace nonprintable/non-ASCII bytes with `?`.

| CDP field / TLV | Common model and presentation |
| --- | --- |
| Device ID / 1 | `device_id`, `chassis_id`, complete raw chassis identity; fallback `system_name` |
| Port ID / 3 | `port_id`, complete raw port identity |
| Addresses / 2; Management Addresses / 0x16 | First supported IPv4 in wire order across lists → `management_ipv4` |
| Capabilities / 4 | Exact four-byte raw `cdp_capabilities`; no invented LLDP mask or enabled-capability meaning |
| Software Version / 5 | `system_description` |
| Platform / 6 | Optional bounded `platform` |
| System Name / 0x14 | Explicit `system_name`, preferred over Device ID fallback |
| Native VLAN / 0x0A | Optional `pvid`, accepted values 0–4094; OLED labels it Native VLAN |
| Duplex / 0x0B | Optional `duplex_present` / `full_duplex`; only values 0 and 1 accepted |
| Header | `protocol=CDP`, version, TTL, and source MAC |

Native VLAN means the peer's advertised native VLAN, not a measurement of this instrument's access VLAN. Duplex means advertised peer duplex, not the local PHY's measured mode. VLAN zero is preserved as advertised. Address envelopes validate count, protocol length, and address length, with at most 64 entries per list. Supported IPv4 identifiers are NLPID `CC` and IEEE 802.2 SNAP `AA AA 03 00 00 00 08 00`, with exactly four address bytes. Other bounded families are skipped; IPv6 is not exposed.

## Selection, expiration, and concurrency

Two internal observations use the same `switch_neighbor_t`: latest LLDP and latest CDP. Public consumers receive one selected common snapshot. Fresh LLDP always wins; otherwise fresh CDP wins; otherwise the snapshot is invalid. Fields are never merged and periodic CDP updates cannot flip the UI away from fresh LLDP. This assumes the ordinary direct-port use case; it does not identify or enumerate multiple peers behind a bridge.

Each observation expires independently at capture timestamp plus TTL. Worker delay never extends its life; already-expired queued observations are discarded. TTL-zero advertisements withdraw only the matching protocol and complete raw Device/Port identity. Link disconnect/stop/reconnect clears both records. Network generation and the private capture epoch are checked before and after parsing, preventing queued old-link data from repopulating a new connection. Snapshot publication and counters retain the existing critical-section discipline; parsing and logging stay outside it. No network generation producer changed.

Existing generic candidate/parsed/malformed/drop counters now cover both protocols. Added `cdp_candidates`, `cdp_parsed`, and `cdp_malformed` identify the CDP subset. Pool and queue drops remain shared; TTL expiration counts can include the hidden observation. Counters cannot detect frames discarded before the callback by MAC filtering or DMA. Per-protocol reception/update logs are limited to five-second intervals; aggregate statistics remain limited to ten-second intervals. CDP logs include device, port, TTL and optional platform, management IPv4, and native VLAN.

## API and OLED

`/api/switch-info` retains LLDP fields and existing omission conventions. Selected CDP returns `protocol:"CDP"` and the common identity, management IPv4, TTL, and optional `pvid`. Optional additions are `platform`, `cdp_version`, raw `cdp_capabilities`, and `duplex:"full"` or `"half"`. Snapshot-level additions report CDP availability/error and its three counters. Invalid snapshots omit neighbor details. Strings use the existing JSON escape path; worst-case bounded strings and maximum counters are exercised against the actual handler's chunk buffers.

The existing five OLED pages and navigation remain intact. CDP uses Device identity, Native VLAN, Peer duplex, Platform, and an eight-digit raw capability mask; enabled capabilities show N/A. Waiting text includes LLDP/CDP. The existing periodic snapshot/redraw model is retained. No SH1106 driver, font, I2C, or button change was made.

## Validation results

| Validation | Result |
| --- | --- |
| `python tests/verify_switch_info_parse.py` | PASS |
| `python tests/verify_switch_info_cdp.py` | PASS |
| `python tests/verify_switch_info_api.py` | PASS |
| `python tests/verify_switch_info_ui.py` | PASS |
| `python tests/verify_release.py` | PASS |
| `python tests/verify_autotest_rules.py` | PASS |
| `python tests/verify_discovery_logic.py` | PASS |
| `python tests/verify_fast_discovery.py` | PASS |
| `python tests/verify_link_loss.py` | PASS |
| `node tests/web_ui.test.cjs` | PASS |

Parser/selection fixtures evaluate actual production logic through the existing compile-time host harness, not a Python parser rewrite. Coverage includes all requested envelope/TLV fields and malformed cases, checksum variants, limits, 576 single-byte mutation cases, TTL/withdrawals, both protocol priorities and fallback, stale generation/epoch, RX forwarding/ownership/drops, actual setup behavior under filter/callback failures, OLED formatting, and API escaping/omissions. Some SDK/HTTP functions are mocked and host constexpr adapters are used. These results do not establish real FreeRTOS scheduling, hardware RX admission, OLED appearance, or physical networking behavior.

Normal `idf.py build` succeeded using ESP-IDF 6.1.0, GCC 15.2.0, and existing configuration. No Clang was used.

| Build item | Result |
| --- | --- |
| Version | `0.5.0-dev` |
| Application | 475,728 bytes (`0x74250`), +3,184 versus Phase C |
| Bootloader | 26,240 bytes (`0x6680`); 2,432 bytes allowance remaining |
| Each OTA slot | 1,740,800 bytes |
| Application OTA headroom | 1,265,072 bytes (`0x134DB0`), approximately 73% |
| C/C++ compiler warnings / build errors | None |

The initial configure output retains five SDK CMake private-include warnings and four pre-existing Kconfig invalid Boolean-default notices. They did not prevent the build; configuration was not altered to suppress them. Final build evidence is `phase_d_build.log`; initial configure evidence is `phase_d_build_initial.log`; test results are `phase_d_test_results.json` and per-script logs.

Final application SHA-256: `fd139ac015a0ef4eb3df5ed8ab8448a2c7134360d57833b04432313baed221e6`.

Map evidence: capture/context 11,152 bytes, worker parser scratch 1,504 bytes, HTTP snapshot 1,592 bytes, OLED snapshot 1,592 bytes: 15,840 bytes for those four static blocks, plus existing worker stack and RTOS overhead. Runtime free heap and stack high-water marks have not been measured.

## Scope and remaining risks

The before/after source integrity comparison confirms no Phase D changes to partitions, sdkconfig/defaults, DMA/ping patches, SH1106 driver, button GPIO, AutoTest, Fast Discover, network-generation producers, OTA authorization, root version configuration, or the existing LLDP parser. Partition CSV SHA-256 remains `41d8ef7960bf03647249e26ca1ace2b8d35f317a87f6c2ff5bc9f4bdbdbf602b`.

Ordinary IP frames retain the original forwarding path, and regression checks pass. Physical DHCP, AutoTest, Fast Discover, HTTP responsiveness, and OTA operation are still unverified with this image. Added static memory and worker CPU load are the practical residual risks. A CDP flood can exhaust the shared pool/queue and lose discovery advertisements, including LLDP, although RX remains nonblocking. Hardware filter capacity/admission, exact live Cisco checksum/optional-TLV compatibility, and runtime heap/stack remain unproven. Tagged CDP, other encapsulations, IPv6 display, and multiple-peer enumeration are outside this implementation.

## Later manual hardware validation — not performed

Perform deployment only after separate approval. Use an isolated test port and record its original configuration, advertisement intervals, and hold times before changing switch settings. Keep serial logs and repeated `/api/switch-info` responses with timestamps.

1. **Startup:** confirm LLDP and CDP filter-installed logs. Check API global availability and `cdp_available=true`, with successful setup errors. A CDP filter error must leave LLDP and ordinary IP usable.
2. **Cisco CDP only:** on the test interface, confirm CDP transmit is enabled using the platform's `show cdp`, `show cdp interface <port>`, and `show cdp traffic`. Temporarily suppress LLDP transmission on that port using its supported command. On IOS-style interfaces this is typically `no lldp transmit`; verify syntax for that device. Connect the instrument and wait at least three configured CDP intervals. Expect rate-limited `CDP: frame received len=...`, `neighbor updated device=... port=... ttl=...`, and optional platform/management/native-VLAN logs. Expect API/OLED protocol CDP and device/port/platform/IP/VLAN/duplex matching the switch configuration. Verify CDP candidates and parsed increase, malformed stays stable for valid advertisements, and data is bounded on all five pages. Confirm DHCP, AutoTest, Fast Discover, and local HTTP work during capture.
3. **Both protocols:** restore LLDP transmit while CDP remains enabled. Once LLDP is received, expect API/OLED LLDP continuously while CDP parsed continues to increase. Suppress LLDP transmit again without interrupting link; wait its last received TTL (or observe a matching shutdown advertisement). Expect fallback to still-fresh CDP. Restore LLDP and expect preference to return to LLDP. Suppress both transmit protocols, wait both TTLs, and expect waiting/invalid status with no stale neighbor fields. Restore original settings.
4. **Lifecycle:** unplug while a neighbor is visible; expect immediate invalidation. Reconnect to a different test peer and confirm neither old identity nor old queued advertisements reappear. Check generation/epoch rejection counters and new identity. Test a withdrawal only if the switch actually emits TTL zero; otherwise record it as fixture-validated only.
5. **Non-Cisco LLDP switch:** connect an LLDP-enabled port with no CDP. Expect unchanged LLDP identity, IPv4/PVID/capabilities where advertised, page navigation and expiration. Check CDP parsed does not grow. Repeat DHCP, AutoTest, Fast Discover, HTTP, and link-loss checks.
6. **Runtime/load:** record serial heap and stack statistics after sustained dual-protocol operation and ordinary HTTP/discovery activity. Inspect drop counters. If CDP is absent despite successful filter setup, obtain a wire capture proving untagged destination, declared length, SNAP signature, version/checksum and IDs before changing parser acceptance. This distinguishes missing advertisements, MAC admission, queue loss, and parser rejection.
7. **OTA:** physical authorization and OTA must be validated in a separately approved deployment test; no OTA action is included in this phase.

Record every physical result explicitly, including switch model/software, port configuration, observed fields, failures, and packet capture if available. None of these steps has been claimed as completed.

## Protocol verification sources

The installed ESP-IDF 6.1 source was inspected for filter API/ownership behavior. CDP encapsulation, TLV meanings, address envelopes and checksum treatment were checked against primary implementations: [Wireshark CDP dissector](https://raw.githubusercontent.com/wireshark/wireshark/master/epan/dissectors/packet-cdp.c), [tcpdump CDP dissector](https://raw.githubusercontent.com/the-tcpdump-group/tcpdump/master/print-cdp.c), and [lldpd CDP implementation](https://raw.githubusercontent.com/lldpd/lldpd/master/src/daemon/protocols/cdp.c). Cisco operational checks are described in the [Cisco CDP command reference](https://www.cisco.com/c/en/us/td/docs/ios-xml/ios/cdp/command/cdp-cr-book/cdp-cr-a1.html). Source inspection and fixtures verify the implemented software behavior; successful physical multicast reception remains an assumption requiring the tests above.
