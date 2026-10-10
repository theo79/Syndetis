# Syndetis 0.6.0-dev — Phase B: Hostname enrichment

Implemented and validated for review on 2026-10-10. Firmware remains `0.6.0-dev`. No flash or OTA was performed. Phase B hostname success has not been observed on hardware. The user supplied Phase A hardware evidence (four hosts in 14,630 ms, vendor lookup 111 us, free heap 146,584 bytes, worker watermark 2,764 bytes); it is baseline evidence, not a measurement of this implementation.

## Files

Added firmware:

- `main/discovery_name.h`: bounds, hostname validation and lifecycle-safe record publication helper.
- `main/discovery_names.h`, `main/discovery_names.c`: aggregate status and bounded PTR enrichment worker routine.
- `main/ptr_dns.h`, `main/ptr_dns.c`: isolated, bounded IPv4 PTR wire codec.

Modified firmware: `main/discovery.c`, `main/discovery.h`, `main/CMakeLists.txt`, `main/ui_menu.c`, `main/web_server.c`, `main/web_index.html`.

Added test: `tests/verify_hostname.py` (production codec, model and transport fixtures).
Modified tests: `tests/verify_enhanced_discovery.py` (getter extraction boundary follows the new function), `tests/verify_enhanced_api_ui.py`, `tests/web_ui.test.cjs`.

Added review evidence: this report, `phase_b_baseline.json`, `phase_b_integrity_results.json`, `phase_b_test_results.json`, build logs and individual validation logs. Generated build artifacts changed. Existing worktree changes were preserved; the overall Git diff includes earlier phases.

## API selection: installed SDK limitation and necessary fallback

The installed ESP-IDF v6.1.0/lwIP source was inspected before implementation:

- `components/lwip/lwip/src/core/dns.c`: `dns_send()` constructs A/AAAA queries; response handling accepts those address types.
- `components/lwip/lwip/src/include/lwip/dns.h`: callbacks return addresses for forward hostname lookup, with no PTR/reverse lookup API.
- `components/lwip/lwip/src/include/lwip/netdb.h`: forward gethostbyname/getaddrinfo facilities; no gethostbyaddr implementation.
- `components/lwip/port/include/netdb.h`: a `getnameinfo()` declaration, but no implementation found in component sources.
- Symbol inspection of the actual built `build/esp-idf/lwip/liblwip.a` and installed ESP32 `picolibc/.../lib/esp32/libc.a` found neither `getnameinfo` nor `gethostbyaddr`.
- AutoTest's existing DNS callback path resolves forward A records; querying an `in-addr.arpa` name through that API would still ask for A, not PTR.

Therefore the request's exception for an absolutely necessary custom DNS parser applies. The selected supported transport is lwIP BSD UDP sockets (`socket`, nonblocking `ioctl(FIONBIO)`, `bind`, `connect`, `send`, `recv(MSG_DONTWAIT)`, `close`) from the existing worker. Only the unavailable PTR query/response codec is implemented locally. SDK internals, DNS cache and configuration are not patched. This is not a claim that the installed SDK has a supported PTR resolver.

The codec sends ordinary recursive IN/PTR queries to one configured DNS server. It does not send to discovered hosts, multicast groups, public fallback servers, or service ports. A connected UDP socket filters the source DNS endpoint; replies must also match the random transaction ID, original reverse question, IN class and direct PTR owner. It bounds packet size at 512 bytes, answers at 32, name expansion at 253 characters and compression traversal at 128 steps. Backward DNS compression is supported; loops, forward/out-of-range pointers, incomplete records and invalid names are rejected. Truncated replies do not trigger TCP fallback. CNAME traversal and IPv6 PTR are deliberately unsupported.

## Architecture and limits

The existing ARP/vendor/ICMP phases are unchanged. `finish()` publishes `DISC_COMPLETE`, core completion timestamp and duration, and logs the core result **before** calling hostname work. OLED and HTTP can immediately copy the core devices while names are pending. Each valid answer updates the same existing record; ordering remains numeric IPv4.

Hostname work runs in the existing discovery task. No new task, queue, full device table or blocking DNS resolver is introduced. DNS network waits are nonblocking receives with 10 ms worker polling; Ethernet RX, TCP/IP callbacks, UI, HTTP and event callbacks do not perform hostname resolution. State locks are held only for bounded local publication/copies.

| Limit | Value |
| --- | ---: |
| Concurrent requests | 2 UDP sockets |
| Global enrichment budget after core publication | 2,500 ms |
| Per-request expiry | 700 ms |
| Request emission spacing | At least 50 ms |
| Maximum queries per scan | 16, first hosts in numeric IPv4 order |
| Retries / server failover after an attempt | None |
| Hostname storage | Existing 64 bytes, at most 63 visible characters |
| UDP DNS payload | At most 512 bytes |

