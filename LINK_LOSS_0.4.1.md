# Syndetis 0.4.1 — Ethernet Link Loss / Recovery

## Investigation: concrete defects and strength of evidence

The user confirmed that the observed freeze occurs while both AutoTest and
Discover are idle; disconnect during an active diagnostic has not yet been
tested on hardware. Active diagnostic cleanup is therefore not the observed
trigger. The audit and regression fixes below cover the idle receive path as
well as the separately requested diagnostic cancellation requirements.

The source audit did **not** find an application lock-order cycle in the
disconnect callback. `network_status.on_eth()` originally changed link state,
revision and IPv4 under a short spinlock, then returned. It did not perform
ping cleanup, HTTP work or OLED rendering. Button presses only enqueue events
with zero wait; OLED I2C operations already have 100-ms transfer timeouts.
The coordinator was a boolean protected by a short spinlock, not a mutex held
for the duration of a scan. It was released by the common completion paths.

The strongest concrete whole-UI hang candidate is in the installed ESP-IDF 6.1
EMAC DMA source, `components/esp_eth/src/mac/esp_eth_mac_esp_dma.c`:

```c
while (!desc_iter->RDES0.LastDescriptor) {
    desc_iter->RDES0.Own = EMAC_LL_DMADESC_OWNER_DMA;
    desc_iter = (eth_dma_rx_descriptor_t *)(desc_iter->Buffer2NextDescAddr);
}
```

The descriptor links form a **circular ring**. Both the receive tail walk and
frame-flush walk depended exclusively on eventually finding LastDescriptor.
Neither had a ring-size limit. A partial/inconsistent frame or a reset race
with no terminal marker therefore makes the walk repeat forever. The Ethernet
receive task has higher priority than the menu/button tasks. A persistent busy
walk is a concrete CPU-starvation path compatible with apparent total UI freeze;
it does not require an application-mutex deadlock or any active diagnostic.

The corresponding hang sequence is: queued receive work -> receive/flush of
a descriptor chain -> absent terminal marker -> revisit the circular chain
forever -> Ethernet task fails to yield -> local lower-priority work is starved.
The normal link path is PHY polling -> MAC set_link/stop -> DISCONNECTED event
-> esp-netif link-down/DHCP/IP handling and application observers. SDK MAC stop
and FIFO flush checks were inspected and are bounded; the descriptor walks were
not. Reconnect also resets descriptors, making this a relevant race boundary.

**Confirmed:** the unmodified installed flush routine fails a synthetic
missing-terminal-marker regression (compiler constexpr operation limit is
exceeded); the actual patched routine returns and returns at most one ring to
DMA. A valid frame still returns only its own descriptors.

**Not yet confirmed:** whether the board's observed cable-removal freeze entered
this exact loop. No hardware task backtrace/serial freeze log was supplied.
The source defect and regression are real; hardware causality and recovery must
be verified with the cable-removal sequence below. This report does not present
source analysis as a successful physical-board reproduction.

Other concrete blocking hazards found:

- Discover snapshot, device, publication and start paths waited on a task-owned
  mutex using portMAX_DELAY. UI unconditionally reads Discover metadata even
  outside the Discover menu. A stuck mutex owner would therefore freeze menu
  processing and make working button polling appear ineffective. No specific
  owner-stuck sequence was proven in the original application code.
- AutoTest's network inspection used synchronous esp-netif API calls. The local
  SDK implementation waits on api_lock_sem/api_sync_sem without a caller timeout
  when invoked from outside TCP/IP. This could strand the diagnostic worker and
  coordinator if TCP/IP failed to service the request.
- The original raw ping receive loop did not check a stop request or absolute
  receive deadline while consuming unrelated ICMP packets. Socket SO_RCVTIMEO
  bounds a receive that gets no packets; it does not bound a stream of unrelated
  packets. A stopped session could therefore remain alive and prevent safe
  callback-context reuse. Shared flags also lacked atomic access.
