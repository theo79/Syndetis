# Syndetis 0.5.0-dev — Phase B implementation and validation

9 October 2026. Passive nearest-bridge LLDP reception is implemented. **Physical reception has not been observed. The standard GCC build remains blocked by the host environment; the alternative complete Clang build failed its bootloader size check.** The alternative application compiled, linked, and passed its application partition size check. Do not treat that as a successful complete firmware build or a final release.

## Files and scope

Added:

- `main/switch_info.h`, `main/switch_info.c`: capture, worker, lifecycle, counters and snapshot API.
- `main/switch_info_parse.h`, `main/switch_info_parse.c`: protocol-neutral fixed neighbor model and host-testable classifier/parser.
- `tests/switch_info_parse_test.c`, `tests/verify_switch_info_parse.py`: parser, classifier, RX ownership/pool and bounds tests.
- This implementation/test report.

Modified for this phase:

- `main/app_main.c`: optional initialization after attachment and before Ethernet start.
- `main/CMakeLists.txt`: two new C sources.
- `main/web_server.c`: read-only `/api/switch-info` endpoint.
- `version.txt`: `0.5.0-dev`.
- `README.md`: development status and report link, preserving its existing encoding.

The working tree already contained 0.4.2 changes at the start. Git's overall diff also contains those changes; they were not reverted or rewritten. No Phase B edits to the main web page, OLED/menu/buttons, board pins, diagnostic implementations, network-status producer, OTA architecture, root CMake patch routing, DMA/ping patches, partition layout, SDK sources or component manifest were made. Project `sdkconfig`, defaults and dependency lock have no diff. The isolated Clang configuration lives under `build/phase_b_clang/sdkconfig`; its compiler/libc differences are verification limitations, not project configuration changes.

## RX integration and filter

The existing glue still owns transmit configuration, buffer-free configuration and Ethernet/IP events. Initialization remains driver install → glue creation → network-status initialization → netif attachment → `switch_info_init()` → Ethernet start. Inside Switch Info initialization, queue/task/event resources are created, nearest-bridge destination `01:80:C2:00:00:0E` is added using `esp_eth_ioctl(driver, ETH_CMD_ADD_MAC_FILTER, destination)`, and `esp_eth_update_input_path_info(driver, rx_input, &context)` installs the callback. The context is permanent static storage.

The filter return is checked and logged. A failure increments `filter_failures`, leaves the normal callback in place, frees optional initialization resources and reports unavailable state. Callback-install failure removes only the filter added by this module and unregisters its handler. The worker waits for a notification until setup commits, so failed setup cannot race worker queue/state access during cleanup. Application startup logs an optional-feature failure and continues with normal Ethernet/HTTP/OTA initialization. No all-multicast fallback, promiscuous mode, TAP or CDP destination is enabled. Callback replacement occurs only before start; there is no live reconfiguration API.

For any frame not matching **both** the six-byte nearest-bridge destination and untagged Ethernet type `0x88CC`, the callback returns `esp_netif_receive(context->netif, buffer, length, NULL)` immediately. It never frees, reads again or queues that forwarded buffer, including on an ESP-NETIF error return. Thus IPv4, IPv6, ARP, broadcast, unrelated multicast, CDP/length-based frames, unknown EtherTypes and VLAN-tagged frames retain the normal path. Frames rejected by the hardware destination filter never reach this callback.

Matching candidates get a bounded local copy and a zero-wait slot-index queue send. The original is freed once and `ESP_OK` returned. Oversize, pool-full and queue-full candidates follow the same free/return rule and increment separate counters. The FCS is not subtracted again; RX metadata pointers are ignored. No parsing, allocation, logging, sockets or formatting occurs inside RX. A short critical section protects slot reservation and counters; copying occurs outside it.

**Tagged LLDP is unsupported in Phase B.** `0x8100`/`0x88A8` frames are forwarded unchanged. Other LLDP scope destinations are also not captured.

## Resources, model and parser