The existing `network_status_inspect()` supplies current DNS settings using its protected, TCP/IP-thread-safe, generation-aware inspection. Its up-to-one-second callback wait is included in the 2,500 ms total budget. If no usable IPv4 DNS server is present, the pass ends without sending queries or failing discovery. Main DNS is preferred; backup is selected only if the main address is unusable, not as a retry. Other hosts beyond the attempt cap/global budget remain visible without names.

The budget is a deadline checked by the worker; actual wall-clock completion includes OS scheduling, one poll and short SDK socket-control/cleanup calls. There is no wait for a DNS network response beyond a request/global deadline. Core scan duration excludes the name pass.

The diagnostic reservation is retained through this short pass. Starting another Discover or AutoTest during enrichment uses the existing busy behavior, then succeeds once sockets close and the reservation is released. This avoids a second lifecycle model and competing DNS work. It is asynchronous relative to result consumers, not a new background task.

## Lifecycle and failure behavior

Every result carries the original diagnostic/discovery token, network revision and target IPv4. `name_current()` requires the original token, live link, ready IPv4, current network revision and no active OTA receive/success. `discovery_hostname_result()` rechecks token/network and requires the still-current completed result set before applying a validated name to a valid matching IP.

Duplicate answers cannot create devices or overwrite a previously accepted name. Result order need not match request order. Link loss, IP loss, revision change, coordinator invalidation or OTA activity stops the pass. All open sockets close before the existing worker/diagnostic reservation can be reused; there are no outstanding DNS callbacks, callback storage or receive queues to leak into a second scan. Existing getters already mask results from a stale network revision.

No PTR, timeout, malformed response, no DNS or setup failure changes valid/MAC/vendor/ping fields or removes a host. Link loss invalidates the overall old-context view according to the existing discovery lifecycle; DNS failure itself does not fail a scan.

## Validation and presentation

`discovery_name_valid()` preserves case and legitimate labels, including underscores, and removes one trailing root dot. It rejects empty names, consecutive/empty labels, leading/trailing hyphens, spaces, control/non-ASCII characters, HTML/quote/slash text, and names longer than 63 characters after root-dot normalization. Overlong identities are rejected rather than truncated into a different DNS name. Storage is explicitly terminated; rejected controls are never displayed. No vendor/MAC/IP/LLDP/CDP-derived name is fabricated.

Preferred OLED name is a valid hostname if present, otherwise IPv4. With a hostname, rows are:

```
DEVICE 2/5
plc-line-01
192.168.1.50
AA:BB:CC:DD:EE:FF
Phoenix Contact
RTT: 2 ms
UP/DOWN: DEVICE
OK: BACK
```

Without a name, the Phase A IPv4/MAC-label layout remains. Hostname and vendor are bounded to 21 display characters; full stored names remain available in JSON/web. Invalid hostname text falls back to IPv4. UP/DOWN host wrapping and OK/list/back behavior are unchanged. No extra details page, scrolling or button behavior was added.

Browser columns are Name, IP, MAC, Vendor, ICMP, RTT. Missing Name is `-`; all values use `textContent`. The existing two-second refresh and OTA/browser functions remain unchanged.

The API's existing optional `hostname` field now receives actual validated PTR results and remains omitted when unavailable. Old IP/MAC/vendor/reachable/latency/ARP/ping fields retain their meanings. String escaping is unchanged. The bounded response scratch grows from 1,024 to 1,536 bytes to accommodate aggregate status plus a maximum escaped error; the response still streams records, without a full response allocation.

Aggregate API fields: `hostname_enriching`, `hostname_attempted`, `hostname_resolved`, `hostname_timeout`, `hostname_failed`, `hostname_duration_ms`, `hostname_packets_sent`, `hostname_cancelled`. Resolved count updates as names are accepted; final attempted/failure/timeout/packet/duration totals are committed at the end. Attempted includes setup failures; packets_sent counts successful query sends. Failed includes no PTR/malformed/setup failure and pending requests discarded on cancellation. Timeout includes request/global-budget expiry. Unattempted hosts are not failures.

Serial logs give attempted/resolved/timeout/failed/duration/cancelled plus final free heap, minimum free heap and existing worker stack watermark. Missing PTR produces no per-device warning.

## Performance and memory

User-supplied Phase A runtime core baseline: approximately 14.63 seconds for four hosts. This phase leaves ARP pacing, settling, candidate scope, ping concurrency and retirement unchanged. Core devices are published before PTR work. No Phase B hardware duration/name success is claimed.

Deterministic production-loop fixtures calculate:

