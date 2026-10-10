# Syndetis 0.4.2 — Fast Discover

Built and host-verified on 8 October 2026. No hardware was flashed or contacted.
The user's successful 0.4.1 hardware verification is the stability baseline;
0.4.2 still needs physical timing and cable-removal verification.

## Why 0.4.1 took approximately 86 seconds

The source creates four concurrent one-packet ping sessions per batch. For
253 candidates there are 64 batches. Every batch, including the last, explicitly
waits 1100 ms after deleting the sessions: **70.4 seconds of deliberate waits**.
Silent probes add approximately 250 ms per batch: another 16 seconds, giving
**86.4 seconds** before small creation, scheduling, socket and callback costs.
Responsive hosts can shorten a few batches, consistent with the observed 86 s.

`cfg.interval_ms=0` already disables the default ping interval. The patched local
ESP-IDF 6.1 `esp_ping_thread()` calls the end callback after the one packet, then
waits on a notification with a 1000 ms idle timeout. `esp_ping_delete_session()`
only clears the initialization flag; it does not synchronously destroy the task,
socket, packet buffer or heap allocation. The 1100 ms application wait prevents
successive batches accumulating these resources. Session creation allocates a
task/socket per target, but the source does not contain an additional deliberate
creation delay. Worker completion waits share a 1500 ms batch deadline; they do
not nominally charge four separate 250 ms waits when probes complete together.
ARP lookup is a single TCP/IP callback after each batch, with a bounded one-second
queue/completion budget and 50 ms cancellation polling; it has no mandatory
one-second settling delay. FreeRTOS scheduling can add jitter.

This is a concrete source timing reconstruction, not a new hardware profile.
It identifies the dominant fixed delay without attributing every measured
millisecond to unmeasured runtime overhead.

## Implemented algorithm and supported SDK APIs

1. Keep the existing subnet validation, /24 maximum, own-address/network/broadcast
   exclusions, diagnostic reservation, network revision and generation guards.
2. Schedule a TCP/IP callback to clear this Ethernet interface's old ARP entries.
3. Submit one raw ARP request, then pace for 40 ms from callback completion.
   Before each request, enumerate stable ARP entries and copy eligible IP/MAC
   observations into the existing permanent Syndetis discovery table.
4. Repeat across the candidate addresses. Replies remain pipelined: there is no
   per-target reply timeout or pending-entry wait. The callback copies the cache
   every emission step, including entries from earlier targets whose replies
   arrived late. No catch-up burst follows slow TCP/IP queue service.
5. Collect every 40 ms for a final 500 ms window, then perform one final collection.
   With 40 ms polling, the ordinary settling duration is approximately 520 ms.
6. Sort the copied hosts by numeric IP. Ping only these hosts, four concurrently,
   one echo each, 250 ms receive timeout, zero ping interval, eight-byte payload,
   Ethernet interface binding and 3072-byte task stacks.
7. Preserve ARP/MAC for silent ICMP hosts, attach successful ICMP/RTT observations,
   update checking progress, retire sessions and publish completion.

Locally inspected sources:

- `C:/esp/v6.1/esp-idf/components/lwip/lwip/src/include/lwip/etharp.h`
- `C:/esp/v6.1/esp-idf/components/lwip/lwip/src/core/ipv4/etharp.c`
- `C:/esp/v6.1/esp-idf/components/lwip/lwip/src/include/lwip/opt.h`
- `C:/esp/v6.1/esp-idf/components/lwip/port/include/lwipopts.h`
- The active project-local `patches/ping_sock.c`.

ARP uses the documented `etharp_request(struct netif *, const ip4_addr_t *)`,
`etharp_get_entry(size_t, ip4_addr_t **, struct netif **, struct eth_addr **)`, and
`etharp_cleanup_netif(struct netif *)` APIs. `netif_get_by_index()` resolves the
interface inside the callback. `tcpip_try_callback()` schedules all lwIP ARP/netif
access on the TCP/IP thread. No ARP-table internals or retained lwIP pointers are
used. MAC bytes are copied before the callback returns.

