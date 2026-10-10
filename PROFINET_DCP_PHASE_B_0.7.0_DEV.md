# Syndetis 0.7.0-dev — Phase B implementation report

Date: 2026-10-10. Identify-only implementation, GCC build and software checks complete. Physical PROFINET acceptance remains pending. No flash or OTA was performed. This is a lab prototype, not a conformance claim.

## 1–2. Files

Added: `main/profinet_dcp.h`, `main/profinet.h`, `main/profinet.c`, `main/ethernet_rx.h`, `main/ethernet_rx.c`, `main/ui_profinet.h`, `main/ui_profinet.c`, `tests/profinet_dcp_test.cpp`, `tests/verify_profinet.py`, this report and evidence logs/JSON.

Modified for this phase: `main/CMakeLists.txt`, `main/app_main.c`, `main/network_status.h/.c`, `main/switch_info.h/.c`, `main/ui_menu.c`, `main/web_server.c`, `main/web_index.html`, `tests/web_ui.test.cjs`, `tests/verify_switch_info_api.py`, `tests/verify_switch_info_parse.py`, `tests/verify_switch_info_ui.py`, `version.txt`, README. Pre-existing working-tree changes were retained. The portable builder/parser use internal-linkage inline functions in one header rather than a separate compilation unit; fixtures evaluate the same production bodies.

## 3. Shared RX router

`ethernet_rx.c` installs the sole application input callback before Ethernet starts. Switch Info has an independent bounded capture entry; PROFINET has another. A consumer returning true owns and frees the original buffer once. Every other buffer is passed to `esp_netif_receive` with the original pointer, length and netif ownership. LLDP/CDP pool, parsing, filter setup and worker remain independent. PROFINET installs no additional multicast filter. Router failure is nonfatal and leaves default netif receive glue; optional PROFINET stays unavailable.

## 4–5. TX callback and lifetime

The only application raw `esp_eth_transmit` call is inside `transmit_on_tcpip`, submitted with nonblocking `tcpip_try_callback`. Existing lwIP TX and this request therefore run on the TCP/IP task. Compile guards reject enabling ETH transmit mutex, lwIP core locking or L2 TAP without review; existing settings remain unchanged. No direct worker TX, retry or lock is added around the SDK transmit.

One permanent 60-byte job holds XID, diagnostic token, physical epoch, network revision, deadline and result. Worker wait is at most 1000 ms. Cancellation/timeout never frees or reuses a pending job; a new scan stays busy until the callback retires. A late callback validates deadline and lifecycle and does not transmit. No second callback is queued. An already-entered SDK transmit cannot be undone if link/OTA changes concurrently; subsequent collection and publication reject that context.

## 6. Fixed Identify request

60 bytes including zero padding, 30 meaningful bytes, excluding hardware FCS:

| Offset | Length | Value |
|---|---:|---|
| 0 | 6 | `01 0E CF 00 00 00` |
| 6 | 6 | driver MAC from `ETH_CMD_G_MAC_ADDR` |
| 12 | 2 | EtherType `88 92` |
| 14 | 2 | FrameID `FE FE` |
| 16 | 1 | Identify service `05` |
| 17 | 1 | Request type `00` |
| 18 | 4 | nonzero XID, big endian |
| 22 | 2 | response delay factor `00 80` |
| 24 | 2 | data length `00 04` |
| 26 | 4 | All selector `FF FF 00 00` |
| 30 | 30 | zero padding |

The private builder accepts only source MAC, XID and output storage. No configurable service, Set, Get, Hello or generic command path exists.

## 7–9. XID, lifecycle and start

XID is randomly seeded at initialization and incremented per accepted scan, skips zero and reseeds on wrap while idle. A diagnostic reservation token also gates callbacks and publication. A separate `physical_epoch` advances on connected/disconnected/stopped events. Existing network revision and discovery link generation semantics remain unchanged. Link loss, epoch/revision change, diagnostic token mismatch, explicit cancel or OTA invalidate collection and clear published device count. DHCP/IP changes invalidate through revision. Completed results are also hidden after context changes.