- Five silent DNS targets finish within the small global budget (700 ms per concurrent request; not five long sequential DNS waits).
- A large silent set stops at exactly 2,500 simulated ms, closes all sockets and respects the query/concurrency caps.
- Immediate negative answers stop at 16 attempts; absence of DNS sends zero packets.
- Out-of-order answers update their original IPs; cancellation closes all sockets before another run.

At most 16 outgoing DNS query datagrams per scan, paced at no more than 20/second. No DNS retries. DNS responses and any underlying ARP/link-layer traffic depend on the LAN and are not included in the query count. Runtime packet/time/heap values remain to be measured.

| Storage / build item | Effect |
| --- | ---: |
| Static DNS packet scratch | 512 bytes |
| Aggregate status appended to discovery state | 28 bytes |
| Total new application static storage | 540 bytes |
| Request-state object | 24 bytes, worker stack |
| Two request-state objects | 48 bytes, worker stack |
| New tasks / application queues | 0 / 0 |
| Device record size | Unchanged, 128 bytes |
| Extra complete device tables | 0 |
| HTTP response scratch increase | 512 bytes, existing HTTP task stack |
| Application binary | 484,512 bytes |
| Binary increase over Phase A | 7,184 bytes |

Target GCC asserts request/status sizes; compiled `libmain.a` symbols confirm 512-byte packet scratch and discovery state grows from 148 to 176 bytes. Parser/worker local buffers additionally use stack; no task stack size was changed. The existing discovery worker is 4 KiB and HTTP task 12 KiB. Actual worker watermark must be checked on hardware.

There is no application heap allocation for names/requests. Up to two sockets use SDK netconn/UDP/socket/receive-buffer heap resources transiently; their exact live allocation depends on SDK/runtime traffic and is not claimed as measured. Every socket is closed on success/failure/expiry/cancellation. Expect the extra 540 static bytes plus transient SDK socket resources to reduce available RAM; verify recovery and minimum heap using the new logs. No runtime heap margin is inferred from the Phase A watermark.

## Tests and build

`verify_hostname.py` evaluates the production PTR builder/parser, name validator/publication helper and entire transport loop with target GCC constexpr fixtures. Socket/config/time/lifecycle SDK calls are mocked; this is not hardware execution. Coverage includes positive/negative PTR, no DNS, missing response, packet truncation at every boundary, wrong ID/IP question, maximum/overlong/empty/control/malformed names, trailing dot, backward compression/self-pointer rejection, null arguments, duplicate result, stale session/network/link/coordinator, missing device, correct-IP updates, unchanged MAC/vendor/ping, reverse completion order, two-request bound, socket/setup busy/failure, global/per-request budgets, cancellation and cleanup before reuse.

Expanded API/OLED/browser tests verify escaped/absent/maximum names, maximum aggregate/error response bounds, 254 device responses, hostname/vendor together, control fallback, no-reply hostname layout, Name column and safe text insertion. Existing route and Phase A tests remain.

| Validation script | Final result |
| --- | --- |
| verify_switch_info_parse | PASS |
| verify_switch_info_cdp | PASS |
| verify_switch_info_api | PASS |
| verify_switch_info_ui | PASS |
| verify_enhanced_discovery | PASS |
| verify_enhanced_api_ui | PASS |
| verify_autotest_rules | PASS |
| verify_discovery_logic | PASS |
| verify_fast_discovery | PASS |
| verify_link_loss | PASS |
| verify_hostname | PASS |
| web_ui | PASS |
| verify_release | PASS |

All 13 scripts pass, exit zero. One existing getter fixture initially included the newly adjacent hostname function; its extraction boundary was corrected. Final test results/logs reflect passing fixtures. No source change was made to bypass a product failure.

Normal `idf.py build`: PASS, exit zero. Installed ESP-IDF v6.1.0, GNU GCC 15.2.0 (esp-15.2.0_20251204), explicit `IDF_TOOLCHAIN=gcc`, process-local `PYTHONUTF8=1`. No Clang or configuration changes. Configure/build log: `phase_b_build.log`; final build log: `phase_b_build_final.log`.

Application: 484,512 bytes. Bootloader: 26,240 bytes (2,432 bytes remaining in its allowance). Each OTA slot: 1,740,800 bytes. Headroom: 1,256,288 bytes per slot, approximately 72.17%.

Image SHA-256: `a45e5415db41a7a30cb9f821fa633d56aa2f20abc70d05934fb49bf987bdb3e5`.

Compiler warnings/errors: none in successful logs. Configure retains five existing SDK WiFi/WPA private-include warnings and four SDK Kconfig invalid boolean `default 0` notices, treated as `n`.

