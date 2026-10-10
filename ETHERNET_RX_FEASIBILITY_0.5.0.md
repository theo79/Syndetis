# Syndetis 0.5.0 — Phase A: Ethernet RX feasibility

Date: 9 October 2026. Inspection only; no firmware implementation, configuration change, build, flash, or partition change performed.

## Decision

**LLDP and CDP are feasible at the software architecture level on the current ESP32 internal EMAC/LAN8720 path. Physical reception is not yet demonstrated.** Both must be intercepted before lwIP processes the Ethernet frame. Explicit destination MAC admission is necessary; a callback cannot recover a frame rejected by the MAC.

Recommended integration: retain the existing ESP-NETIF Ethernet glue and event handling, replace only its RX input callback using the public `esp_eth_update_input_path_info()` API after attachment and before driver start, admit the discovery multicast destinations with `ETH_CMD_ADD_MAC_FILTER`, and forward every other frame to `esp_netif_receive()`. Use a small preallocated capture pool and a nonblocking worker queue. No promiscuous mode is required.

This recommendation is supported by inspection of the locally installed SDK and project. It is not a claim that hardware reception, timing, heap margin, or regressions have been tested. Phase B requires approval.

## Evidence and baseline

- `version.txt` and `build/project_description.json` identify firmware 0.4.2. Build metadata identifies target `esp32`, IDF path `C:/esp/v6.1/esp-idf`, and SDK revision `v6.1`. This is build metadata, not proof of the image currently running on a board.
- SDK source files under that path were read directly. Repository identity could not be independently checked with `git describe` because access to the SDK Git repository was denied. API conclusions below concern the inspected local source, checked against the versioned official documentation.
- The working tree already contains substantial modified and untracked firmware/build files. They were inspected as the effective baseline and were not reverted or edited.
- Current `sdkconfig`: internal ESP32 EMAC enabled; DMA RX buffers = 10, DMA TX buffers = 10, each 512 bytes; L2 TAP disabled; Ethernet TX mutex disabled; TCP/IP task priority 18 and mailbox size 32. Default MAC configuration gives RX task priority 15 and stack 4096 bytes.
- Root `CMakeLists.txt` deliberately replaces the SDK DMA implementation with `patches/esp_eth_mac_esp_dma.c` and its ping implementation with `patches/ping_sock.c`. DMA ring-walk/CRC guards and ping cancellation guards are part of the baseline. Preserve them unchanged.

## Existing initialization and event architecture

`main/app_main.c` initializes NVS, ESP-NETIF and the default event loop; powers the external 50 MHz oscillator using GPIO16; waits 100 ms; creates the ESP32 RMII EMAC using MDC23, MDIO18 and external REF_CLK input GPIO0; creates the LAN87xx PHY at address 1 without a reset GPIO; installs the Ethernet driver; and creates `ESP_NETIF_DEFAULT_ETH()` plus the standard Ethernet glue.

At lines 102–106 it calls `network_status_init(eth_handle)`, attaches `eth_netif` to `eth_glue`, then starts Ethernet. HTTP startup and OTA boot confirmation follow; AutoTest, Fast Discover and optional UI initialization occur afterward. There is currently no application RX callback or discovery multicast configuration.

Important ordering detail: `esp_eth_new_netif_glue()` registers SDK event handlers immediately. `network_status_init()` therefore follows glue-handler registration even though it precedes attachment. Those SDK handlers use `base.netif`, which attachment supplies. Preserve the current initialization order in Phase B; the comment about publishing before SDK link-down handling should not be taken as proof of registration order. Auditing that existing behavior is separate from this RX feature.

`main/network_status.c` tracks link/IP lifecycle under a short critical section. It registers ETH and IP handlers, queues logs to a separate task, and performs netif inspection through a TCP/IP callback. AutoTest and discovery use lifecycle generations/revisions to cancel stale operations and check OTA state. Passive switch discovery must not depend on DHCP readiness, change these lifecycle counters, or hold their locks while processing packets.

