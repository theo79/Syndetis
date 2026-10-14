# Syndetis 0.8.0 — Opt-in SNMP runtime integration

2026-10-12. Implementation and software validation complete; hardware acceptance remains pending. Firmware version is still **0.7.0-dev**. Nothing staged or committed; no push, flash, OTA, subnet scanning, live agent query, or source refactor performed.

## Architecture and file scope

The existing Web UI exposes a discovered-host table rather than a details page. One optional action per table row now submits an identity query and displays its latest result below that table. The existing HTTP server still has one task and the same 12288-byte stack. The discovery snapshot/device APIs, record layout, ARP/ICMP timing, Ethernet configuration, shared RX, PROFINET and SNMP protocol codec/transport remain unchanged.

Added:

- `main/snmp_runtime.h`: status, stable target identity, start outcomes and public worker API.
- `main/snmp_runtime.c`: serialized queue/worker, protected latest result, identity/network/run guards and resource telemetry.
- `tests/verify_snmp_runtime.py`: evaluates production worker and HTTP control flow with target GCC and deterministic mocks.
- `tests/snmp_runtime_ui.test.cjs`: exercises the actual embedded JS/button with browser/network/timer mocks.
- `SNMP_RUNTIME_0.8.0.md`: this delivery and acceptance report.

Modified:

- `main/CMakeLists.txt`: registers the isolated runtime C module.
- `main/app_main.c`: initializes the optional worker after discovery initialization. Failure logs a warning; Ethernet/HTTP startup is not aborted.
- `main/web_server.c`: two SNMP routes, bounded input parsing and streamed escaped output; URI capacity increases from 8 to 10 for the two extra handlers.
- `main/web_index.html`: one action column and result panel; existing fields/controls retained.
- `tests/verify_profinet.py`: narrows its production HTTP fixture extraction to the PROFINET handlers, excluding the new independently tested SNMP handlers. PROFINET firmware behavior is unchanged.

## Worker and synchronization

One priority-1 FreeRTOS task with **4096 bytes of stack**, one depth-one queue, and one fixed latest-status structure. The idle worker blocks on its queue. There is no startup request, periodic query, per-host cache, query history, or background scan. Accepted requests become queued before the nonblocking queue send; queued/running requests reject further starts. No second query accumulates behind an active one.

A portMUX protects only bounded state copies/changes. Network operations, discovery reads, queue operations and JSON sends occur outside that lock. HTTP only submits or copies status; the worker alone calls the existing synchronous SNMP API. The worker does **not** acquire the shared diagnostic reservation, so it cannot reserve/block a later discovery/AutoTest/PROFINET operation. Starts are rejected as busy while discovery or hostname enrichment is active. An already-running query is invalidated if discovery restarts.

Each job retains numeric IPv4, MAC and its presence flag, a random-seeded incrementing nonzero request ID, network revision, physical epoch, and discovery start timestamp. Host lookup checks discovery completion/run before and after its bounded record search. Network generation is checked around that lookup. All identities are revalidated before network IO, after IO, and before exposing a completed result. Discovery-table indices are never retained and the table is never modified. An old queued/completion ID cannot overwrite a newer request.

The worker uses fixed-default v2c/public via the existing foundation. There are no protocol/version/community/SET/WALK options in the HTTP API. Existing v1 support in the foundation remains unchanged.

## State machine and API

`idle -> queued -> running -> success | timeout | error`

Every accepted request clears previous identity data and increments its request ID. Busy does not enqueue or replace the active job. Missing/stale hosts, unavailable worker/network and invalid input do not enqueue. Queue failure yields an error and a later request can retry. Link loss, network changes, rediscovery and host/MAC disappearance invalidate the result with ESP_ERR_INVALID_STATE. Completed result snapshots are also guarded against later identity changes.

- **POST `/api/snmp/query?ip=192.168.1.20&mac=001122334455`**: empty body and `X-Syndetis-SNMP: 1` header required. MAC is twelve hex digits and must match the current discovery record. Omit MAC only when the discovery record has no MAC. Numeric IPv4 only; no hostname resolution. Input buffers have fixed limits; no HTTP body read/wait. Header requirement prevents simple cross-origin form submissions; no CORS is enabled.
- Accepted: **202**, JSON `{ "state": "queued", "request_id": ... }`.
- Busy: **409**; stale/missing identity: **404**; unavailable/network: **503**; malformed input: **400**.
- **GET `/api/snmp/status`**: latest state, request ID, target IP/MAC, error code and resource telemetry. Only validated success includes `sysName`, `sysDescr`, `sysObjectID`, `sysUpTime` (TimeTicks), `sysContact`, `sysLocation`. Strings are JSON-escaped. No community is exposed.