Baseline hashes show only the six listed existing firmware files changed. OUI/vendor/model, network lifecycle, coordinator, AutoTest, RX/LLDP/CDP/Switch Info backend, OLED/button driver, OTA authorization, DMA/ping patches, sdkconfig/defaults, root CMake and version are byte-identical to Phase B baseline. Partition bytes are unchanged, SHA-256 `41d8ef7960bf03647249e26ca1ace2b8d35f317a87f6c2ff5bc9f4bdbdbf602b`. Release verification confirms 0.6.0-dev, image descriptor/checksum/SHA-256 and both existing OTA partitions.

## Exact home-LAN validation after separately approved flashing

1. Only after a separately approved installation, start Syndetis on the normal wired DHCP LAN. Open the normal serial monitor on the known device port. Verify About/web still says 0.6.0-dev; this phase does not issue any flash command.
2. Note router/client IPs and MACs, configured DNS IPv4 and the supplied Phase A core baseline. Start Discover physically: Discover, OK to enter, OK to start.
3. Record `ARP sweep`, `ARP settle`, `vendor lookup`, `ICMP enrichment`, and `completed ... hosts in ... ms`. Confirm core devices appear when completion is published, before name enrichment finishes; `duration_ms` should be the core measurement.
4. Record the subsequent `hostname enrichment` attempted/resolved/timeout/failed/duration and final free/minimum heap and worker watermark. Missing PTR is normal. Confirm total name work remains approximately within 2.5 seconds; investigate greater delays with serial/network observations rather than treating baseline timing as a hard target.
5. Compare every discovered IP/MAC with the router and Phase A behavior. All ARP hosts must remain visible when names fail or time out, including ICMP no-reply devices. Sleeping DHCP leases and the device's own IP can explain count differences.
6. For any resolved name, compare with the router/device identity. On Windows, independently query the same configured DNS server using `nslookup <discovered-ip> <configured-dns-ip>`. A router UI name need not have a PTR record. If the normal LAN has no PTR entries, record zero resolutions as expected; a real positive PTR test remains pending until a known PTR-capable DNS environment is available.
7. On OLED select Devices and a host. A resolved name should occupy the first detail row, followed by IP, full MAC, vendor and RTT/no-reply. Unresolved devices keep the familiar layout. Exercise UP/DOWN wrap and OK/back; inspect a long name if a real PTR record is available.
8. Open `http://<syndetis-ip>/`. Confirm Name/IP/MAC/Vendor/ICMP/RTT columns and two-second refresh. Names should appear as answers arrive; missing Name is `-`. Do not expect a name for a vendor alone.
9. Fetch `http://<syndetis-ip>/api/discovery`. Verify `state=complete`, unchanged core fields, optional real `hostname`, aggregate metrics and no fabricated names. During the short pass `hostname_enriching` may be true; final counters are committed at its end. Compare hostname case/root-dot normalization with the DNS answer.
10. Repeat Discover at least three times after each pass releases the coordinator. Save core time, name time, counts, packets_sent, free heap, minimum heap and watermark. Check fixed ordering, correct name-to-IP association and no declining recovered free-heap trend. More than 16 hosts can correctly leave later hosts unattempted.
11. To test cancellation, unplug Ethernet immediately after the serial `completed ... hosts` line, while hostname_enriching is true if observed. Confirm the name pass reports cancellation, old-context records disappear through the existing lifecycle, and the worker/coordinator releases. Repeat if the home DNS replies too quickly to catch the window.
12. Reconnect, wait for DHCP, run a fresh Discover. Compare names against current IP/PTR data; no name from an old result set may be attached to a different/new context. Repeat a scan as soon as the coordinator is available to exercise cleanup/reuse.
13. If the LAN naturally lacks DNS/PTR or returns NXDOMAIN, record that host visibility and Phase A vendor/MAC/ping data remain intact. To demonstrate an unreachable-server timeout case, use a separately controlled test network/DNS configuration; do not change firmware sdkconfig or normal router settings as part of this implementation.
14. Run AutoTest after hostname enrichment and verify existing link/DHCP/gateway/DNS behavior. During the short retained reservation it may report diagnostic busy; retry after release.
15. Check Switch Info and its existing pages/UP/DOWN/OK behavior. A switch without LLDP/CDP can correctly report no neighbor; verify an advertising neighbor where available. PTR names must not be populated from Switch Info.
16. Save serial logs, router/DNS comparison notes and JSON for all runs. Report any missing hosts, wrong IP/name pairing, core-time regression, name-budget overrun, heap decline or low watermark before accepting runtime success.

## Deferred and review limits

No mDNS/NBNS/LLMNR/SSDP discovery, PROFINET DCP, SNMP, port/service scans, Modbus, S7, EtherNet/IP or role inference was added. IPv6 PTR, DNS-over-TCP/CNAME following, retries, broader DNS discovery and enrichment of hosts beyond the bounded first-16 pass are deferred. Real PTR success, LAN packet counts, runtime heap recovery and stack margin remain hardware validation tasks. Stop here for review; no flash/OTA has been performed.