Start requires available scanner, valid driver MAC, Ethernet link, coordinator reservation, retired TX job, no OTA and cooldown expiry. No IPv4, gateway, DHCP or netif-address requirement is introduced. Public HTTP has no start endpoint.

## 10–11. Classifier and VLAN

Classifier requires destination equal to our MAC, valid unicast source, EtherType 8892, response FrameID FEFF, Identify service 05, successful response type 01 and a complete advertised data envelope. Session matching additionally requires active collection, matching XID/token/epoch/revision and receive deadline. Nonmatching traffic is forwarded; wrong XID is counted. One 8100 VLAN tag is accepted and its VID/PCP retained in capture metadata. Provider tags and nested tags are forwarded. TX is untagged. VLAN metadata is not represented as a configured device VLAN or advertised DCP field.

## 12–14. Bounds and worker

Independent eight-slot capture pool, 1600 bytes per frame, eight-entry queue of slot indices; 128 matching candidate cap. RX copies bounded data and queues without waiting, never parses blocks or allocates heap. Oversize, exhausted pool/queue and candidate-cap drops are counted; overflow marks results partial. Original matching buffers are still freed once on these drops.

Permanent priority-1 worker, 4096-byte stack. Collection starts at actual accepted TX time, lasts 3000 ms, followed by at most 250 ms queue drain. Worker yields between processed frames. Cooldown is 10 seconds from accepted start. No retries, background scans, periodic Identify or boot-time Identify. Idle worker reclaims stale queued slots without resetting producer-owned storage.

## 15–18. Model and parser

32 source-MAC-keyed records, **408 bytes each**, target C static assertion. Table is 13,056 bytes. Station name stores 240 bytes plus NUL; vendor value and alias each store 63 bytes plus NUL. Validity/presence, truncation and sanitization are explicit flags. Optional IP/subnet/gateway, VendorID/DeviceID, role, instance and IP BlockInfo are preserved. Zero IP is represented as advertised-but-unset, not missing. Vendor text is the advertised value; there is no invented product name or OUI substitution.

Supported blocks: IP MAC 01/01 (source cross-check), IP parameters 01/02, device vendor text 02/01, station name 02/02, IDs 02/03, role 02/04, supported options 02/05 (presence only), alias 02/06, instance 02/07. Unknown/OEM blocks skip by their checked length. BlockInfo is interpreted only for known blocks; supported-options entries are validated as pairs. IP conflict statuses 80/81/82 are exposed.

Truncated headers/data, invalid known lengths, missing/nonzero odd padding and malformed options reject the entire response before merge. Parsing uses one scratch record; failed parsing never modifies the device table. Nonprintable bytes become `?`, bounded overlong text is truncated with flags. The first duplicate known block wins within a frame and duplicates are counted. Later valid frames merge only present fields for the same source MAC. First 32 unique MACs are retained, no eviction; excess devices mark partial and increment a counter. Complete records sort by MAC after admission ends.

## 19. Counters and logs

API includes scan attempts, callback posted/accepted/failed/timeout, candidates, accepted, parsed, malformed, wrong-XID, stale, oversize, pool-full, queue-full, candidate-limit, device-limit and duplicate-block counters. Counters saturate; attempts persist across scans, remaining counters reset per scan. Exposes packets sent, duration, state, partial, XID/context identifiers, TX error, free/minimum heap and worker stack watermark. Logs identify start/TX acceptance, rate-limited responses (at most two per second), final state/count/partial/drop statistics and heap/stack.

## 20–21. OLED and API/web

Industrial → PROFINET → Scan / Results / Back. Only Scan starts an operation. OK during scanning cancels and remains on the progress page until cleanup/coordinator release finishes, then returns to menu. Results distinguishes zero replies, partial results, failure and changed network. Device navigation spans three bounded pages per record: summary, network and identity; no field is invented. OTA retains display priority.

Read-only `GET /api/profinet` streams a bounded snapshot with optional fields as null, escaped text and explicit flags/counters. Existing HTTP stack size is unchanged. A fixed 32-record HTTP snapshot avoids a large automatic array. Web refreshes via GET and builds cells with `textContent`; no HTML interpretation or configuration controls. Existing endpoints and OTA flow remain intact.