- Application status observers were registered after the SDK netif glue
  handlers. A blocked SDK handler could delay publication of DOWN/generation.
- AutoTest could refresh its network snapshot after a brief disconnect/reconnect
  during preparation/DHCP and continue on a different link generation.

## Fixes and callback/lifecycle behavior

The top-level CMake substitutes **two project-local SDK source copies**:

- `patches/esp_eth_mac_esp_dma.c`: RX tail and flush walks are bounded by
  CONFIG_ETH_DMA_RX_BUFFER_NUM, initialized receive-length output, and rejection
  of invalid CRC-length underflow. A missing terminal marker is dropped with a
  concise warning. There is no chip reset, driver pin change or clock change.
- `patches/ping_sock.c`: the receive loop checks an atomic START flag and elapsed
  receive budget. Stop requests cannot be ignored by a stream of unrelated
  packets. A stopped/timed-out unmatched receive returns failure, never a
  fabricated success. Normal packet counts/timeouts/intervals are unchanged.

The installed SDK is not modified. Original-source hashes and exact diffs are
in `patches/README.md` and the two `.c.patch` files. CMake rejects IDF versions
other than 6.1 and fails if either original source was not located. Tests inspect
compile_commands.json to prove each replacement is active and compiled once.

Application observers register before esp_netif_attach installs default glue
handlers. They only update a small state snapshot and try a zero-wait queue send;
logging runs in a separate task. They do not call ping, netif inspection, OLED,
HTTP, diagnostic execution or worker completion waits. LOST_IP invalidates IP
and the link generation; GOT_IP while DOWN is ignored. Existing SDK netif glue
continues to own DHCP/down/up processing. The stock SDK handlers themselves can
make synchronous TCP/IP calls; the application publishes cancellation before
them, and neither the local UI nor diagnostic cancellation waits for them.

The existing network revision still tracks IPv4 changes. A separate link
generation tracks link transitions/lost IP. Diagnostic requests capture the
generation/revision **when requested**, so preparation is covered too. The
coordinator now issues a nonzero operation ticket. A stale release cannot clear
a newer owner's reservation. Ping, ARP and optional DNS callbacks check the
ticket and network revision before accepting data or submitting a DNS request.
Late callbacks only complete/reap their permanent job storage; they cannot
change the published result or mark a cancelled diagnostic RUNNING.

Discover keeps its 254-entry record format, ascending numeric traversal,
ARP-or-ICMP presence rule, read-only API and UI navigation. No ARP-first or scan
speed optimization is included. Four probes, one echo, eight payload bytes,
250-ms receive timeout and 1100-ms retirement pause are unchanged.

On link loss during preparation/probing/ARP inspection, Discover ends with
`error` and `ABORTED_LINK_DOWN` (or ABORTED_NETWORK_CHANGE after a fast reconnect,
or ABORTED_OTA). Partial results are cleared. Active ping sessions are stopped;
completion waits remain finite, sessions that completed are deleted, and any
delayed context is quarantined until completion rather than deleted unsafely.
The common finish path always releases the coordinator. Normal stopped sessions
finish within their configured socket timeout and the existing retirement delay.
Worst application cleanup allowance is approximately four 500-ms waits plus
1100-ms retirement and polling, under normal task scheduling (about 3.2 s).
ARP waits poll cancellation every 50 ms rather than blindly waiting one second.

AutoTest may still start with no cable and produce the established Link FAIL.
Link changes during preparation/DHCP/ping/DNS produce Link FAIL and an explicit
ABORTED_LINK_DOWN/IP_CHANGE note. A cancelled operation returns before starting
later network stages. Its worker always clears running and releases its ticket.
Existing PASS/WARN/FAIL rules and the gateway's three packets, 1000-ms timeout
and 500-ms interval remain unchanged.