The actual configured **ARP_TABLE_SIZE is 10**, confirmed by preprocessing the
firmware with its actual compile command/includes. It remains unchanged.
`etharp_request()` emits a frame without allocating pending cache entries;
replies addressed to this interface can create stable entries through lwIP's
normal input processing. Copying all ten entries every 40 ms avoids retaining
all 253 targets in lwIP. Already copied hosts survive subsequent cache eviction.

The rolling collection window covers all previously issued targets, up to the
254-address storage bound, rather than a fixed four-address batch. Each TCP/IP
job emits at most one request and examines at most ten cache entries. Host
deduplication is by IP; repeated observations update the MAC without appending.
An IP whose MAC changes remains one device with the latest observed MAC.

Rate is controlled by the single `DISCOVERY_ARP_REQUESTS_PER_SECOND` constant in
`main/discovery_logic.h`, initially 25. Callback and scheduling costs can reduce
the actual rate. The normal final settle is configured by
`DISCOVERY_ARP_SETTLE_MS`, initially 500.

## Cache freshness and limitations

The Ethernet ARP cache is cleared once at the start, on the TCP/IP thread. A
result must then have a stable entry on this interface, belong to this subnet,
and have had its request issued in this scan. Old unchanged cache entries cannot
become false positives. Fresh normal ARP traffic after the reset is useful
current evidence; this is not packet-level correlation of each reply to its
individual request. No unsupported ARP age/struct inspection is used.

Clearing this cache can briefly require normal Ethernet traffic, including the
browser connection's neighbor, to resolve again. lwIP also frees any queued
packets attached to removed pending entries. This is the deliberate tradeoff
for reliable stale-cache exclusion using supported APIs; the interface is not
reset, DHCP is not restarted and TCP sessions are not explicitly closed.

A single paced sweep plus bounded settling can miss unusually late replies or
a burst of more than ten resolved entries between collections. Hardware testing
should assess this under the actual network's traffic; there is no retry storm
or increased ARP table allocation.

## Timing estimates

Theoretical ARP pacing budget for 253 candidates is `253 / 25 = 10.12 s`.
The implementation also charges short callback/scheduling overhead per request.
Final collection normally adds approximately 0.52 s.

The proven 1100 ms retirement wait is retained after each **discovered-host**
ping batch, including the last. It now checks cancellation every 10 ms. For
silent hosts, one ordinary batch costs approximately `0.25 + 1.10 = 1.35 s`.
This deliberately avoids changing the verified ping implementation or allowing
idle ping tasks/sockets to accumulate. Faster ICMP replies shorten the 250 ms
portion, but not the retirement wait.

| Discovered hosts | ICMP batches | ICMP + retirement estimate | Total estimate |
| --- | ---: | ---: | ---: |
| 0 | 0 | 0 s | 10.64 s + overhead |
| 4, observed network | 1 | 1.35 s | 11.99 s + overhead |
| 20 | 5 | 6.75 s | 17.39 s + overhead |
| 253, dense /24 bound | 64 | 86.40 s | 97.04 s + overhead |

Thus the observed four-host network should be approximately 12–13 seconds,
and twenty hosts approximately 17–19 seconds under ordinary scheduling. The
10–20 second target is **not** met for a densely populated /24. This is an
explicit stability tradeoff, not a claim that every /24 is now a 12-second scan.
The overall scan retains its 120-second deadline, with bounded callback/probe
cleanup. Exact timing must be measured on hardware.

Logs report ARP sweep, settling, ICMP enrichment (including retirement) and total
duration/found count. Timing details do not appear on the OLED. OLED sweep
progress counts issued candidates and current discoveries; checking progress
counts enriched hosts. Its existing refresh/navigation workflow is retained.

## Stability, compatibility and scope