## 22–23. Software verification

New compiler-evaluated production fixtures cover exact request bytes, malformed/truncated data, known/unknown blocks, padding, duplicates, text bounds/sanitization, VLAN envelopes, mutation cases, field merging, 32-device cap/sorting, lifecycle gates, no-IP start, callback posting failure, descriptor error, cancellation/context changes, timeout quarantine/late retirement, cooldown, eight-slot exhaustion, queue/oversize/candidate drops, stale slot rejection, shared-router ownership, router installation failure, API escaping/maximal 32-device output, nullable fields, OLED bounds, and actual local menu navigation/cancel behavior.

Fixtures are synthetic with explicit construction; none are presented as captured real-device frames. GCC C++ constexpr evaluation runs production bodies with SDK/RTOS boundary doubles and bounds checking. This does not exercise real scheduling, DMA or devices. Existing Switch Info fixtures were adapted to the shared-router boundary and unchanged assertions retained; handler extraction ends before the new handler. Navigation expectation now routes Industrial to its implemented submenu.

**All 15 checks passed:** verify_autotest_rules, verify_discovery_logic, verify_enhanced_api_ui, verify_enhanced_discovery, verify_fast_discovery, verify_hostname, verify_icmp_degradation, verify_link_loss, verify_profinet, verify_release, verify_switch_info_api, verify_switch_info_cdp, verify_switch_info_parse, verify_switch_info_ui, web_ui.test.cjs. Evidence: `profinet_phase_b_test_results.json`, individual `profinet_regression_*.log`. The final PROFINET menu extension was separately rerun and passed after the complete suite.

## 24–28. Build, sizes, integrity and warnings

Normal `idf.py build`, ESP-IDF 6.1, ESP32 GCC 15.2 toolchain succeeded. Windows sandbox DLL relocation restrictions required running the installed toolchain with approved elevated filesystem access. No SDK source changes were made. Final build log: `profinet_phase_b_build_final.log`; memory report: `profinet_phase_b_size.log`.

| Measurement | Result |
|---|---:|
| Application binary | 497,984 bytes (`0x79940`) |
| Phase B input application | 485,344 bytes |
| Increase | 12,640 bytes |
| Bootloader | 26,240 bytes |
| Each OTA app slot | 1,740,800 bytes (`0x1A9000`) |
| Free per OTA slot | 1,242,816 bytes (`0x12F6C0`), 71% |
| Final DRAM | 141,817 bytes: BSS 130,032, data 11,785 |
| DRAM region remaining reported by size tool | 38,919 bytes |
| IRAM | 51,003 bytes |

Target object symbol sizes: scanner context 26,464 bytes (includes table and pool), parser scratch 408, HTTP snapshot 13,056, OLED device copy 408. These principal new fixed objects total 40,336 bytes, plus small locks/counters/epoch/metadata. Pool frame payload alone is 12,800 bytes. Additional dynamic allocation is one 4096-byte worker stack plus RTOS TCB/queue allocator overhead; exact recovered heap impact requires hardware measurement. HTTP escaping uses bounded temporary storage; real HTTP/worker stack margins must be measured. Map-region remaining bytes are not a runtime free-heap measurement. Compared with the supplied pre-phase free-heap baseline around 144,864 bytes, a roughly 45 KiB decrease is expected; this is an estimate, not observed hardware data.

Application SHA256: `4d77dbb52e5584e3a81f5304cdf1b61a2f07042c4f573c69b302270e9763a14e`.

Partition SHA256 unchanged: `41d8ef7960bf03647249e26ca1ace2b8d35f317a87f6c2ff5bc9f4bdbdbf602b`.

Input-baseline hash comparison confirms unchanged sdkconfig, sdkconfig.defaults, root CMake, all existing patches, discovery/OUI/PTR/ICMP implementation, OTA implementation and unrelated tests. `profinet_phase_b_source_baseline.json` and `profinet_phase_b_source_changes.json` preserve evidence. Version is 0.7.0-dev. Existing working-tree/build-artifact differences from older Git history were not reset. Final firmware build has **zero compiler warnings/errors**. Initial misleading-indentation warnings and an overlong OLED literal were fixed; fixture-adapter warnings were also fixed. Git line-ending notices are not compiler warnings.