On reconnect the existing driver restarts the MAC and esp-netif restarts DHCP.
Observers show UP, then the reacquired IPv4. HTTP remains started on its existing
server; no ESP32 reset or application HTTP restart is performed. After ordinary
bounded session retirement, new diagnostics can start on the new generation.
If the TCP/IP thread itself never resumes or an SDK session never completes,
permanent storage stays quarantined and the next attempt reports unavailable
instead of reusing unsafe context. This is a fail-safe, not a claim that software
can recover an arbitrarily dead SDK task. The patched RX path addresses the
source-confirmed persistent spin condition that could cause such starvation.

## Lock and wait audit

Rule: **hold one subsystem state lock at a time; only copy/update bounded data;
release before calling another subsystem, signaling a callback, logging, waiting,
or rendering. No lock is held for the duration of an operation.**

| Subsystem | Protection | Work while held | Wait policy |
| --- | --- | --- | --- |
| Network status | Short portMUX | State copy, lifecycle counters, bounded clear/copy, inspect-job flags | No task-owned mutex or network call while held |
| Discovery | Short portMUX, replaces mutex | Metadata/single-device copy; bounded 5080-byte HTTP table copy; up to four result appends | All six portMAX_DELAY state-mutex acquisitions removed |
| AutoTest | Short portMUX | Snapshot/result/job flags and bounded string copies | Formatting removed from this critical section; no network call while held |
| Coordinator | Short portMUX | Reserve, validate or release a ticket | Acquisition is a nonblocking try; no operation-duration mutex |
| Menu | UI-task ownership; button queue | Navigation local to UI | Queue receive 200 ms; press callback sends with zero wait |
| Buttons | Polling-task ownership | GPIO reads/debounce | 10-ms pacing; no network/discovery/HTTP locks |
| OLED | UI-task ownership; IDF I2C driver | Existing render/transfer | Existing 100-ms I2C transfer bound; untouched |
| Firmware update | Existing short portMUX | Authorization/state/progress metadata | OTA code unchanged; no subsystem network lock held across writes/HTTP |

Exactly two application portMAX_DELAY waits remain: idle request notifications
in the AutoTest and Discover workers. They hold no subsystem lock, reservation
or active network operation. An idle worker sleeping cannot block UI/events.
No active diagnostic semaphore wait is infinite. Network inspection submits
tcpip_try_callback and polls at 10 ms for at most one second, exiting early on
link/revision change. Ping completion remains bounded (Discover 1500-ms batch
deadline/500-ms per-slot cleanup; AutoTest 6-s gateway deadline/2-s cleanup).
ARP callback wait is at most 1 s with 50-ms cancellation checks; optional DNS
wait remains at most 5 s with 100-ms checks. Worker loops yield/delay.
SDK socket/netif internals still depend on a functioning TCP/IP task; no unsafe
task deletion, private ping-handle manipulation, watchdog disabling or watchdog
timeout increase is used to hide a failure.

Status shows ETH DOWN and `IP: -` after disconnect. Discover remains selectable
and displays Ethernet link down / OK: BACK. AutoTest, About and Firmware Update
menus remain usable locally without Ethernet. OLED/buttons/pins are unchanged.

## Files edited for 0.4.1

`CMakeLists.txt`, `version.txt`, `README.md`, `main/app_main.c`,
`main/network_status.c`, `main/network_status.h`, `main/discovery.c`,
`main/discovery.h`, `main/autotest.c`, `main/diagnostic.c`, `main/diagnostic.h`,
`main/ui_menu.c`, `tests/web_ui.test.cjs`.

## Files added for 0.4.1

`main/diagnostic_rules.h`, `patches/esp_eth_mac_esp_dma.c`,
`patches/esp_eth_mac_esp_dma.c.patch`, `patches/ping_sock.c`,
`patches/ping_sock.c.patch`, `patches/rx_ring_guard.h`,
`patches/ping_receive_guard.h`, `patches/README.md`,
`tests/link_loss_test.cpp`, `tests/verify_link_loss.py`, `LINK_LOSS_0.4.1.md`.

