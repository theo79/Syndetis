# Network Tools Phase A: Ping and Continuous Ping

Implementation and software validation completed on 12 October 2026. OLED and buttons provide the complete workflow. Web Ping controls are deferred. No commit, push, flash, OTA, SD support, history, DNS tool, traceroute, port scanner, firmware-version change, or hardware configuration change was performed.

## Architecture inspected

Discovery uses the checked-in patched ESP-IDF `ping_sock.c` through `esp_ping_new_session/start/stop/delete`: up to four simultaneous one-request sessions, eight payload bytes, 250 ms receive timeout, 3072-byte SDK session stacks, zero inter-request interval, and the proven 1100 ms task-retirement margin. Discovery reaps/quarantines sessions using end-callback semaphores. These settings, ARP timing, discovery callbacks and hostname behavior are unchanged.

Quick Test uses the same patched SDK ping API for its gateway checks, with its own callbacks, timeout/cancellation handling and diagnostic reservation. The patch observes atomic start/stop flags and an absolute receive budget despite unrelated raw ICMP traffic. Its task may retire asynchronously up to approximately one second after deletion, which is why discovery preserves a retirement margin. Neither this patch nor the installed SDK was modified.

The existing global `diagnostic_acquire/release/current` coordinator serializes Discover, Quick Test and PROFINET. SNMP previously serialized only its own queued worker and guarded discovery/hostname activity; it did not participate in the global reservation. Socket configuration remains `CONFIG_LWIP_MAX_SOCKETS=10`; FreeRTOS ticks remain 100 Hz. No socket limit or task safety margin was reduced.

The new Ping worker uses the existing BSD raw IPv4 ICMP socket path, independently of discovery internals. It creates one socket per user session and closes it on the owning worker before publishing completion and releasing the reservation. There is no SDK ping-session task or callback to retire. This gives the new tools a 50 ms cancellation-check interval, independent of the discovery implementation and its cadence.