Four capture slots each contain a 1600-byte frame array; frame storage totals **6400 bytes**. Queue depth is **4**, with one-byte slot indices. A slot remains reserved until parsing finishes. Worker priority is **1**, below EMAC RX's default 15 and TCP/IP's configured 18; task stack is **4096 bytes**. A one-tick delay after processing prevents continuous discovery traffic from monopolizing CPU. Task and queue allocation occurs only during initialization. Parser scratch is static worker-only storage.

Alternative-build symbol sizes: capture context **7984 bytes**, parser scratch **1368 bytes**, HTTP snapshot scratch **1432 bytes**. Together these occupy **10,784 bytes** of static storage, plus the 4096-byte worker stack, task/queue overhead, lock and constants. Queue/task allocator overhead and actual free heap require runtime measurement. The internal status getter and HTTP API expose free heap, minimum free heap and the worker's sampled stack watermark (ESP-IDF reports bytes).

`switch_neighbor_t` contains validity, protocol enum (`NONE`, `LLDP`, future `CDP`), receive/expiration timestamps in microseconds, link generation, TTL seconds, source MAC, chassis/device/port IDs, port description, system name/description, optional management IPv4, PVID and supported/enabled capabilities. Presence flags distinguish unavailable numeric fields. `device_id` remains empty for LLDP. One latest valid neighbor is retained; this is not a multi-neighbor database.

Display IDs are 96 bytes, name/port description 128 bytes and system description 256 bytes, including terminators. Full chassis and port identity strings (including subtype, up to 256 bytes each) are retained for equality. This prevents a withdrawal matching another neighbor because display strings were truncated or sanitized.

Parser support:

- Mandatory chassis ID, port ID and two-byte TTL, in that order, once each.
- Chassis/port subtypes 1–7. MAC IDs require exactly six address bytes and use colon-separated uppercase hex. IPv4 network IDs use dotted decimal; IPv6 IDs use eight hexadecimal groups. Other network families use bounded family/hex representations. Text IDs are bounded and sanitized.
- Standard port description (4), system name (5), system description (6), capabilities (7), management address (8).
- Management IPv4, with the full management TLV envelope checked; structurally valid unsupported families are ignored. First IPv4 management address is retained.
- IEEE 802.1 organizational TLV OUI `00:80:C2`, subtype 1, exact six-byte information length: PVID 0–4094. Missing PVID is normal. Other valid organizational TLVs are skipped.
- End TLV must have zero length. Padding after End is ignored.