Generated application, ELF/map, compiler objects/libraries, build configuration,
test fixtures and logs were regenerated in build/. Earlier 0.4.0 source additions
remain in the workspace; they are not additional 0.4.1 features.

## Automated tests

All requested checks passed after the final code changes:

- `idf.py build`: ESP32 / ESP-IDF 6.1 / descriptor version 0.4.1.
- `python tests/verify_release.py`: ESP image/version/project, checksum/SHA256,
  partition MD5/bounds, 4 MB layout and both OTA slot fit checks.
- `python tests/verify_autotest_rules.py`: all 39 original grading assertions.
- `python tests/verify_discovery_logic.py`: original subnet, limits, exclusions,
  numeric sorting and ARP/ICMP presence checks.
- `node tests/web_ui.test.cjs`: prior AutoTest/OTA/offline checks, discovery
  results/progress/error/zero hosts and new ABORTED_LINK_DOWN rendering.
- `python tests/verify_link_loss.py`: actual production lifecycle/reservation
  rules for idle UP->DOWN; both diagnostics UP->DOWN; DOWN->UP->DHCP; both abort
  ->reconnect->new request; stale callbacks/releases; lost IPv4; ticket wrap;
  error reservation release; bounded ping/ring guards; actual patched DMA flush
  on valid and missing-marker rings; expected nontermination failure of original
  SDK flush on the same missing-marker fixture; source common-cleanup/zero-wait
  callback invariants; active CMake replacement routing; unchanged scan pacing.

The pure rules and actual flush body are evaluated with C++ constexpr using the
installed compiler. Hardware/cache/log operations in the flush fixture are
stubbed; no Python model replaces the tested C logic. These checks cannot certify
PHY/DMA timing or successful physical-board recovery. Upstream IDF configuration
warnings remain, with no project compilation/test failures.

## Hardware acceptance still required — not performed by Codex

1. Boot with no cable. Navigate Status, About, Firmware Update and main menu;
   run AutoTest and confirm Link FAIL; Discover should show Ethernet down.
2. Connect the cable, wait for DHCP, verify local Status and web access.
3. While both diagnostics are idle, remove the cable. Continue pressing all
   buttons and navigating for at least two minutes; verify DOWN and cleared IP.
4. Reconnect without resetting. Verify UP/DHCP/web, then start both diagnostics.
5. Repeat cable removal during Discover preparation, probing and ARP inspection.
   Verify an explicit abort, cleared device list, responsive menu, and a new
   scan after reconnect. Include rapid unplug/replug to test generation checks.
6. Repeat during AutoTest DHCP wait, gateway ping and optional DNS. Confirm FAIL,
   coordinator release, and successful next run after DHCP.
7. Inspect serial logs for link/generation changes, cancellation, completed abort
   and coordinator release. If a freeze remains, capture the last logs and a task
   backtrace to confirm which SDK path is blocked before broadening the fix.

No physical device was contacted, flashed, erased or reset during this task.

## Final OTA artifact and preservation checks

Application: **462,432 bytes** (`0x70E60`).
Remaining in each unchanged 1,740,800-byte OTA slot:
**1,278,368 bytes** (73.44% free).
Application SHA256: `315c81d3a13c120181e9740e217b93d9b4403cde80063831e7fbb36d7901e22a`.
Exact application binary for the existing browser updater:

`C:\Users\theoc\Documents\ChatGPT\Syndetis\build\syndetis_finder.bin`

OTA authorization, second OK, five-minute window, upload endpoint and validation
code are unchanged. Rollback, bootloader and partition-table binaries are
unchanged. Both GPIO assignments and OLED/button drivers are unchanged. The
installed SDK was not edited; only the two build-local source replacements run.
The firmware uses the existing application descriptor/version mechanism for
About (`SYNDETIS`, `v0.4.1`, `by T.Anastopoulos`). No watchdog settings changed.