The existing coordinator, network revision checks, token checks, permanent probe
contexts, quarantined pending ARP callback storage and bounded waits remain.
Link loss/network change/OTA aborts the paced sweep or checking phase. ARP jobs
with stale tokens cannot send requests or copy results. Ping success callbacks
retain token, network and expected-source checks. Common `finish()` releases
the coordinator on scan errors/cancellation and clears incomplete results.
Pending callback/probe storage is reaped before reuse; a callback still pending
causes a bounded error instead of unsafe reuse. Reconnect permits another scan
once pending callbacks have retired.

The 0.4.1 project-local EMAC receive-ring and ping receive-loop patches are
unchanged, as are network event handling, button handling, AutoTest, OTA logic,
GPIO assignments, sdkconfig/defaults and partition layout. Static discovery
device layout and all existing `GET /api/discovery` fields are unchanged. The
OLED's About screen still reads the app descriptor and displays SYNDETIS,
v0.4.2, a blank row and by T.Anastopoulos. No additional discovery protocols,
hostname lookup, vendor lookup or port scanning were added.

Host regressions support preservation of these safeguards; physical 0.4.2
cable-removal/reconnection behavior has not been reverified during this task.

## Build, tests, RAM and OTA artifact

All requested checks passed on the final build:

- `idf.py build` with ESP-IDF 6.1.
- `python tests/verify_release.py`: version, ESP32/IDF, checksum/SHA256, partition
  MD5/bounds, two 1700 KiB OTA slots and application fit.
- `python tests/verify_autotest_rules.py`: existing 39 policy assertions.
- `python tests/verify_discovery_logic.py`: existing subnet, exclusion, presence
  and numeric-order assertions.
- `python tests/verify_fast_discovery.py`: generated constexpr execution of the
  **actual production ARP callback** against simulated four-host and dense /24
  networks with a ten-entry cache; duplicates/MAC changes, rolling collection,
  copied-result survival after eviction, ARP-only retention, stale-cache reset,
  link-down/stale callback rejection and reservation/reconnect rules. It confirms
  253 paced requests have a 10,120 ms logical pacing budget and verifies there
  are no ping calls in the ARP phase. Actual firmware preprocessing confirms
  the ten-entry cache configuration.
- `python tests/verify_link_loss.py`: existing generation/reservation/DHCP,
  stale release/callback, ping/ring and actual EMAC flush tests; phase coverage
  now includes Discover ARP, Discover ICMP and AutoTest. The source invariant
  was adapted to the cancellation-aware 1100 ms retirement helper.
- `node tests/web_ui.test.cjs`: existing discovery/ARP-only/RTT, AutoTest,
  offline and OTA browser regressions.

These are build, source and deterministic host tests, not real-network timing
or physical link-loss tests. SDK configuration warnings remain external to this
change; the application builds successfully.

The working ESP-IDF Python environment differed from the original build cache.
The existing build was reconfigured to the active installed 6.1 environment;
no fullclean or SDK source edits were performed.

Compared with the existing 0.4.1 artifacts captured before building:

- Firmware image: **463,616 bytes**, up **1,184 bytes** from 462,432.
- Discovery ARP job: 320 bytes, up 220 bytes from 100.
- Discovery status: 148 bytes, up 8 bytes from 140.
- Result table: unchanged at 5,080 bytes; probe contexts unchanged at 144 bytes.
- Total linked DRAM BSS: 17,952 bytes, up **232 bytes** including alignment.
- Linked DRAM data: unchanged at 11,769 bytes.
- Worker stack remains 4096 bytes. ICMP task concurrency/stacks remain four
  at 3072 bytes each; no new persistent heap table or enlarged lwIP cache.

Both OTA slots remain **1,740,800 bytes**. Remaining capacity per slot is
**1,277,184 bytes**, approximately **73.4%**. The partition offsets remain
ota_0=0x20000 and ota_1=0x1D0000.

Exact OTA application path:

`C:\Users\theoc\Documents\ChatGPT\Syndetis\build\syndetis_finder.bin`

Application SHA256:

`b31494530c3ee73ef8cf196a18fb245a772cb540a178b1e6a2a7d361d356688a`

No hardware was flashed.
