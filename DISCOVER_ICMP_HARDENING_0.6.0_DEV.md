# Discover ICMP reliability hardening — Syndetis 0.6.0-dev

Implemented and GCC validated on 2026-10-10. No flash or OTA performed. Version remains 0.6.0-dev. The supplied successful hardware results and intermittent failures are user evidence; this change has not been hardware tested.

## Files

Modified firmware: main/discovery.c, main/discovery.h, main/web_server.c, main/web_index.html, main/ui_menu.c, patches/ping_sock.c.
Modified tests: tests/verify_enhanced_api_ui.py, tests/web_ui.test.cjs.
Added: tests/verify_icmp_degradation.py, this report, baseline/integrity/test-result JSON and individual build/test logs prefixed icmp_hardening_. Generated build artifacts changed; earlier working-tree changes were preserved.

## Exact failure path and graceful degradation

Previously, after ARP/vendor found N hosts, esp_ping_new_session called socket(AF_INET, SOCK_RAW, IP_PROTO_ICMP). A negative descriptor produced the SDK's create socket failed message; SDK partial-allocation cleanup returned failure without publishing a handle. Discovery set Ping session creation failed, then called finish(error). Fatal finish set DISC_ERROR and cleared state.found=0. Hostname enrichment only ran on successful finish. Current-batch measured results were also skipped because copying required no error.

Now, ICMP is optional enrichment. Creation/start/send/completion/ICMP time-budget failure stops further ping setup for that scan and logs one degradation summary. Each completed session is harvested and published even if another session fails. All ARP records survive, including MAC/vendor/private/last-seen/source. Remaining hosts are unmeasured. Core finish succeeds and the unchanged PTR phase runs. Link/network/OTA cancellation remains fatal and continues to reject stale-context data.

Normal ARP -> vendor -> ICMP -> core publication -> PTR ordering is unchanged. ARP pacing/range/settling/order, OUI lookup and hostname architecture are unchanged.

Total failure: five ARP hosts plus first-session creation failure yields five completed devices, all unmeasured, PTR execution and eventual coordinator release.
Partial failure: two completed measurements plus third creation failure keeps both real measurements and all five host records. The remaining hosts are unmeasured; PTR runs. Earlier batches also remain untouched.

## Three measurement states

Measurement now uses ESP_PING_PROF_REQUEST after the end semaphore and before deletion. Installed esp_ping_send increments transmitted only if sendto returns the complete packet length. Creating/starting a session alone does not prove a probe was sent.

| State | API | Web | OLED |
| --- | --- | --- | --- |
| Transmit + reply | ping_measured=true, ping_response=true, real RTT | Yes / RTT | RTT: n ms |
| Transmit + no reply | ping_measured=true, ping_response=false, null RTT/latency | No / - | ICMP: NO REPLY |
| Unmeasured | ping_measured=false, ping_response=null, null RTT/latency | - / - | ICMP: N/A |

Internal RTT/loss/response fields are cleared for unmeasured records; zeros are unavailable storage defaults, not measured zero latency/loss. A measured single-probe timeout retains loss=100. ARP keeps reachable=true independently of ping. API preserves old field names, adds ping_measured, and adds aggregate icmp_setup_failures, icmp_measured, icmp_unmeasured. Names/vendor remain visible with ICMP unavailable. Buttons/navigation are unchanged.

The scan-level warning is: ICMP unavailable (<reason>); preserving N ARP hosts; hostname enrichment continuing. Setup failure is not retried for every remaining host. Existing SDK error logs remain visible.

## Immediate socket diagnostics; cause remains unknown

The patch now saves int socket_errno=errno immediately after a negative socket result, before logging/cleanup, and logs:

    ping_sock: socket() failed errno=<n> (ENOBUFS=<target value>, ENFILE=<target value>)

The old create socket failed message remains. Target constants avoid assumed Linux errno numbers or allocated error-name strings.

Installed SDK verification:

- components/lwip/port/esp32xx/include/arch/cc.h includes errno.h.
- lwip/priv/sockets_priv.h defines set_errno by assigning errno.
- lwip_socket in src/api/sockets.c returns ENOBUFS if netconn creation fails; it returns ENFILE if alloc_socket finds no slot, after deleting the new netconn.
- Toolchain errno.h includes sys/errno.h, the same mechanism read by this logging.

Neither supplied failure captured errno, so no retrospective cause is proven. Socket-slot exhaustion, netconn/raw-PCB/memory pressure or another transient resource failure remain hypotheses. ENFILE on recurrence identifies socket-slot allocation failure for that event. ENOBUFS identifies netconn creation failure, but does not alone prove a heap leak. Reboot recovery and stable free heap do not establish root cause.

CONFIG_LWIP_MAX_SOCKETS=10 remains unchanged. The only ping patch changes are the errno include/log; receive guard, atomic flags and retirement behavior are unchanged.

## Cleanup audit

- Failed creation publishes no handle; SDK cleans its partial allocations. Discovery has no phantom handle to delete.
- Created but unstarted sessions are deleted once and included in the existing 1100-ms retirement margin.
- Ended sessions are harvested, deleted once and retired before another batch.
- End semaphores are checked immediately, so already queued completions survive an earlier session consuming the shared deadline.
- Pending sessions are stopped once per cleanup pass and given the existing bounded 500-ms end wait. Without completion, permanent callback storage/handle stays quarantined; callback-owned records are not copied.
- Later scans reap completed handles before reuse. While any remain pending, ARP/vendor/PTR still run and ICMP is skipped; no new ping sessions overwrite the quarantined storage.
- Lifecycle cancellation retires/stops the started batch and uses common fatal finish/coordinator release. The existing coordinator continues to serialize AutoTest/Discover and the short PTR pass.