## 29. Remaining hardware risks

Real-device acceptance of untagged Identify and chosen response delay, single-tag responses, filter behavior, scheduler timing, loss during bursts, coexistence under sustained Ethernet load, heap recovery and stack margins remain unverified. An in-flight TX race must be assessed with actual link/OTA events. Synthetic fixtures cannot prove EMAC DMA behavior, device interoperability, complete PCP/conformance requirements or absence of all task races. No physical support or certification is claimed.

## 30. Later manual acceptance procedure

1. After separate deployment approval, use an isolated authorized test network, one known PROFINET Siemens/Phoenix/other device, Syndetis and a PC with TIA or the applicable vendor discovery tool. Record model, firmware, wiring, MAC, configured station/IP and tool results. Keep configuration writes disabled throughout this test.
2. If available, mirror the Syndetis switch port or use a TAP. Capture with Wireshark `eth.type == 0x8892 || vlan.etype == 0x8892`; save the entire pcap and serial logs. Record pre-scan free/minimum heap and task watermarks; run AutoTest and Discover as controls.
3. Choose Industrial → PROFINET → Scan once. Prove exactly one outbound Identify frame: multicast 01:0e:cf:00:00:00, actual source MAC, 8892/FEFE/05/00, nonzero XID, delay0080, dataLen0004, selectorffff0000, 60 bytes without FCS. Prove no Get, Set or Hello. Check unicast FEFF/05/01 replies to Syndetis with matching XID, and compare station, IDs, role and advertised IP/mask/gateway/vendor text with capture and vendor tool.
4. Repeat without a DHCP server: Ethernet link alone must permit Identify. Separately use an already-unconfigured/zero-IP responder; show advertised zero IP as unset and retain its MAC/name/IDs. Do not change its configuration through Syndetis.
5. If an available device/network emits exactly one 8100-tagged response, record VID/PCP and compare parser results. Replay captured malformed/nested/provider-tag cases only in the isolated lab; verify they are forwarded/rejected without false records. Mark tagged hardware coverage pending if no real tagged responder is available.
6. Start again inside 10 seconds and verify cooldown, then after expiry. Perform at least 20 scans, including zero-responder scans. Verify one request per accepted start, stable recovered heap, bounded stack watermarks, complete-zero results, no growing allocations and no retries.
7. Connect several known devices for a response burst; compare unique source MAC count and fields against the vendor tool/capture. If safe lab traffic exceeds pool/candidate/table limits, verify explicit counters/partial indication, stable HTTP/OLED responsiveness and next-scan recovery. Do not infer full discovery from partial results.
8. Press OK during collection; confirm bounded cleanup, coordinator release, cleared partial devices and no late publication. Remove cable during callback wait and during collection; verify network_changed, cleared IP/results and safe recovery after reconnect. Trigger DHCP address/revision change mid-scan and verify invalidation. Exercise delayed TCP/IP callback under controlled load and confirm timeout/quarantine prevents job reuse and late TX.
9. Verify AutoTest/Discover block concurrent local starts. Test an approved OTA operation during collection only with a reviewed valid image and deployment authorization; verify cancellation and coordinator release before OTA priority display. This step is not performed in Phase B. Verify no late PN TX after an OTA cancellation gate.
10. Send/observe LLDP and CDP before, during and after scans; verify Switch Info still refreshes. Exercise web status and normal ARP/IP/DHCP traffic concurrently, then rerun AutoTest/Discover. Compare heap recovery, minimum heap and all relevant task watermarks with initial measurements; investigate any progressive loss or inadequate margins before accepting.
11. Archive pcaps, serial logs, tool screenshots/results and device provenance. Acceptance requires exact packet proof, field agreement, lifecycle/ownership behavior and stable memory. Record unavailable cases explicitly; do not promote 0.7.0-dev to a final release on software tests alone.

Stopped after implementation/build/test for review. No flash, OTA, DCP Set/Get/Hello or configuration-changing operation was executed.