HTTP client disconnect does not cancel, duplicate or strand the worker. Its query still terminates under the existing 750-ms-per-attempt / one-retry bounds (up to 1500 ms receive wait plus scheduling/IO overhead). No synchronous SNMP query executes in HTTP.

## Browser behavior

Query SNMP is explicit, never invoked when discovery/results load. A click submits one POST and disables query actions while pending. It polls only that request at one-second intervals, with at most twenty status reads. Both headers and JSON body are covered by a three-second fetch abort timer. Terminal/failure/superseded states stop timers; there are no automatic retries. Busy is shown as busy, not accumulated.

Request ID, IP and MAC must match before rendering. Existing discovery refreshes invalidate the selected result on host identity/run changes. All six fields are inserted using textContent; absent strings show `-`, and uptime is rendered as days/hours/minutes. The upload and other existing UI behavior passes its original regression suite.

## Resource impact

Measured by the normal ESP-IDF 6.1.0 / ESP32 / GCC build and `idf.py size`:

| Resource | Foundation baseline | Runtime | Delta |
|---|---:|---:|---:|
| Application binary | 497984 bytes | 509280 bytes | +11296 |
| Static DRAM | 141817 bytes | 142849 bytes | +1032 |
| Static DRAM remaining | 38919 bytes | 37887 bytes | -1032 |
| IRAM | 51003 bytes | 51003 bytes | 0 |
| Flash code | 320646 bytes | 327114 bytes | +6468 |
| Flash data | 114384 bytes | 119216 bytes | +4832 |

Application partition headroom is **1231520 bytes (71%)** of 1740800. Bootloader remains **26240 bytes**, with 2432 bytes free. Metadata and binary descriptor both report **0.7.0-dev**. Final C build has no compiler warnings/errors; existing SDK CMake/Kconfig notices were emitted.

The +1032 static-byte growth includes the protected worker context and fixed HTTP status snapshot. The worker additionally allocates its 4096-byte stack and RTOS task/queue overhead from heap at initialization. Each query still allocates the foundation's bounded **1729-byte scratch** and one UDP socket; RTOS/socket/pbuf overhead is additional. No large discovery-table copy was added.

The status API instruments `heap_before`, `heap_after`, `minimum_heap` and `worker_stack_free_bytes` after each request. The minimum heap is system-wide since boot, not exclusively SNMP. ESP-IDF's stack high-water mark is in bytes. **Real on-device heap values and worker high-water mark have not been measured**: no hardware has been flashed or runtime firmware deployed during this task. Mock fixture values are not hardware measurements.

## Validation

Normal build and size: PASS. All **16 verify_*.py scripts and both Node UI suites pass** (18 commands, exit 0). Logs: `snmp_runtime_build_final.log`, `snmp_runtime_size.log`, `snmp_runtime_test_results.json`, `snmp_runtime_metrics.json`, individual `snmp_runtime_verify_*.log` and UI logs. These generated files are ignored by Git.

New fixtures exercise actual production bodies with constexpr-compatible library/SDK adapters. Coverage includes idle start, queued/running serialization, busy/reentrant requests, setup/queue failure and retry, MAC-known/unknown identity, disappearance before and during query, run changes during query, stale IDs, timeout/failure, link loss and recovery, success invalidation, lock boundaries and **40 repeated simulated queries (20 successes/20 timeouts)**. HTTP fixtures cover input rejection, 202/409/404/503, status fields/escaping, omission on failure and POST/GET client disconnect. Existing foundation tests cover wrong community, unreachable peer and malformed BER/floods with mocked sockets/time. Browser fixtures cover no automatic request, one POST, busy, timers/abort, missing fields, safe strings, stale ID/MAC/run, terminal states and disconnected clients.

The initial PROFINET test failure was fixture extraction extending into the new SNMP handlers. Its extraction boundary was corrected; all original PROFINET transport/router/API/OLED/menu cases pass. No PROFINET production source changed.

## Hardware acceptance and limitations

**Software validation passed; hardware acceptance is pending.** No suitable deployed runtime firmware/agent access was supplied, and flash/OTA are prohibited for this task. Physical responding/non-agent/wrong-community/unreachable/malformed-peer behavior, twenty real repeated queries, heap stability, worker watermark and live Ethernet/discovery/UI concurrency remain to be observed.

The latest job/result is shared by all HTTP clients; request IDs prevent cross-client confusion, and a second active trigger receives busy. Only completed discovery with finished hostname enrichment can start SNMP. An in-flight query is not forcibly interrupted on link loss; its bounded IO finishes and its result is invalidated. Without a MAC, identity relies on IPv4 and the discovery run. Directed broadcasts must not be passed to the underlying raw-IP API; the runtime accepts only existing discovered records.

No SET, GETNEXT/WALK, interface polling, v3, community configuration, automatic queries, history, or runtime version bump. No staging was performed; `git diff --cached --stat` is empty.