These guarantees are evaluated with production-code fixtures. They do not prove actual hardware heap recovery.

## Tests

New verify_icmp_degradation.py extracts the actual ICMP phase, late-session reap block and common finish. Target GCC constexpr fixtures mock SDK session/semaphore/time APIs and begin with five ARP/vendor records. Cases include total/partial creation failure, start failure, no actual transmission, measured no-reply, missing completion, PTR execution, preserved host data, coordinator release, cancellation, pending-session scan/reaping/recovery, and repeated success -> total failure -> success -> partial failure -> success. Exact-once deletion/retirement is checked for completed/setup/start-failure paths. Errno capture must precede logging/cleanup.

Expanded API/OLED/browser fixtures verify all three states, ARP reachable=true with null ping_response, preserved names/vendor, maximum response bounds, and existing routes/browser OTA functions.

Final results, all exit zero:

| Script | Result |
| --- | --- |
| verify_switch_info_parse.py | PASS |
| verify_switch_info_cdp.py | PASS |
| verify_switch_info_api.py | PASS |
| verify_switch_info_ui.py | PASS |
| verify_enhanced_discovery.py | PASS |
| verify_enhanced_api_ui.py | PASS |
| verify_hostname.py | PASS |
| web_ui.test.cjs | PASS |
| verify_release.py | PASS |
| verify_autotest_rules.py | PASS |
| verify_discovery_logic.py | PASS |
| verify_fast_discovery.py | PASS |
| verify_link_loss.py | PASS |
| verify_icmp_degradation.py | PASS |

Final release integrity was checked after the final build. Evidence: icmp_hardening_test_results.json and individual logs. No hardware fault injection/execution was performed.

## Build and integrity

Normal idf.py build: PASS, ESP-IDF v6.1.0, GNU GCC 15.2.0 / esp-15.2.0_20251204, explicit GCC and process-local UTF-8. No Clang. Logs: icmp_hardening_build.log and icmp_hardening_build_final.log.

| Item | Result |
| --- | ---: |
| Application | 485,344 bytes |
| Increase over Phase B | 832 bytes |
| Bootloader | 26,240 bytes |
| Bootloader remaining allowance | 2,432 bytes |
| OTA slot size, each | 1,740,800 bytes |
| OTA headroom, each | 1,255,456 bytes (about 72.12%) |
| Compiler warnings/errors | None |
| New static state | 12 bytes, three uint32 counters |

No device fields/tables, tasks, queues or application heap allocations were added. A four-element completion flag array and small bookkeeping use the existing worker stack; physical heap/watermark impact remains a retest item.

Image SHA-256: 377ca7ce41ed0f3e6c61313be86588766d4ffd2f91960738ff6dd0961f3db6c6.
Partition SHA-256 unchanged: 41d8ef7960bf03647249e26ca1ace2b8d35f317a87f6c2ff5bc9f4bdbdbf602b.
Release verification confirms descriptor/version, image checksum/SHA-256, 4 MB flash and two existing OTA slots.

Baseline hashes show exactly the six listed firmware changes. ARP model/logic, OUI, PTR files, AutoTest, coordinator/network lifecycle, LLDP/CDP/Switch Info/RX, DMA patch, OLED/button driver, OTA authorization, sdkconfig/defaults, CMake files and version are unchanged. The ping patch was changed solely for the requested diagnostic.

## Exact hardware retest after separately approved flashing

1. After a separately approved installation, reboot and keep the normal serial monitor open on the known device port. Confirm 0.6.0-dev. This task issues no flash command.
2. Run Discover five times on the same LAN, waiting for hostname completion/coordinator release. Save counts/core time/ICMP/PTR results and final free/minimum heap/watermark. Compare with the supplied prior observations (recovered heap roughly 144864–144876, minimum roughly 129556, watermark 2252 bytes), without treating them as hard targets.
3. Check normal real RTTs, measured no-reply devices, correct PTR/IP/MAC/vendor pairs, OLED navigation, web table and /api/discovery.
4. If socket() failed/create socket failed recurs, DO NOT REBOOT IMMEDIATELY. Save immediate errno/ENOBUFS/ENFILE references, the degradation summary, preserved core count, PTR counters and heap/stack logs.
5. Compare the ARP/vendor count preceding failure with completed count. Verify every valid IP/MAC/vendor/private label survives. Hostname enrichment should run; absence of PTR or separate DNS failure must still leave hosts visible.
6. Affected unmeasured hosts must show OLED ICMP: N/A, web ICMP/RTT '-', API ping_measured=false, ping_response=null, rtt_ms=null, latency_ms=null and reachable=true. Actual measured timeouts must show NO REPLY/No. Earlier completed measurements must remain real.
7. Record icmp_setup_failures/icmp_measured/icmp_unmeasured and hostname counts. Total setup failure should leave all ARP hosts unmeasured; partial failure should retain its measured subset.
8. After coordinator release, run another Discover WITHOUT REBOOT. Capture recovery or repeated failure/errno. Pending cleanup should allow ARP/PTR with ICMP skipped until the callback is reaped.
9. Repeat recovery scans and compare recovered heap; note declining heap, persistent pending-session messages or missing hosts. Do not claim resource recovery from mocked tests alone.
10. Run AutoTest afterward, then Switch Info. Confirm existing button routes and OTA physical authorization behavior. Do not perform an OTA upload as part of this retest.
11. Separately test link loss during a scan: old-context data must be rejected; reconnect/get DHCP and start a fresh scan without stale ping/name data.
12. Provide saved serial logs and JSON before attributing the issue to exhaustion or changing socket limits. A captured errno identifies the failing allocation branch, but further evidence is needed to diagnose a leak/root cause.

Stop here for review. No new discovery features, socket-limit changes, flash or OTA were performed.