Socket calls follow [Espressif's BSD socket guidance](https://docs.espressif.com/projects/esp-idf/en/v5.0.9/esp32/api-guides/lwip.html). ICMP Echo fields and checksum follow [RFC 792](https://datatracker.ietf.org/doc/rfc792/). The installed ESP-IDF 6.1 headers/source and normal GCC build provide the local API evidence; no SDK upgrade is involved.

## Runtime and concurrency

`ping_runtime.c` owns one permanent 4096-byte worker, a queue with one four-byte session ID, a protected fixed-size status record and a cancellation flag. The idle worker blocks on the queue and holds no socket. UI submission is nonblocking and performs no socket IO. No Ping code executes from an HTTP handler.

Single Ping sends exactly one 16-byte ICMP Echo request, with no automatic retry. Continuous Ping keeps the same socket open for that session and sends approximately once per second, pacing from each actual request start. The maximum receive budget is 750 ms; `select()` waits no more than 50 ms at a time, and `recvfrom/sendto` use `MSG_DONTWAIT`. Interval waiting also checks cancellation/network/target/OTA state every 50 ms. Standard lwIP scheduling and syscall latency still apply; the software polling budget is not a hard real-time hardware guarantee.

Each accepted Ping session acquires the existing diagnostic reservation before queueing. A second Ping, conflicting Discover, Quick Test or PROFINET start is rejected until cleanup releases it. The coordinator and those engines are unchanged.

The necessary SNMP change is confined to `snmp_runtime.c`: acquire the same reservation before queueing, return its existing Busy result when occupied, and release on completion or queue failure. This prevents conflicts in both directions, including Web-triggered SNMP. SNMP transport, codec, default community, retries, result format, worker stack, HTTP endpoints and Web UI are unchanged. A conflict now rejects cleanly rather than allowing one diagnostic to invalidate another. Synchronous SNMP library functions remain worker-only; production UI/Web calls use the coordinated runtime.

Only short state copies occur inside the runtime critical section. Socket calls, network/discovery getters, reservation calls, waits and task creation run outside it. Link loss, IPv4/network generation changes, target changes, Stop and OTA receiving/success terminate the operation. Cleanup closes even socket descriptor zero and releases only the operation's reservation token. There is no cross-task socket close or task deletion while waiting on `select()`.

## Stable targets and packet validation

Targets must be valid, current discovered IPv4 hosts on the current subnet. Zero, loopback, multicast/reserved-class, local-interface and subnet broadcast/network targets are rejected. There is no manual IP entry and no subnet probing from either Ping tool.

The queued operation retains numeric IPv4, own IPv4 for socket bind/reply validation, MAC presence/value, discovery start identifier, network revision and physical link epoch. It does not retain a discovery-table index. Getter-based searches validate the discovery run on both sides of the search, and network state is checked again afterward. Discovery enrichment must have completed before starting. A missing host, changed MAC, new discovery run, changed network or link epoch invalidates the selected target/result.

Each Echo contains a full 32-bit session ID and 32-bit probe number in its eight-byte payload, in addition to the normal 16-bit ICMP identifier/sequence. Replies are accepted only when source and destination IP, IPv4 version/IHL/total length/protocol/header checksum, nonfragmented flags, Echo Reply type/code/checksum, identifier/sequence and full payload match. IP options are bounded and supported. The full probe payload prevents 16-bit sequence wrap from accepting an old probe. Short, malformed, unrelated and stale packets cannot produce a success or extend the absolute receive deadline.

Status messages distinguish Reply, Timeout, Stopped, Link lost, Network changed, Target changed, Error, Session limit and Firmware update. A later network/discovery change invalidates completed results in the getter; the OLED hides stale measurements.

## Menu and field operation

Top-level navigation remains Quick Test, Discover, Network Tools, Industrial and System. The existing 450 ms held-OK Back event and 30 ms debounce are preserved; no fourth button or GPIO is added.

Network Tools contains five real choices, all fitting the existing five menu rows:

```text
NETWORK TOOLS

> PING
  CONT PING
  SNMP - SELECT HOST
  SWITCH INFO
  BACK
HOLD OK: BACK
```

UP/DOWN moves selection without starting anything. Ping or Cont Ping opens the current discovered-host list. If results do not yet exist, the existing Discover ready/scan/results flow obtains them; after selecting Devices, selecting a host opens the chosen tool's Ready screen. An empty list has Back and the existing Scan again route remains accessible.

Selecting a host through Discover opens Host Actions, now Ping / Cont Ping / SNMP / Details / Back. Four action rows are visible below title/IP/vendor; the fifth action scrolls into view. Switch Info remains the passive connected-neighbor view under Network Tools, not an unsupported arbitrary-host query. Existing SNMP, Details, Industrial, System and firmware paths remain reachable.

Single Ping: choose tool -> select host -> Ready -> short OK Run -> one result -> OK returns to Ready for a confirmed repeat. Back from the Ready screen returns to its originating list or Host Actions. UP/DOWN does not execute a probe. Results report Reply with RTT or Timeout; failures/invalidations are explicit.

Continuous Ping: same selection/Ready boundary -> short OK Run -> two statistics pages. UP/DOWN toggles pages without network actions. Short OK while queued/running requests Stop and retains the terminal result on screen. Held OK requests Stop and shows Stopping until the worker reports completed cleanup, then returns to Ready. It never leaves an orphan operation running. A new session resets all statistics. Leaving an already completed result returns to Ready; repeating still requires the ready-screen Run action.

Ready, single Ping:

```text
PING
192.168.1.20

READY

OK: RUN

HOLD OK: BACK
```

Success:

```text
PING
192.168.1.20
REPLY

RTT 2.3MS


OK: REPEAT
```

Timeout uses TIMEOUT at row 2 with no RTT. A stale target uses TARGET CHANGED or the relevant link/network message and hides measurements.

Continuous summary page:

```text
CONT PING 1/2
192.168.1.20
RUNNING
TX 42 RX 41
LOSS 2%
AVG 2.4MS
UP/DOWN OK: STOP
HOLD OK: BACK
```

Continuous detail page:

```text
CONT PING 2/2
192.168.1.20
RUNNING
LAST 2.0MS
MIN 1.4MS
MAX 7.8MS
UP/DOWN OK: STOP
HOLD OK: BACK
```

After Stop the state says STOPPED and the footer is UP/DOWN OK: REPEAT. Missing RTT values display N/A, not a fabricated zero. All rows fit 21 characters; font/controller/dimensions remain unchanged. Back hints remain contextual; no new hint appears on Home or locked OTA states.

## Statistics and future result serialization

The fixed-size record stores session ID, monotonic start/end microseconds, target identity, state, terminal reason/error, statistics and resource instrumentation. `utc_seconds` is explicitly zero as a placeholder until time synchronization exists. These fields can later be serialized; there is no file format, storage dependency, filesystem driver or history in this phase.

Counters are unsigned 32-bit TX/RX with a hard one-million-request session cap. Continuous Ping stops with Session limit at that cap, requiring explicit restart; it never wraps or silently discards old samples. This is approximately 11.6 days at one probe per second. TX increments live when send succeeds; send failures do not count a transmission. RX counts only valid matching replies.

Loss is `floor((TX-RX)*100/TX)`, using 64-bit multiplication, or zero with no transmissions. Last/min/max RTT are microseconds. Sum is 64-bit; average is integer `sum/RX`, with display rounded to tenths of a millisecond. Maximum accumulated sum is 750,000,000,000 microseconds, safely below the 64-bit limit. Each session resets the complete statistics structure. Last RTT is N/A for an in-flight or unanswered latest probe; min/average/max retain the valid-reply aggregates. No replies yield N/A RTT fields and 100% loss after transmissions.

Loss reflects replies accepted before termination. A cancelled in-flight request remains transmitted/unanswered; terminal reason distinguishes a user stop from a completed timeout. Link/network/target invalidation marks results invalid rather than presenting stale statistics as current.

## Files

Added by this phase:

- `main/ping_logic.h`: bounded statistics and SDK-independent ICMP request/reply validation.
- `main/ping_runtime.h`: fixed session/result model and nonblocking runtime API.
- `main/ping_runtime.c`: isolated worker, socket ownership, cancellation, target validation and reservation.
- `tests/verify_ping_runtime.py`: actual production runtime/codec/statistics fixtures with deterministic RTOS/socket/network mocks.
- `NETWORK_TOOLS_PHASE_A.md`: this report.

Modified by this phase:

- `main/CMakeLists.txt`: register the new runtime source only; no dependency or build-system patch change.
- `main/app_main.c`: initialize the optional Ping runtime with nonfatal error handling; Ethernet setup unchanged.
- `main/snmp_runtime.c`: shared diagnostic reservation around the existing queued operation; protocol unchanged.
- `main/ui_menu.c`: Network Tools and Host Actions choices, target routing, Ready/result screens and stop-before-Back behavior.
- `tests/verify_field_ui.py`: extend actual navigation/render/completion fixtures for Ping and preserve existing UI/button coverage.
- `tests/verify_snmp_runtime.py`: mock and verify reservation ownership/conflicts/release alongside the existing worker/HTTP fixtures.

The workspace already contained the uncommitted, validated 450 ms/contextual-hint refinement at task start. Its changes in `main/ui_buttons.c`, `main/ui_buttons.h`, `FIELD_UI_V1_GPIO_SD_AUDIT.md`, `main/ui_menu.c` and `tests/verify_field_ui.py` are retained. This phase does not alter the button implementation. Resource deltas below use the actual refinement binary/map preserved at this task's start, rather than the earlier committed image.

Web files, Ethernet, discovery source/timing, AutoTest, PROFINET source/protocol, SNMP codec/transport, firmware update mechanism, sdkconfig, root build patches, version and partition configuration are unchanged.

## Software validation

Normal ESP-IDF v6.1 / classic ESP32 / GCC 15.2 `idf.py build` and `idf.py size`: PASS, exit 0. `git diff --check`: PASS. Firmware remains `0.7.0-dev`.

All **20 regression commands pass**: 18 `verify_*.py` scripts and both Node UI suites. Evidence: `build/network_tools_tests.json`, `build/network_tools_verify_*.log`, the two Node logs, `build/network_tools_build_validated.log`, `build/network_tools_size_validated.log`, `build/network_tools_build_results.json` and `build/network_tools_metrics.json`.

New runtime tests execute production start/probe/process/stop/get logic, with deterministic time and IO. Coverage includes reply, timeout, zero replies, malformed/stale frames, every short-frame length, header/payload corruption, IP options/fragments, malformed/local/broadcast targets, target disappearance/MAC/run changes, link loss, revision/physical-epoch changes, queue/task/socket/bind/send/select setup failures, stop before dequeuing and during a continuous receive wait, second Start while active, OTA interruption, approximately one-second cadence, repeated sessions/counter reset, cap termination and worst-case accumulator math. One hundred repeated simulated sessions keep one worker, at most one socket, matching opens/closes and released reservations. Socket descriptor zero is explicitly exercised. Malformed traffic cannot prolong the absolute timeout.

UI fixtures exercise the actual menu/render/navigation/completion branches: tool selection, discovered-host Ready routing, no accidental start on navigation, Single Ping Run/Reply/Repeat/Back, Continuous Run/Stop/Back waiting for cleanup, two pages and N/A fields, busy and stale/link-loss messages, host-action routing and existing feature/hint/row-bound behavior. The existing 449/450 ms button tests remain passing.

SNMP fixtures retain the transport/HTTP/identity cases and add coordinator conflicts and cleanup. The existing discovery, ICMP degradation, hostname, AutoTest, link-loss, PROFINET, Switch Info, SNMP codec, Web and generated release-image regressions all pass. Tests are software fixtures and compile-time evaluation under the target compiler, not real-network or hardware acceptance.

| Regression command | Result |
| --- | --- |
| snmp_runtime_ui.test.cjs | PASS (exit 0) |
| verify_autotest_rules.py | PASS (exit 0) |
| verify_discovery_logic.py | PASS (exit 0) |
| verify_enhanced_api_ui.py | PASS (exit 0) |
| verify_enhanced_discovery.py | PASS (exit 0) |
| verify_fast_discovery.py | PASS (exit 0) |
| verify_field_ui.py | PASS (exit 0) |
| verify_hostname.py | PASS (exit 0) |
| verify_icmp_degradation.py | PASS (exit 0) |
| verify_link_loss.py | PASS (exit 0) |
| verify_ping_runtime.py | PASS (exit 0) |
| verify_profinet.py | PASS (exit 0) |
| verify_release.py | PASS (exit 0) |
| verify_snmp_runtime.py | PASS (exit 0) |
| verify_snmp.py | PASS (exit 0) |
| verify_switch_info_api.py | PASS (exit 0) |
| verify_switch_info_cdp.py | PASS (exit 0) |
| verify_switch_info_parse.py | PASS (exit 0) |
| verify_switch_info_ui.py | PASS (exit 0) |
| web_ui.test.cjs | PASS (exit 0) |

## Resource impact

| Resource | Task-start refinement baseline | Phase A | Delta |
| --- | ---: | ---: | ---: |
| Application binary | 513072 bytes | 518320 bytes | +5248 |
| Unpadded image from linker map | 512918 bytes | 518198 bytes | +5280 |
| Static DRAM | 143569 bytes | 143905 bytes | +336 |
| DRAM remaining | 37167 bytes | 36831 bytes | -336 |
| IRAM | 51003 bytes | 51003 bytes | 0 |

OTA slot size remains 1,740,800 bytes, leaving 1,222,480 bytes (about 70.2%) for the new image. Bootloader is unchanged at 26,240 bytes. Binary and partition integrity checks pass.

The new worker stack is 4096 bytes, priority 1. The existing menu/button/SNMP/discovery stacks are not reduced. The new queue has one four-byte item. Compiler size probes show the configured FreeRTOS queue control structure is 84 bytes and task control structure is 100 bytes: nominal startup allocation is 4096 + 100 + 84 + 4 = 4284 bytes before allocator overhead/alignment. Runtime status is 136 bytes, target identity 28 bytes; static copies are included in the DRAM delta. These ABI values were measured with the actual target compiler, not inferred from host Python types.

Socket use is one raw IPv4 ICMP socket while a session is active, zero after completion, and no socket while merely viewing Ready/results. The socket/mailbox/pbuf heap cost depends on actual lwIP traffic and is not established by the build. The runtime records heap before/after, minimum heap and worker stack high-water for hardware inspection. In mocks, a socket consumes an intentionally modeled 512 bytes; every tested terminal path restores the modeled baseline. That number is not an ESP32 heap estimate.

`-fstack-usage` measurements (`build/network_tools_stack_audit.log`) show unchanged UI frames: menu task 1200 bytes, render 560 bytes, navigation 432 bytes. New worker frames: worker 48, process 192, probe 208, interrupted 160, target check 112, discovered-host lookup 544. The deepest modeled worker path contributes about 1264 bytes before SDK calls; the result getter path from menu/render contributes about 2448 bytes before SDK calls. These are individual compiler frames and call-chain sums, not measured runtime high-water guarantees. Existing 4 KiB UI and new 4 KiB worker allocations are preserved; real hardware measurements remain required.

## Hardware acceptance still required

No device was flashed or accessed. Before acceptance, check OLED-only discovery/target selection, reply/timeout against known hosts, Continuous Stop and held-OK Back during receive/interval waits, both pages, zero replies, repeated sessions, competing Discover/Quick Test/PROFINET/Web-SNMP starts, cable removal/reconnect and IP changes, socket/heap recovery, worker/UI stack high-water and preserved existing diagnostic behavior. Software mocks cannot establish real network RTT accuracy, PHY recovery or actual heap margins.

Implementation and software validation stop here. No commit or push was made.

## Git diff stat

```text
 FIELD_UI_V1_GPIO_SD_AUDIT.md |  12 ++--
 main/CMakeLists.txt          |   2 +-
 main/app_main.c              |   3 +
 main/snmp_runtime.c          |  13 +++-
 main/ui_buttons.c            |   2 +-
 main/ui_buttons.h            |   2 +-
 main/ui_menu.c               | 152 +++++++++++++++++++++++++++++++++----------
 tests/verify_field_ui.py     | 136 +++++++++++++++++++++++++++++++++-----
 tests/verify_snmp_runtime.py |   8 +++
 9 files changed, 271 insertions(+), 59 deletions(-)
```

Git's tracked diff stat excludes the five new untracked files listed above. It includes the prior uncommitted UX refinement; no staging was performed.