## Verified receive path

The inspected SDK implements this chain:

```text
LAN8720/RMII → EMAC destination filtering → RX DMA ring
 → emac_rx task: allocate heap buffer, copy frame, release DMA descriptors
 → mediator stack_input_info
 → registered Ethernet input callback
 → default glue eth_input_to_netif
 → optional L2 TAP filter (currently disabled)
 → esp_netif_receive → ethernetif_input → custom pbuf → TCP/IP thread
```

Relevant SDK files:

- `components/esp_eth/src/mac/esp_eth_mac_esp.c`: `emac_esp32_rx_task()` at line 600; invokes `stack_input_info()` at line 630. It runs in a FreeRTOS task, not an ISR.
- `components/esp_eth/src/esp_eth.c`: mediator dispatch and input-path setters. Updating either input API clears the other callback and sets shared private context. This is replacement, not callback chaining.
- `components/esp_eth/src/esp_eth_netif_glue.c`: attachment installs `eth_input_to_netif` using the information-aware API; callback forwards to `esp_netif_receive(priv, buffer, length, NULL)` with TAP disabled. Its RX free callback calls `free()`.
- `components/esp_netif/lwip/esp_netif_lwip.c`: `esp_netif_receive()` dispatches to the configured lwIP input function.
- `components/esp_netif/lwip/netif/ethernetif.c`: wraps the driver buffer in a custom pbuf; frees it if netif is down, pbuf allocation fails or stack enqueue fails; upper layers release successful input.
- `components/lwip/lwip/src/netif/ethernet.c`: handles supported Ethernet protocols and drops unknown protocols unless an unknown-protocol hook takes them. No project LLDP/CDP hook was found.

The public input replacement APIs are documented in the [ESP-IDF 6.1 Ethernet reference](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-reference/network/esp_eth.html). Install the application callback **after attachment**, otherwise attachment overwrites it. Install it **before start**, because setter assignments are not a synchronized live callback swap.

## LLDP and CDP classification

### LLDP