Every TLV header and advertised length is checked before access. ID lengths/subtypes, duplicate mandatory/singleton standard TLVs, optional field shapes and enabled-capability subset are validated. Standard textual optional TLVs are limited to their 255-byte protocol maximum and safely truncated for display. There is a **64-TLV maximum**, including mandatory and End, and a 1600-byte full-frame limit. Missing End, truncated headers/payloads and malformed recognized organizational TLVs are rejected. Unknown bounded TLVs are skipped. Work is bounded by frame/TLV/string limits; no recursive parser or per-packet heap allocation is used. Management-address envelope layout was checked against [IEEE's LLDP management-address discussion](https://www.ieee802.org/1/files/public/docs2024/60802-woods-proxy-considerations-1024-v02.pdf).

All output strings terminate explicitly. Bytes outside printable ASCII, including embedded NUL/control bytes, become `?`. Strings are never format strings. JSON uses the existing escaping helper; future HTML consumers must still escape text.

## TTL, lifecycle and synchronization

Expiration is RX timestamp + TTL × 1,000,000, using 64-bit arithmetic. Queue delays do not extend lifetime. An already-expired queued advertisement is not published. Valid TTL-zero advertisements withdraw only the current matching chassis/port identity; an unrelated withdrawal leaves it alone.

RX stamps existing network generation, link-up state, private capture epoch and time. Worker checks them before parsing and again under the publication lock afterward. Its ETH handler advances only the module's private epoch and clears the neighbor on connect/disconnect/stop. It never changes existing network lifecycle counters. Periodic processing and every snapshot getter clear records that have expired, have lost link or belong to a different generation. Records are cleared rather than returning stale fields alongside `valid:false`.

The existing generation also advances on IPv4-loss events; Switch Info conservatively invalidates on any generation change. Receiving LLDP itself does not require DHCP or an IPv4 address.

The module uses a FreeRTOS `portMUX_TYPE` critical section for bounded state copies, counters and slot state. Parsing/logging/HTTP formatting occur outside it. Network-status getter locks are nested only for a bounded copy; network-status code never acquires the Switch Info lock, so there is no reverse lock order. Event processing is asynchronous: a physical change becomes visible when the existing SDK event path delivers it. Absolute physical-instant cable detection is not claimed. Getter checks prevent observed stale generations from being reported as current; rapid physical link cycling still needs board validation.

Counters saturate at `UINT32_MAX`: `candidates`, `queued`, `parsed`, `malformed`, `oversize`, `pool_full`, `queue_full`, `stale_generation`, `expired`, `filter_failures`. `parsed` includes valid withdrawals. `expired` counts published records invalidated by their TTL, not malformed frames or lifecycle invalidation. These counters cannot measure hardware-filter/DMA/pre-callback losses.

Neighbor update/withdrawal log groups are limited to one per five seconds. Changed receive counters and heap/stack metrics are logged at most every ten seconds. Expiration is logged by the worker. No packet dumps or per-packet warnings occur during floods.

## Diagnostic endpoint

`GET /api/switch-info` provides `available`, numeric `setup_error`, `valid`, protocol (`"LLDP"` or `null`), counters and memory metrics. Valid records additionally expose TTL, source MAC, receive/expiration times, generation and available parsed fields. Missing text/numeric optional fields are omitted. Invalid records omit neighbor data. HTTP uses a copied static scratch snapshot owned by the existing server task; all sending/escaping occurs after releasing the state lock. The seventh route fits the existing default eight-handler capacity. No final web/OLED Switch Info UI was added.

## Verification results and build limitations

**Unit-tested through compile-time evaluation:** actual production parser and actual RX callback bodies, with constexpr/library/SDK adapters. This follows the project's existing rule-test technique; it is not native execution on ESP32 or a sanitizer run. Parser logic is not reimplemented in Python. Library formatting/copy/zero operations are adapted for evaluation; unadapted C compiles in the application build.

Tests cover minimal/optional advertisements, MAC/text/network identities and all port subtypes, management IPv4/unsupported families, PVID, TTL zero, exact expiration/generation predicates, exact raw identity matching, End/padding, every truncated prefix, invalid headers/lengths/org TLVs, overlong display strings, duplicate TLVs, excessive count and separate classification of normal traffic. Additional **544 single-byte mutations** of an exact-sized minimal frame were evaluated; invalid object accesses cannot pass constexpr evaluation. RX tests mock stack input/free/queue and verify forwarding (including stack error return), four slots, metadata, one free per consumed frame, all three drop paths and saturating counters. Thread scheduling, SDK allocation failures and real DMA operation remain runtime gates.

**Regression checks passed:** existing AutoTest, discovery and link-loss constexpr test sources compiled with Clang; `node tests/web_ui.test.cjs` passed existing AutoTest/discovery/offline/OTA browser behaviors. Existing main sources and patched SDK sources compiled and application linked in the isolated build. Full generated partition-table bytes match the original build.

**Normal complete build: not passed.** Installed IDF Python `_overlapped` loading and GCC launching are blocked by Windows application control/DLL relocation, including an outside-sandbox attempt. No security setting was changed. Bundled Python 3.12, existing compatible IDF modules and an existing cryptography 48.0.1 package copied into build-local storage allowed alternative verification. The normal `build/` artifacts remain the older baseline and must not be mistaken for this development image.

**Alternative complete build: attempted, failed bootloader size gate.** ESP clang 21.1.3 built and linked the application and bootloader under the separate Clang/Newlib configuration. ESP-IDF explicitly labels Clang experimental. Its bootloader was **29,584 bytes (0x7390)** versus the existing **28,672-byte (0x7000)** allowance, exceeding it by **912 bytes**. The partition table was not moved and the check was not relaxed. No combined flashable firmware build is endorsed.

After that failure, the application alone was rebuilt/linked and converted with the generated esptool arguments for measurement:

- Path: `build/phase_b_clang/syndetis_finder.bin` — **experimental application-only verification artifact**, not the normal GCC build.
- Descriptor: `0.5.0-dev`.
- Application image: **601,776 bytes**.
- Each unchanged OTA slot: **1,740,800 bytes**.
- Application slot headroom: **1,139,024 bytes (~65.4%)**; SDK app size check passes.
- SHA256: `e88f913f3381652854c095b7a7f9705a708dcca6be77a8ac3f5f258d12fe8192`.
- Compiler warnings in final application build: none reported. Configuration diagnostics included experimental Clang, SDK private include-dependency warnings, Kconfig notifications, registry connectivity and ROM GDB configuration issues. These do not turn the failed complete build into a pass.
- Actual free heap, minimum heap and LLDP stack watermark: **not measured on hardware**; endpoint/log instrumentation is implemented.

No hardware reception, DHCP/diagnostic throughput, TTL timing under load, button/display behavior, HTTP endpoint execution or OTA upload was physically tested. No board was contacted, flashed or erased.

## Rebuild and physical test procedure

First restore a functioning **normal ESP-IDF 6.1 GCC shell**. Do not change security policy or the partition layout merely to accommodate the alternative build. From the project directory:

```powershell
idf.py build
python tests/verify_switch_info_parse.py
node tests/web_ui.test.cjs
python tests/verify_release.py
```

Require a successful complete build, matching `0.5.0-dev` descriptor and existing partition layout before loading firmware. For the already-performed parser checks in this host environment, the compiler override was:

```powershell
$env:SWITCH_INFO_TEST_COMPILER = 'C:/Espressif/tools/esp-clang/esp-21.1.3_20260408/esp-clang/bin/clang.exe'
& C:/Users/theoc/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe tests/verify_switch_info_parse.py
```

The portable C parser test can also run under a normal host compiler with `-DSWITCH_INFO_TEST_MAIN -I main main/switch_info_parse.c tests/switch_info_parse_test.c`; that native test was not run here.

For an existing working 0.4.2 installation with the current OTA layout, load the **new normal-build** `build/syndetis_finder.bin` through the current web interface's physically authorized OTA upload. It writes the inactive app slot and preserves layout. Then start serial monitoring at 115200 baud:

```powershell
idf.py -p COM4 monitor
```

COM4 is the port recorded in prior project logs; replace it if the actual adapter differs. Monitoring does not flash. For a board requiring initial serial programming, after a successful normal complete build use `idf.py -p COM4 flash monitor` only as an intentional full programming operation; it writes the existing bootloader/table/OTA metadata/application and may reset OTA selection. No such operation was performed. Do not flash the isolated oversized Clang bootloader.

Connect **Syndetis directly by cable to a managed switch port with LLDP transmit enabled**. LLDP does not need DHCP; without DHCP use serial logs instead of the HTTP endpoint. Confirm firmware version and this sequence (values are examples/placeholders, not observations):

```text
SYNDETIS: Syndetis 0.5.0-dev
LLDP: multicast filter installed: 01:80:C2:00:00:0E (ESP_OK)
SYNDETIS: Ethernet link up ...
LLDP: frame received len=<length>
LLDP: neighbor updated chassis=<advertised ID> port=<advertised ID> ttl=<seconds>
LLDP: system=<advertised name>        [when present]
LLDP: mgmt=<advertised IPv4>          [when present]
LLDP: pvid=<advertised PVID>          [when present]
LLDP: counters candidate=... queued=... parsed=... malformed=... ...
```

Wait for at least three configured transmit intervals plus a margin. If the interval is unknown, initially observe for 2–3 minutes, then check the switch configuration; do not infer failure from a short silent interval. Capture `GET /api/switch-info` when IP networking is available. Compare chassis/port/name/PVID to switch advertisements rather than to assumed switch inventory.

## Diagnosing absent reception

| Suspected cause | Evidence/action |
|---|---|
| LLDP globally disabled | Inspect the switch's global LLDP status/configuration; enable it intentionally for this test. Commands depend on the switch vendor. |
| Disabled on this port | Inspect the actual physically connected port and its LLDP settings, not another port. |
| Receive enabled, transmit disabled | Check separate TX/RX port settings and switch advertisement/TX counters. RX-only mode cannot advertise to Syndetis. |
| Destination filter failed | Startup warning plus `available:false`, nonzero `setup_error` and `filter_failures`; normal Ethernet should still boot. |
| ESP32 MAC/DMA not delivering | First verify correct frames on the physical link with an external TAP or suitable capture arrangement, and successful filter setup. A switch SPAN alone may not mirror link-local traffic. Inject known untagged nearest-bridge LLDP if needed. |
| Classifier rejects frames | Examine on-wire destination/type/tagging: only `01:80:C2:00:00:0E` with untagged `0x88CC` qualifies. Tagged/other-scope frames are deliberately forwarded. Controlled correctly formed injection plus successful classifier unit tests narrows the issue. |
| Parser rejection | `candidates`/`queued` increase but `malformed` increases instead of `parsed`; compare captured bytes to mandatory ordering, lengths, End and supported field shapes. Unsupported optional address families alone should not reject. |
| Stale lifecycle | `stale_generation` increases during link/generation changes; wait for the next fresh advertisement after stable link. |
| Pool/queue exhaustion | `pool_full`/`queue_full` increases; examine traffic load and worker stack/free heap. Discovery drops are allowed; normal networking must remain responsive. |

With zero candidates, production counters alone cannot distinguish hardware rejection from a software classifier rejecting a frame: they only count matching candidates. On-wire evidence and controlled correctly classified injection are necessary; if still ambiguous, a separately reviewed bounded instrumentation step is needed. Do not claim a definitive MAC failure from this endpoint alone.

## Runtime regression checklist — all pending

1. Cold boot connected; confirm optional LLDP setup and ordinary Ethernet startup.
2. Confirm DHCP IPv4, ARP/IP reachability and existing web interface.
3. Confirm an LLDP neighbor with switch-matching fields; also test without DHCP through serial.
4. Complete AutoTest and Fast Discover; check cancellation/progress/results against 0.4.2.
5. Confirm subsequent LLDP advertisements update timestamps after Fast Discover.
6. Poll the new API and existing status routes while advertisements/scans run; check memory minimum and worker watermark.
7. Unplug: neighbor invalidates. Replug same switch: only a fresh advertisement repopulates it.
8. Move to another switch/port: old fields must not survive. Exercise rapid link cycling to check queued-generation rejection.
9. Disable switch LLDP TX while retaining link; wait past advertised TTL and verify `valid:false` with neighbor fields absent and expiration counter/log. Inject matching and nonmatching TTL-zero withdrawals if a controlled injector is available.
10. Exercise malformed/oversize/flood traffic in a controlled test; verify bounded counters/storage and normal DHCP/IP/HTTP responsiveness. Include IPv6/unrelated multicast if available.
11. Confirm OLED/buttons/menu behavior and existing OTA routes/authorization rejection.
12. Only after ordinary functionality is confirmed, intentionally authorize and perform OTA; verify upload, reboot confirmation and LLDP startup again. Interrupt/recovery tests require a deliberate operator decision.

Phase B stops here for review. CDP and final OLED Switch Info presentation are deferred. A normal complete build and WT32-ETH01 runtime evidence remain required before any final 0.5.0 release.