Capture Ethernet II EtherType `0x88CC`, normally destination `01:80:C2:00:00:0E` for nearest-bridge discovery. IEEE describes the scope address and EtherType in its [LLDP scope discussion](https://grouper.ieee.org/groups/802/1/files/public/docs2025/new-bottorff-lldp-tlvs-for-lsvr-0425-v00.pdf). Broader LLDP scopes may use `01:80:C2:00:00:00` or `01:80:C2:00:00:03`; decide whether these are in scope before allocating additional MAC filter entries.

LLDP requires neither IPv4 nor DHCP nor an IP multicast membership. A normal IP raw socket is not the appropriate receive interface. Advertisements must actually be enabled and transmitted on the connected switch port. Nearest-bridge LLDP is link-local, so do not promise discovery of a switch beyond an intervening bridge; see the [IEEE forwarding interpretation](https://www.ieee802.org/1/pages/int-4.html).

### CDP

CDP uses destination `01:00:0C:CC:CC:CC`, an IEEE 802.3 length field, LLC `AA AA 03`, SNAP OUI `00 00 0C`, and SNAP PID `20 00`. **The PID is not an Ethernet II EtherType at bytes 12–13.** Verify the complete encapsulation, not destination alone: other Cisco control protocols share that destination. Cisco documents these fields in its [control protocol table](https://www.cisco.com/c/en/us/support/docs/switches/catalyst-6500-series-switches/24330-185.html).

For an untagged frame, LLC begins at byte 14 and CDP begins at byte 22. Require enough bytes, length-field consistency and a valid SNAP header; bound parsing to declared payload length rather than Ethernet padding. Validate CDP version, checksum, TTL and TLV lengths before publishing data. Cisco describes passive announcements, normally every 60 seconds, in its [CDP behavior note](https://www.cisco.com/c/en/us/support/docs/network-management/discovery-protocol-cdp/118736-technote-cdp-00.html). Absence during a short capture is inconclusive.

The inspected driver transports valid raw frames without EtherType selection before the application callback. LLC/SNAP does not require PHY changes. This establishes a software route for CDP; actual receipt of length-based frames through the installed MAC/DMA configuration remains a hardware test.

For VLAN-tagged traffic, a classifier can walk a bounded number of `0x8100`/`0x88A8` headers before interpreting the inner type/length. Preserve the full frame when forwarding. Explicitly test tagged discovery or state it unsupported initially; do not strip tags from normal traffic or divert all VLAN traffic to the discovery worker.

## Destination MAC filtering

The installed EMAC driver implements `ETH_CMD_ADD_MAC_FILTER`, `ETH_CMD_DEL_MAC_FILTER`, and `ETH_CMD_S_ALL_MULTICAST` through public ioctl dispatch, MAC methods and HAL register operations. Its default initialization disables promiscuous and receive-all modes, enables broadcast reception and normal destination filtering. No project code admits LLDP/CDP addresses or enables all multicast. Thus default reception of these addresses must **not** be assumed.

Recommended: add nearest-bridge LLDP first; add the CDP destination when CDP capture is enabled. Check each return value, track only entries successfully added, and do not delete lwIP-owned entries. Hardware filter capacity is finite: ESP32 LL defines `EMAC_LL_MAX_MAC_ADDR_NUM` as 8 and the automatic allocator scans additional address indices 1–8. IPv4 IGMP and IPv6 MLD also use the driver filter API. Capacity and concurrent membership changes must be considered.

If filter addition fails, leave IP networking working and report discovery unavailable/degraded. `ETH_CMD_S_ALL_MULTICAST` is a supported fallback in this implementation, but broadens received load and needs an explicit design decision and stress validation. Do not silently enable it. MAC filtering is separate from TAP software filtering; opening a TAP descriptor does not admit a destination at hardware level.

The source also implements promiscuous mode, but the proposal does not use or rely on it. The HAL blocks MAC control frames by default; LLDP is not an Ethernet MAC-control (`0x8808`) frame. That setting alone is not evidence that all link-local multicast is blocked. Confirm destination admission with injected LLDP/CDP traffic on hardware.

## Supported integration choices

### Recommended: input callback plus bounded capture pool

Use `esp_eth_update_input_path_info(handle, callback, permanent_context)` after standard attachment. Context contains the existing netif, bounded capture resources and counters. The callback checks header bounds and classifies only enabled discovery protocols. It copies a matching frame into an available preallocated slot and queues its slot index with zero wait. On no slot, oversize or queue full, count the discovery drop. Free the original matching driver buffer exactly once and return `ESP_OK`, including intentional discovery drops.

For **every other frame**, immediately return `esp_netif_receive(context->netif, buffer, length, NULL)`. Do not free or access that buffer afterward, even if the return is an error: the inspected Ethernet input path already releases it on its error paths. Do not return an error expecting the MAC RX task to clean up; it ignores the callback return value.

Retain standard glue for TX configuration, RX freeing and events. The application replaces only input routing; it cannot call the private static glue callback or obtain its pointer through a public getter. Forwarding directly reproduces its current TAP-disabled path. If TAP is enabled later, this wrapper bypasses TAP and must be reviewed; do not combine the two architectures accidentally.

### Alternative: ESP-NETIF L2 TAP

Enable `CONFIG_ESP_NETIF_L2_TAP`, register its VFS, open two descriptors, bind to `ETH_DEF` or the Ethernet driver, and set receive filters: LLDP `0x88CC`; length-based traffic with a filter at or below `0x05DC`. The application then resolves LLC/SNAP for CDP. TAP leaves unmatched IP/ARP frames on the normal netif path. These behaviors are documented in the [ESP-NETIF 6.1 manual](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-reference/network/esp_netif_programming.html).

Local source confirms TAP queues matching original driver buffers, consumes them instead of forwarding to lwIP, drops/frees on queue overflow, and copies/frees on `read()`. An undersized user read buffer truncates the frame and discards its remainder. Choose a full-frame buffer. Only one descriptor should claim the length-based class; avoid overlapping filters.

TAP is a credible supported alternative, especially for a simple untagged LLDP-only receiver. It is not preferred for this combined feature because it captures all length-based traffic for CDP, retains variable heap buffers in its queues, does not resolve inner VLAN protocols, and its Kconfig selects `ETH_TRANSMIT_MUTEX`, changing the existing TX configuration. Its default queue depth is 20 per descriptor; reduce and budget explicitly if chosen.

## Ownership, concurrency and loss

- RX callback receives a heap copy, not a pointer to DMA memory. The patched DMA allocator uses `malloc`; the DMA copy subtracts the four-byte FCS. Capture data includes Ethernet headers and padding, excludes preamble/FCS. Never subtract FCS a second time.
- The received pointer remains valid only until freed or handed to ESP-NETIF. Never queue a borrowed pointer and simultaneously forward it. The recommended copy pool avoids long-lived retention of driver buffers.
- RX metadata `info` is not permanent storage. On this ESP32 path the timestamp pointer is NULL; other targets may provide a stack-local timestamp. Do not retain it.
- Perform no TLV parsing, socket operations, logging per frame, OLED rendering, HTTP formatting, blocking queue sends or long lock operations in RX context. All packets share the same RX task.
- Proposed initial budget: four full-frame slots of roughly 1.6 KiB each, a slot-index queue, a 3–4 KiB low-priority worker stack, and bounded neighbor records. Approximately 10–12 KiB added static/task storage, excluding exact RTOS overhead and records; verify against measured heap and binary size. This is a proposal, not a measured allocation.
- Use a worker priority below EMAC RX and TCP/IP; give normal networking priority. Bound TLV count, string lengths, neighbor count, nested VLAN count and processing time. Publish snapshots under a short lock; copy snapshots before HTTP/UI formatting. Treat received strings as untrusted data and escape HTML/JSON/control characters.
- Stamp captured frames with current link generation and receive time. Reject mismatched generations; clear/expire records on disconnect/stop and TTL expiry. Frames queued near link transitions can otherwise populate a new connection with old switch data. An asynchronous event boundary still has a small race window; make generation checks conservative and test rapid unplug/replug. Never block event callbacks on the worker.
- Loss can occur before interception due to destination filters, CRC/length errors, DMA saturation or driver allocation failure; after interception due to pool/queue exhaustion or parser rejection. Ten 512-byte DMA descriptors do not mean ten full-size frames of capacity.
- Floods can exhaust RX bandwidth even with bounded application storage. Exact multicast filters minimize exposure but do not stop malicious traffic to those addresses. Queue overflow should sacrifice discovery packets, not delay IP.
- Counters should distinguish candidate, queued, oversize, pool-full, queue-full, malformed and stale frames. They cannot account for frames rejected before the callback without extra driver/hardware instrumentation. Lossless capture is not promised.

## Minimal passive architecture

1. Initialize fixed resources and worker before hooking RX; on failure keep the existing input callback.
2. After normal attachment, before Ethernet start, add requested destination filters and install the callback only when its permanent resources are valid. Optional-feature failures must not abort Ethernet/HTTP/OTA boot confirmation.
3. RX classifies and queues bounded copies; unhandled traffic goes straight to existing netif input. No discovery transmissions or IP multicast joins.
4. Worker parses LLDP first, then CDP; stores protocol identity, source MAC, chassis/device ID, port ID, optional name/description/capabilities/management address, receive timestamp and TTL in bounded records. Optional fields are not required for a valid observation.
5. Expose snapshot/status accessors; lifecycle and TTL processing prevent stale display. Keep this passive service independent from AutoTest/Fast Discover scheduling and diagnostic ownership.
6. During OTA, retain fast RX handling and buffer draining; optionally discard candidates rather than parsing them. Do not close/reconfigure the driver from an OTA handler.

## Existing-feature risks and acceptance gates

| Feature | Main risk | Phase B verification |
|---|---|---|
| DHCP / IPv4 / ARP | Misclassification, wrong netif, buffer double-free, RX delay | DHCP from cold boot and renewal; ARP/IP reachability; operate with discovery absent, malformed and flooded |
| IPv6 / multicast | Hardware filter capacity or deleting shared entries | Check existing MLD memberships and admission failures; preserve existing configuration |
| AutoTest | RX latency and heap pressure affecting ping/DNS/TCP timing | Compare results/timeouts to 0.4.2; cancellation and link-generation behavior |
| Fast Discover | Reduced packet-processing budget during concurrent scan | Normal scan and flood; cancellation, progress and expected discovery results |
| HTTP / UI | Long locks, unsafe network strings, memory growth | Poll while scanning/capturing; missing OLED; bounded response formatting and responsive buttons |
| OTA | RX starvation, flash-write stalls, image growth, changed boot failure behavior | Authorized upload while capturing, unauthorized rejection, interruption/recovery and boot confirmation; verify image fits existing OTA slot |

First prove real reception with a directly connected advertising switch or controlled Ethernet injector: known LLDP and CDP frames, observed source/capture evidence, no DHCP dependency, and successful MAC-filter ioctl results. Test unicast/broadcast/IP continuity, non-CDP Cisco SNAP traffic, truncated/malformed TLVs, oversize frames, TTL zero/expiry, link cycling and sustained traffic. Measure free/minimum heap, worker stack watermark, pool drops and HTTP/OTA throughput against the existing image. No build, board test, or benchmark was performed in Phase A.

## Exact proposed file scope

For the recommended passive receiver core:

- **Modify `main/app_main.c`**: initialize receiver and install after `esp_netif_attach()` and before `esp_eth_start()`; handle optional setup failures without aborting baseline networking.
- **Modify `main/CMakeLists.txt`**: add new source files. Existing esp_eth, esp_netif, FreeRTOS and esp_timer dependencies suffice.
- **Add `main/switch_info.c` and `main/switch_info.h`**: capture pool, callback routing, worker, lifecycle/TTL management, snapshots/counters.
- **Add `main/switch_info_parse.c` and `main/switch_info_parse.h`**: bounded LLDP/CDP parsing with a host-testable interface.
- **Add `tests/switch_info_parse_test.c` and `tests/verify_switch_info_parse.py`**: parser/classifier fixtures and ownership-routing checks appropriate to implementation.

Only if Phase B includes user-visible Switch Info:

- **Modify `main/ui_menu.c`** for menu/display integration using snapshots.
- **Modify `main/web_server.c` and `main/web_index.html`** for status/API and escaped display.
- **Modify `tests/web_ui.test.cjs`** if browser behavior changes.

At release, update `version.txt` and `README.md`; do not label unvalidated feasibility code as a completed 0.5.0 release.

No required changes to `main/network_status.c/.h`, `main/autotest.c`, `main/discovery.c`, `main/diagnostic.c`, `main/firmware_update.c`, board pins, root `CMakeLists.txt`, existing patches, installed SDK, partition layout, or sdkconfig for the recommended route. Existing lifecycle snapshots can be reused without modifying their producers. If TAP is selected instead, `sdkconfig` and `sdkconfig.defaults` additionally need explicit TAP settings and its selected TX mutex must be regression-tested.

**Phase A conclusion:** proceed to an approved, bounded LLDP receiver using the public RX input API, with CDP classification supported by the same interception point. Treat hardware admission and regression gates as outstanding validation, not assumptions of success. No Phase B implementation has begun.
