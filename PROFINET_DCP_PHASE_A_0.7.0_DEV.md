# Syndetis 0.7.0-dev Phase A: PROFINET DCP feasibility and architecture

Assessment date: 10 October 2026. Current firmware remains **0.6.0-dev**. This document is the only project change in this phase. No source, configuration, partition, version, firmware image, flash or OTA operation is part of this assessment.

## Decision

**Conditional GO for Phase B implementation of Identify only.** The installed ESP-IDF and current RX interception provide a viable L2 path without DHCP. Use one fixed Identify-All builder, one serialized TX callback, a separate bounded capture pool, and a MAC-keyed 32-device table. Do not transmit directly from a new worker with the current configuration: `CONFIG_ETH_TRANSMIT_MUTEX` is disabled and the DMA descriptor cursor is not protected against concurrent callers.

The recommended alternative is `tcpip_try_callback()` followed by `esp_eth_transmit()` **inside the TCP/IP task**, using permanent job storage. Source inspection establishes serialization against the current lwIP transmit paths; this is conditional on retaining the current configuration and prohibiting other direct raw-TX callers. Physical interoperability, burst handling, cancellation races, and runtime memory/stack margins still require the validation gates below. This is permission to consider a design, not a claim of implemented or hardware-validated support. Stop after this report for approval.

## Evidence and limits

Labels used here:

- **SDK verified:** read from installed `C:/esp/v6.1/esp-idf` and the actual project replacement sources.
- **Reference verified:** protocol fields cross-checked against primary implementation sources.
- **Design:** proposed Syndetis behavior or calculated estimate, not existing firmware behavior.
- **Hardware pending:** requires WT32-ETH01 and real device/capture testing.

Reviewed project areas: `app_main.c`, `discovery.c`, `discovery_logic.h`, `switch_info.c`, LLDP/CDP parsers and selection, `network_status.*`, `diagnostic.c`/`diagnostic_rules.h`, OLED menu and Switch Info renderer, HTTP serializers, `sdkconfig`, root/component CMake, DMA and ping patches. Reports reviewed include Ethernet RX Phase A, LLDP Phase B, OLED Phase C, CDP Phase D, Enhanced Discover Phase A, hostname Phase B and ICMP hardening. Existing changes in the working tree predate this report and were preserved.

Protocol references consulted:

- [Wireshark DCP dissector](https://raw.githubusercontent.com/wireshark/wireshark/master/plugins/epan/profinet/packet-pn-dcp.c): header/block decoding and Identify response interpretation.
- [rt-labs p-net DCP implementation](https://raw.githubusercontent.com/rtlabs-com/p-net/public/src/common/pf_dcp.c): responder construction, timing formula and allowed factor range.
- [Scapy DCP implementation](https://raw.githubusercontent.com/secdev/scapy/master/scapy/contrib/pnio_dcp.py): independent Identify-All selector and response block cross-check.
- [p-net Ethernet dispatch](https://raw.githubusercontent.com/rtlabs-com/p-net/public/src/common/pf_eth.c): VLAN-aware frame-ID dispatch.
- [PI commissioning guideline, September 2022](https://www.profibus.com/fileadmin/media/downloadsection/installation_guide/PROFINET_Commissioning_8082_V153_Sep22.pdf): operational reasons to minimize DCP multicast traffic after startup.
- [PI identification numbers](https://www.profibus.com/products/ident-numbers): authority and distinction of Vendor_ID and Device_ID.
- [Manufacturer's PROFINET switch manual](https://imager.lapp.com/e/lapp/JVoTDAG67MRVed2yAGbsIA~~/BZ21700140_MANUAL_EN.pdf): priority conventions for cyclic I/O, alarms and MRP, not evidence that Identify must use their priorities.

The public PI commissioning document is not the complete normative wire specification. Implementation verification is not a certification assertion. In particular, full normative DCP PCP requirements and client timing conformance should be checked against the applicable PI/IEC edition before claiming conformance. Public implementations use different defaults; their code must not be copied wholesale into this project. Freeze reference revisions and retain captured fixtures in Phase B.

## 1. Ethernet framing and Identify header

**Reference verified:** Ethernet type `0x8892`; Identify request FrameID `0xFEFE`; response `0xFEFF`; service ID `0x05`; request type `0x00`; successful response type `0x01`. All multibyte numeric fields are big endian. XID is 32 bits. The two bytes after XID are ResponseDelayFactor on the Identify request and reserved on the response. The DCP header is 10 bytes, following the separate two-byte FrameID.

Untagged offsets and recommended values:

| Offset | Size | Request | Successful response |
|---|---:|---|---|
| 0 | 6 | `01:0E:CF:00:00:00` | Requester's actual Ethernet MAC |
| 6 | 6 | Actual Ethernet source MAC | Responder MAC |
| 12 | 2 | `88 92` | `88 92` |
| 14 | 2 | `FE FE` | `FE FF` |
| 16 | 1 | `05` | `05` |
| 17 | 1 | `00` | `01` |
| 18 | 4 | Current XID | Echoed XID |
| 22 | 2 | `00 80` proposed factor | Reserved, normally zero |
| 24 | 2 | `00 04` | Length of response block region |
| 26 | variable | `FF FF 00 00` | Response blocks |

Identify-All has one option/suboption `FF/FF` block with **zero value length**, no BlockInfo or qualifier, and no filter value. Thus the meaningful untagged request is 30 bytes. Scapy's explicit Identify-All example and builder use DataLength 4 and BlockLength 0. A p-net explanatory comment says BlockLength 2; that comment disagrees with the independent builder and must not be used to add a fictitious qualifier. Retain an exact-byte request fixture and compare with a real engineering-tool capture before field acceptance.

Design example for source `02:11:22:33:44:55`, XID `0x12345678`:

```text
01 0E CF 00 00 00 02 11 22 33 44 55 88 92
FE FE 05 00 12 34 56 78 00 80 00 04 FF FF 00 00
[30 zero octets of Ethernet minimum-frame padding]
```

The illustrative source is a fixture only. Production must never hard-code it. DCPDataLength excludes Ethernet header, FrameID, DCP header, Ethernet trailing padding and FCS; it includes the block headers, values and block alignment padding.

## 2. Read-only boundary

TX whitelist: only EtherType `8892`, FrameID `FEFE`, service `05`, type `00`, the fixed All/All zero-length selector, actual source MAC, current XID and fixed allowed delay. No generic caller-supplied option, service, destination, payload or frame-ID argument. Expose `build_identify_all(mac, xid, out)` rather than a general DCP command API. Make TX private to the scanner; assert fixed output size and constants in host tests and source-level checks.

RX record whitelist: EtherType `8892`, FrameID `FEFF`, service `05`, exact successful type `01`, destination equal to our MAC, valid unicast/nonzero source, current XID and active lifecycle/session. Type `05` is an unsupported-service response, not successful discovery: reject for records, optionally count it. Reserved type-bit combinations are not accepted as success.

Get/Set share FrameID `FEFD` and use services `03`/`04`; Hello is `FEFC`/`06`. None belongs in this scanner. Reset, Signal, name/IP assignment and transaction controls must have no builder, dispatcher or UI/API action. An advertised DeviceOptions pair describing Set capability is merely data; skip it or count options, never dispatch it. Unknown response blocks are length-checked and skipped, including control/manufacturer blocks. No response triggers another request.

## 3. Multicast and source MAC

Identify uses `01:0E:CF:00:00:00` for TX. The p-net responder copies the request Ethernet source into response destination and retains XID. Consequently **no additional RX filter is required** for this requester-only feature; ordinary local-unicast admission already applies. Do not add the Identify multicast RX entry. Hello multicast ending in `:01` is irrelevant. No promiscuous/all-multicast mode or passive request/Hello monitoring is proposed.

**SDK verified:** `esp_eth_ioctl(handle, ETH_CMD_G_MAC_ADDR, mac)` is supported. `network_status_init()` already uses it and stores the Ethernet MAC. Phase B should obtain/check the driver MAC during scanner initialization; reuse the verified cached value only while the same driver/MAC configuration remains in force. Reject zero/multicast source MAC or failed ioctl. No random MAC, OUI-based substitution or Wi-Fi MAC API.

Hardware pending: confirm destination in a real response, successful unicast reception with existing filters, and any vendor-specific deviation. Do not broaden filtering to accommodate a speculative deviation.

## 4. Installed TX path and ownership

**SDK verified:** public `esp_eth_transmit()` in `components/esp_eth/src/esp_eth.c:377` calls the MAC transmit method. `esp_eth_netif_glue.c` registers this same function as ESP-NETIF's transmitter. `esp_eth_mac_esp.c:548` calls `emac_esp_dma_transmit_frame()` from the project replacement `patches/esp_eth_mac_esp_dma.c:137`, not a hypothetical older driver. Root CMake replaces exactly that SDK source.

The DMA routine synchronously copies caller bytes into driver-owned DMA buffers, then hands descriptors to hardware and advances the shared cursor. Caller storage is reusable when the call returns, including an error return; it is not retained for asynchronous DMA. Stack storage is valid for a synchronous call, but **not** for a queued callback that may run after its submitting worker returns. Use a permanent 60-byte TX job buffer until that callback acknowledges retirement. Static application storage needs no DMA allocation for this copy path. This ownership conclusion is specific to the current ESP32 MAC/replacement source, not all ESP-IDF MAC drivers.

DMA descriptors are zero-initialized; DisablePad and DisableCRC remain clear on this path. HAL descriptor definitions document automatic short-frame padding and CRC insertion. Hardware therefore supplies minimum-frame padding and FCS. Nevertheless design the builder to explicitly zero-pad to **60 bytes excluding FCS**, making bytes deterministic and avoiding dependence on implicit pad contents. Send no FCS, preamble or SFD. Wire minimum is 64 bytes including FCS. Do not increase DCPDataLength to count Ethernet padding.

The driver rejects link-down with `ESP_ERR_INVALID_STATE`; null buffer/zero length are invalid arguments. Always pass an initialized, retained valid handle (the implementation tests its link before its later handle check). MAC returns `ESP_ERR_NO_MEM` when descriptors/buffer capacity are unavailable. With mutex enabled there is also a 250 ms acquisition timeout. `ESP_OK` means accepted/copied into TX DMA, **not proven wire delivery** or an answer. Phase B records the error and ends without automatic retry; test the actual wire length/FCS with capture equipment where visible.

## 5. Concurrency and configuration decision

Current `sdkconfig:1186`: `# CONFIG_ETH_TRANSMIT_MUTEX is not set`. DMA TX uses a shared descriptor pointer and owner checks without a caller mutex. The MAC's separate multi-register mutex protects register operations, not this copy/cursor sequence. A DCP-worker-only mutex does not protect lwIP. The diagnostic coordinator also does not serialize DHCP, HTTP, ARP replies or ping socket output.

**Unsafe:** arbitrary worker `esp_eth_transmit()` concurrent with normal networking. **Source-verified serialization under current architecture:** queue a nonblocking `tcpip_try_callback()`; execute the one raw TX call in `tcpip_thread`. The installed core-locking configuration is disabled. lwIP sockets, including the ping patch's raw-IP `sendto()`, use netconn/API messages; packet input/timers and ordinary output execute in that core context. `ethernetif.c`'s `ethernet_low_level_output()` calls `esp_netif_transmit()`, which calls the driver's registered transmitter. Project searches found no independent direct Ethernet TX caller. One callback on that task cannot overlap another normal lwIP output on that task.

Required design safeguards: permanent job storage; at most one pending callback; immutable job token/XID/epoch/MAC/frame; nonblocking posting; short callback with no parsing/logging/network inspection/waiting; recheck token, link/epochs, cancellation, OTA and absolute TX deadline before sending; publish result and acknowledge retirement. Worker wait at most 1 s. A delayed callback after timeout must perform **no TX** and retire its storage. Quarantine a pending job; do not reuse it for another scan. A full callback mailbox returns failure, with no retry. Start the response deadline from actual successful TX acceptance, not submission time. No callback means no discovery frame.

This proof is bounded to current callers/configuration. A future direct raw-TX feature, L2 TAP TX, alternate output task, or core-locking change requires a new audit or driver-wide protection. Add a compile-time guard/review requirement for changed core-locking/TAP settings and a test/source audit of raw TX call sites. Hardware stress remains necessary even though serialization follows from the inspected code. Stop/link event processing can occur during TX; a frame already handed to DMA cannot be recalled. Describe abort as preventing further work/publication, not unsending a frame.

**Recommended required sdkconfig changes: none.** Keep L2 TAP disabled and use callback serialization. Alternative if independent application TX is deliberately chosen: `CONFIG_ETH_TRANSMIT_MUTEX=y` in sdkconfig and project defaults, with the same SDK rebuild and regressions. It adds one dynamically allocated FreeRTOS mutex plus a handle per Ethernet driver (budget about 0.1–0.2 KiB including allocator overhead; exact size requires ABI measurement). Every normal TX acquires/releases it and may wait up to 250 ms, adding scheduling/contention cost; no new DMA buffer is implied. This is not necessary for the recommended serialized path and was not enabled. It also does not by itself solve driver stop/reset lifecycle races.

## 6. RX owner and exact classifier

`switch_info_init()` currently replaces the glue input with `rx_input()` before Ethernet start. Untagged LLDP/CDP candidates use four 1600-byte capture slots/queue entries; noncandidates go unchanged to `esp_netif_receive()`. Consumed buffers are freed once by the callback. The installed lwIP `ethernet.c` default branch drops unknown EtherTypes after taking ownership; unhandled `8892` normally reaches that branch, not an industrial parser.

Recommended integration is one shared input owner, keeping current LLDP/CDP classification/handling intact and calling a small independent DCP capture function for matching frames. Do not install a second replacement that overwrites Switch Info. Optional DCP initialization failure leaves that routing functional. Avoid making DCP availability depend on successful LLDP filter setup: Phase B should extract neutral RX routing/installation from Switch Info initialization or provide an equivalent safe fallback before start.

Exact future DCP classifier:

1. Check nonnull buffer and length at least 14. Read bytes; no unaligned struct casts.
2. Preserve destination/source. Accept untagged or one `8100` tag only, checking four tag bytes before advancing; FrameID offset becomes 18 when tagged. Preserve VID/PCP metadata. QinQ/`88A8`/a second tag is outside first scope and follows unchanged forwarding.
3. Require inner EtherType `8892` and all 12 bytes of FrameID plus DCP header present.
4. Require FrameID `FEFF`, service `05`, type `01`, own destination and valid source.
5. Require active collection, current XID and receive timestamp within TX/window bounds. Obtain current capture token/epochs in a bounded critical section.
6. Validate DataLength with subtraction-based available-length checks. Never read beyond it; cap total capture length at 1600. Reserve slot, copy and queue without waiting; count oversize/pool/queue drops. No parser, heap allocation or log in RX.
7. Free only buffers actually consumed by this path, exactly once. All other frames, including unrelated PN frames and normal tagged traffic, retain original pointer/length and go to `esp_netif_receive()`.

Malformed recognized responses may be consumed/count-dropped once their fixed Identify signature is established; they never produce records. Outside an active session they can follow normal unknown-protocol forwarding. Recheck all criteria in the worker before parsing and again before publication; fast classification is not trust. Explicit whitelist excludes cyclic RT, alarms and PTCP even though they share EtherType. MRP uses its own Ethernet protocol and also cannot match this signature. No transit forwarding or PROFINET runtime participation is being added.

## 7. VLAN and priorities

Design: initially **untagged TX**. p-net constructs an untagged response and its Ethernet receiver accepts VLAN frames; Scapy's Identify-All example is untagged. These establish a practical implementation precedent, not a survey of all hardware. Add bounded single-802.1Q RX, including VID 0 priority tags, to avoid a cheap interoperability limitation. Nonzero VID response tags may be parsed as received metadata; this does not allow probing another VLAN or stripping tags for ordinary IP. No tagged TX, VLAN selection, QinQ or trunk support is promised.

The manufacturer's manual distinguishes cyclic I/O PCP 6, alarms PCP 5 and MRP PCP 7. These are **not proof that DCP Identify must be PCP 6**. No verified normative requirement from the accessible sources forces tagging of this Identify requester; do not invent one. Capture engineering-tool requests/device replies to determine local DCP tagging and priority behavior. If a target requires tagged TX, reconsider Phase B scope before adding it. Untagged/VID0/device-specific acceptance is a hardware gate.

## 8. Filter capacity

Installed `components/esp_hal_emac/esp32/include/hal/emac_ll.h:140` defines eight **additional** MAC filters. HAL allocator scans indices 1–8; index 0 is the station MAC. Broadcast acceptance is separate. LLDP and CDP consume two extra entries if successfully installed, leaving at most six before stack memberships. DCP adds zero, so its incremental filter cost is zero.

ESP-NETIF IGMP/MLD callbacks add IPv4 `01:00:5E:*` and IPv6 `33:33:*` addresses through the same finite allocator. IGMP is enabled in lwIP options; IPv6 is enabled, autoconfiguration disabled, MLD report support enabled. Actual memberships are runtime-dependent: do not report all six as guaranteed free. An IPv4 all-systems membership can consume one; IPv6 all-nodes/solicited-node groups consume entries when configured/joined, as do future application groups. Record actual filter/membership failures on hardware. No new all-multicast fallback, IPv6 disable or removal of another owner's entry.

## 9. Burst timing and rate limit

**Reference verified from p-net:** allowed factor 1–6400 (`0x0001`–`0x1900`); responder spreading uses a random-like value, falling back to its last two MAC octets, modulo the factor, multiplied by 10 ms. Factor is not a fixed delay for every device. At proposed factor `0x0080` (128), that implementation spreads replies from 0 through 1270 ms. Zero/reserved factors should never be emitted. Choose an engineering-tool spreading factor greater than 1.

**Design:** collect for 3000 ms after successful TX acceptance, allowing roughly one second processing margin plus rounding beyond 1280 ms. This is a conservative product timeout, not a new normative timing claim. No retries, no periodic background requests, one request per physical Scan action; minimum 10 s between accepted attempts, unaffected by early abort/error. Debounce buttons; a held button cannot schedule multiple jobs. At most one active operation via coordinator. A zero-device completion means no replies observed in this bounded window, not proof of no PROFINET devices.

ResponseDelay reduces but does not eliminate bursts or synchronize poorly behaved devices. PI commissioning guidance specifically supports minimizing multicast activity after startup. DCP remains active network traffic even though it does not change configuration.

## 10. Separate pool and worker

Reject reusing Switch Info's four-slot pool: a DCP response burst would compete with passive LLDP/CDP and hide the attached switch. Use eight independent 1600-byte slots, eight one-byte queue indices, fixed metadata, no per-packet allocation. Frame capacity covers ordinary maximum Ethernet frames including one tag; oversize responses are counted and dropped, not truncated then parsed.

Design one permanent DCP worker, priority 1 (below existing discovery priority 2 and TCP/IP priority 18), 4096-byte stack initially. It waits idle outside scanning, parses at most one slot per iteration and yields after bounded work. Eight slots are an initial bounded compromise, not a lossless guarantee for 32 replies. Separate pools remove shared slot starvation but not shared RX CPU load. Cap accepted candidates per scan (suggest 128) and bound block iterations by data length/frame cap. Report accepted, malformed, wrong/stale XID, oversize, pool_full, queue_full, device_limit and candidate_limit. At overflow sacrifice discovery completeness and display partial results; never wait in RX or allocate overflow slots. Higher capacity requires measured justification.

At window close, stop admission, process only already-queued entries received before the deadline with a bounded drain (e.g. 250 ms), then publish. Deadline, link and token checks still apply. Free every reserved slot even on cancel. No slot index reuse while a worker owns it.

## 11. Response block schema and bounds

Each response block: option u8, suboption u8, BlockLength u16, then BlockInfo u16 and value for the selected IP/Device Identify response blocks. BlockLength includes BlockInfo and value, excludes its four-byte header and excludes alignment pad. Odd BlockLength requires one zero alignment octet; that octet is counted in DataLength. Parse only the declared DCP block region. Ethernet trailing padding is separate and need not all be zero on received traffic.

**Reference-verified field mapping** (lengths below include BlockInfo, exclude header/pad):

| Option/Suboption | Advertised field | BlockLength/value handling |
|---|---|---|
| `01/01` | MACAddress | 8: info 2 + MAC 6. Optional cross-check; Ethernet source remains primary identity. |
| `01/02` | IPParameter | 14: info 2 + IP 4 + subnet 4 + gateway 4, network octets. |
| `02/01` | DeviceVendorValue | 2 + variable VisibleString (ASCII); descriptive device/type text, not guaranteed manufacturer/model. Store sanitized bounded text and truncation flag. |
| `02/02` | NameOfStation | 2 + variable octet string, name syntax/Punycode representation; nominal 1–240 name octets, tolerate empty advertised unassigned name distinctly. Store 240 + terminator, no DNS decompression/lookup. |
| `02/03` | DeviceID | 6: info 2 + VendorID u16 + DeviceID u16. |
| `02/04` | DeviceRole | 4: info 2 + role u8 + reserved u8. Role mask: bit0 IO-device, bit1 IO-controller, bit2 IO-multidevice, bit3 PN-supervisor; retain raw mask/unknown bits. |
| `02/05` | DeviceOptions | 2 + even count of option/suboption byte pairs; validate/count, optionally record known capability bits without commands. No unbounded pair list. |
| `02/06` | AliasName | 2 + variable octets; port-name + dot + station-name form. Optional, may be absent from Identify-All. Bounded 63-character display, flag truncation. |
| `02/07` | DeviceInstance | 4: info 2 + instance high/low bytes, preserve as u16. Not a serial number. |
| `02/08` | OEMDeviceID | Separate OEM vendor/device pair if later useful; initial parser can safely skip. Do not replace advertised DeviceID with it. |

IP BlockInfo conveys advertised state: 0 not set, 1 set, 2 DHCP; conflict variants `0x80`/`0x81`/`0x82`. Preserve raw info and expose a conflict flag only for these known encodings. Presence of the block does not establish routability or that zero IP is usable. General device-block BlockInfo is reserved in the dissector; do not invent persistence/error semantics or interpret it as a Set BlockQualifier. Nonzero reserved values may be counted and retained diagnostically; they are not permission to execute anything.

Bounds policy: reject short fixed headers, DataLength beyond available bytes, overflow, missing alignment pad/nonzero block alignment pad, BlockLength less than 2 for recognized info-bearing fields, and wrong fixed field length. Unknown blocks require only generic structural bounds and are skipped without assuming they contain BlockInfo. Known text lengths must be bounded before copy, never `strlen()` on wire data. Replace nonprintable/high bytes with `?` for display and flag sanitization; do not claim Unicode decoding. A malformed frame produces no partial publication: parse to worker scratch, then merge atomically only after the entire declared region is structurally valid. Invalid known optional field lengths fail the frame. Unknown extensions remain skippable.

No order number, firmware version, serial number, application health or complete supported-service inventory is promised by this Identify subset. Do not fabricate missing fields from OUI, PTR or LLDP.

## 12. Device model, duplication and Vendor ID

Separate from `discovery_device_t` and its 254 IPv4 records. Recommended illustrative fixed representation:

```c
typedef struct {
    uint32_t flags;                 /* valid/presence/truncation/sanitization */
    uint32_t last_seen_ms;
    uint8_t ip[4], subnet[4], gateway[4];
    uint16_t vendor_id, device_id, device_instance, ip_block_info;
    uint8_t mac[6], device_role, reserved;
    char station_name[241];
    char vendor_value[64];
    char alias_name[64];
} profinet_device_t;
```

Expected conventional 32-bit alignment size: 408 bytes; confirm with `sizeof`/static assertion in Phase B. Flags independently distinguish missing vs empty/zero for each optional field. Keep scan token, XID, physical epoch and revision at session/table level, not duplicated on every record. Snapshot carries them. Receive timestamp is monotonic scan uptime, not wall clock or device time.

Limit **32 unique MACs**. Merge only identical six-byte source MAC within the same validated transaction/session. Never merge by name or IP. XID associates a response with our scan; it is not cryptographic authentication, and a spoofed MAC/XID can still lie. Fully parsed later responses fill absent fields and replace present fields with the later advertised values; absence never erases an earlier field. Within one response use first occurrence of a known field and count conflicts, so parsing order is deterministic. Sort completed records lexicographically by MAC, with no slot-table exposure to UI. At capacity retain existing records/updates, reject new MACs, increment limit drops and mark partial. No eviction churn.

PROFINET VendorID is PI's company key and DeviceID a manufacturer-managed key; **neither is a MAC OUI**. The current OUI table is never used for this mapping. Numeric hex IDs are sufficient. Later a small independently sourced table from PI's linked Manufacturer-ID XML is practical with dated provenance/licensing review and unknown fallback; no guessed labels or runtime download. Keep VendorValue text explicitly labeled as advertised text. [PI identification authority](https://www.profibus.com/products/ident-numbers).

## 13. XID, lifecycle and DHCP independence

Design seed a private 32-bit counter from `esp_random()` at boot, then increment for each accepted transaction, skipping zero. This avoids repeats during a boot and reduces across-boot collisions; do not call it a security nonce or assume full entropy when RF entropy sources are inactive. On wrap reseed while no transaction/job remains pending, keeping session token/epochs as independent guards. No fixed XID across scans.

Acquire existing diagnostic coordinator; snapshot network context; clear result table and establish session; post one TX callback; arm collection for this XID; accept responses; close/drain; publish sorted bounded snapshot; release coordinator once. Retain/reap permanent pending callback storage after bounded timeout without new TX/job reuse. RX and publication need both XID **and** captured token/epoch, not just a globally current XID. Flush/release queued slots on cancel. Recheck before and after parsing. Old queued frames cannot populate a new scan even if the source MAC matches.

Important current lifecycle finding: `network_ip_event(false, ...)` increments `link_generation`, and IP acquisition/change increments `revision`. Thus the existing name "link generation" is not purely a physical link epoch. **Do not weaken or change this behavior for Discover/AutoTest/PTR.** Recommended Phase B add a separate physical Ethernet epoch to network status, incrementing on connected/disconnected/stop events only; retain all old fields/semantics. DCP requires `link_up` and a valid initialized driver, **not** `ipv4_ready`, subnet permission, gateway or DNS. It must not call the ARP Discover start/subnet rules. UI accepts link-up/no-IP, and result getters also use the L2 epoch rather than requiring DHCP.

Conservative first behavior additionally snapshots current network `revision` and cancels on any revision change during the short scan, including DHCP acquisition/loss. This satisfies rejection of a new network context without making IP availability a start prerequisite. If DHCP churn occurs, show "Network changed; scan again"; no automatic resend. A fresh scan on stable link/no IPv4 must work. Results are bound to physical epoch; decide explicitly whether an IP-only change invalidates completed results (recommend yes initially, with clear stale status). A future relaxed IP-change policy can use the new physical epoch but requires separate tests; it must never alter existing IPv4 guards.

Cancellation: physical link loss, stop, revision/epoch change, explicit scanner cancel, token invalidation, OTA_RECEIVING/OTA_SUCCESS or scanner shutdown. Check OTA at start, TX callback and worker polling (e.g. <=50 ms). UI Back currently leaves ordinary Discover running; define DCP Back during scan as explicit cancel and release after bounded cleanup, rather than silently assuming a general UI cancellation API exists. An already queued DMA frame is irreversible; never retry it. Link event ordering in app_main publishes lifecycle before netif handling and should be preserved.

## 14. OLED and web architecture

Industrial is currently a main-menu placeholder. Add only an Industrial submenu containing PROFINET and Back, then PROFINET Scan/Results/Back consistent with current queued-button/UI-task ownership. Label ready/running screens "Identify only". Show link-down/busy/cooldown/TX error separately from successful zero results. Missing IPv4 on Syndetis is not a scan error.

Use existing eight rows/21-character display bounds. Device summary: index/count, station-name clipped to display width, advertised IP or "IP not set/unknown", Vendor hex, Device hex, role and navigation. Separate network page shows full 17-character MAC, IP, mask and gateway; extra identity page can show instance/text and truncation indicator. Long station text must not overflow rows or be used as format string. Copy one 408-byte record to persistent UI-owned scratch, not a whole table onto its 4 KiB stack. Names are station names, not PTR hostnames. Keep Switch Info and ordinary Discover navigation unchanged.

Future `GET /api/profinet` only: no remote start/write/query operation. Include state, count, scan generation, duration, packets_sent (0/1), complete/partial and drop counters; devices carry MAC and only present advertised fields (or explicit nulls), station/vendor/alias truncation flags, numeric IDs/role/instance and raw IP status. Bounded 32-record HTTP-owned static snapshot like existing Discover; release lock before formatting/network sends. JSON-escape every device string, use browser `textContent`, never HTML interpolation. Stream bounded per-device chunks, check every snprintf bound. Budget a ~3 KiB serializer chunk including worst-case escapes of 240-byte name and two 63-byte strings, not a giant JSON stack buffer. HTTP stack is currently 12288 bytes: measure its watermark with maximal escaped text before deciding it needs an increase. `Cache-Control: no-store`; result retrieval never emits DCP.

HTTP still requires a usable management IP to reach the page; **DCP scanning itself remains usable through OLED without one**. Do not claim DHCP-independent HTTP access or add an implicit fallback IP/web service.

## 15. Memory budget

Design estimate, no firmware build/ABI measurement performed:

| Storage | Proposed bound | Bytes |
|---|---:|---:|
| Device table | 32 x 408 | 13,056 |
| HTTP snapshot | 32 x 408 | 13,056 |
| Independent capture pool | 8 x (1600 + ~32 metadata) | ~13,056 |
| Worker parser record | one | 408 |
| UI record | one | 408 |
| TX frame + job/status/locks | bounded permanent | ~320–640 |
| Queue indices | 8 x 1 | 8, plus RTOS control structure |
| Worker stack | one permanent task | 4,096 |
| Worker TCB, queue/semaphore overhead | allocation/ABI dependent | reserve ~0.5–1 KiB |

Static data approximately **39.4–39.7 KiB**, plus ~4.6–5.1 KiB permanent RTOS heap if dynamically creating worker/queue/semaphore: **budget 44–46 KiB total incremental internal RAM**, with ~1 KiB contingency. If using static RTOS allocation that storage moves to static RAM, not zero cost. Callback posting temporarily allocates one small lwIP message; no sockets, DNS jobs or per-device heap. RX driver's ordinary frame allocation still exists transiently, as before. Serializer scratch is on existing HTTP stack; adding it is a stack-margin cost rather than another task allocation.

Current ordinary Discover already reserves 65,536 bytes for records/probes/snapshot. User-reported recovered heap was roughly 144.9 KiB, minimum ~129.6 KiB, and discovery worker free stack ~2252 bytes. Subtracting the DCP budget suggests approximately 99–101 KiB recovered heap, but **this is an estimate**, not a new measured baseline; allocator layout, transient bursts and HTTP matter. Prefer 32 devices to another 254-device table. If actual margin is poor, reduce text/snapshot duplication or device bound explicitly rather than removing old safeguards. Measure DCP, Switch Info, HTTP, RX/TCPIP and UI worker watermarks; don't assume discovery's watermark applies to the new parser.

## 16. Existing feature risk assessment

| Feature/resource | Main risk and required protection |
|---|---|
| DHCP/ARP/IP forwarding | RX misclassification or TX contention. Narrow signature, unchanged forwarding/ownership, serialized one-frame TX, verify DHCP renewal during scan. |
| ARP Discover/ICMP/PTR/AutoTest | Coordinator conflict and weakened IPv4 rules. Separate records; unchanged old lifecycle fields; busy handling; no new socket pressure; retain ping degradation/quarantine. |
| LLDP/CDP | Shared callback replacement, burst CPU/slot starvation. One router, separate pool, existing filters/parsers/TTL/LLDP preference preserved; optional DCP failure leaves Switch Info. |
| HTTP | Large string escapes/snapshot and concurrent scans. Static bounded snapshot, short lock, checked chunks, read-only route; test max records/text and client disconnect. |
| OLED | Long station names/menu states/stack. Existing row renderer bounds; persistent one-record scratch; test no-IP path and Back cancellation. |
| OTA | Pending TX or parser after OTA starts. Check phase throughout; invalidate session/retire jobs; test upload during scan; preserve physical OTA authorization. |
| Multicast | Finite common filter table. DCP adds zero; no mode broadening/removal of others. |
| Ethernet RX | Flood copying/parsing overhead. Nonblocking pool admission, finite frame/candidate cap and yields; drop discovery rather than stall ordinary networking. |
| Heap/stacks | ~44–46 KiB added budget, transient frame/message allocation. Repeated recovery, low-watermark and maximal HTTP fixture measurements. |
| Raw TX | Current DMA not concurrent-call safe. Core-task callback only, audited callers/config guards; no unprotected worker TX. |

No feature is silently disabled to make DCP fit. No partition, PHY/pin, ping patch or DMA patch change is proposed for DCP.

## 17. Future host verification gates

Compile the production portable builder/parser/classifier/model in host tests, not separately reimplemented algorithms. Fixtures should assert meaningful 30 request bytes plus zero padding to 60, destination/type/FrameID/service/type/XID/factor/DataLength/All-All. Verify only the whitelisted builder can reach TX. Capture-generated fixtures must exclude FCS or clearly mark it.

Parser/classifier suite: valid empty/minimal response, multiple blocks in arbitrary order, station empty/240-byte/overlimit, IP and status/conflict, IDs, combined role/reserved byte, VisibleString sanitation/truncation, instance, alias, even option pairs, unknown/OEM/control-advertisement skip, duplicate known fields, odd block with correct/missing/nonzero pad, outer Ethernet padding, untagged/VID0/nonzero-VID, second-tag/truncated-tag forwarding, oversize frame, short Ethernet/FrameID/header, short/overlarge DataLength, block header/value truncation, BlockLength underflow/fixed-length mismatch, and byte-order errors. Fuzz bounded parser with sanitizers and verify no record publication on malformed frames.

Reject wrong destination/source/XID/FrameID/service/type, unsupported type as record, Set/Get/Hello, unrelated cyclic RT/alarm/PTCP and MRP. Mock RX ownership to prove exactly one free for consumed responses and unchanged forwarding for others. Duplicate same MAC merges; different MAC/same IP/name stays distinct; stable MAC sort; table bound 32; overflow stats/partial state; separate pool overflow cannot consume LLDP slots.

Lifecycle/TX tests use controlled callback and clock fakes: callback queue full, descriptor unavailable, link-down, pending callback timeout/quarantine/reap, deadline expiry before queued callback runs (no TX), stale slot token/XID/epoch, cancellation during parse, link reconnect, stop, IP revision change, no-IPv4 stable scan, OTA start, repeated physical actions/cooldown, end-window bounded drain and coordinator release exactly once. Assert at most one TX attempt and no retries under every failure. Preserve and run all existing LLDP/CDP/Discover/name/ICMP/link-loss/OLED/API/AutoTest/release tests in Phase B; new source-level tests alone are not proof of parser behavior.

## 18. Hardware acceptance plan

Start on an authorized isolated lab segment with a device whose exact model/firmware is documented as PROFINET capable: Siemens PLC/ET200, PN remote I/O/drive/HMI, or a confirmed PN-capable Phoenix Contact device. A brand or an Ethernet port alone is insufficient evidence. Record device model/firmware and capture setup.

Compare with TIA Accessible Devices/Online Access or the vendor tool and a mirror/TAP/Wireshark capture. Filter `pn_dcp` and inspect `pn_dcp.service_id == 5` / service type, FrameID and XID. Verify one correct multicast request, actual source MAC, no Set/Get/Hello/control TX, factor/padding, unicast matching-XID responses and collection timing. Compare only fields advertised in those replies: MAC, station, IP/mask/gateway, IDs, role/instance/text. Do not use a tool's extra RPC-derived model/firmware fields as DCP expectations.

Required scenarios: normal DHCP; link up with no DHCP/local IP; responder unconfigured/zero-IP; ordinary managed switch; VLAN/priority-tag response where available; several devices/burst and >32 via replay; overflow/foreign PN traffic; repeated scans after cooldown; link loss/reconnect and Ethernet stop; DHCP revision transition; OTA receive; HTTP polling/client disconnect; LLDP/CDP continuing; Discover/AutoTest before/after. Capture drop counters, normal packet behavior, duration, recovered/min heap and all worker watermarks across at least ten scans. Use replay/injection on the isolated lab for malformed/high-load tests, not a live production OT segment.

Accept only with no configuration-changing packet, no stale publication/leak, bounded duration, stable recovered heap, adequate measured stack/heap margins and no regression to existing network behavior. Empty discovery remains inconclusive when switch DCP boundaries/filters, VLAN scope, delay, unsupported devices or dropped replies explain it. Route through local L2 only; no subnet-wide/routed guarantee.

## 19. Proposed Phase B files and changes

Exact proposed names (not created in this phase):

- Add `main/profinet_dcp.h`/`.c`: portable fixed Identify builder, narrow classifier, bounded response parser and protocol constants.
- Add `main/profinet.h`/`.c`: fixed device model/session, TX callback, separate capture pool/queue/worker, getters/start/cancel/counters/coordinator.
- Add `main/ui_profinet.h`/`.c`: bounded device page formatting.
- Add `main/ethernet_rx.h`/`.c`: neutral shared receive router, retaining LLDP/CDP and forwarding. Refactor `switch_info.c`/`.h` to expose bounded capture entry/setup without replacing the callback itself; optional setup failures remain isolated.
- Modify `main/app_main.c`: initialize shared router and optional scanner before Ethernet start, retaining event-handler ordering and nonfatal optional failures.
- Modify `main/network_status.h`/`.c` (and lifecycle helper/tests if the helper stores the new field): add independent physical epoch without changing old revision/generation behavior.
- Modify `main/ui_menu.c`, `main/web_server.c`, `main/web_index.html`, `main/CMakeLists.txt`: submenu, read-only endpoint/escaped display and source registration. Update README/phase report for scope and field limitations.
- Add `tests/profinet_dcp_test.c`, `tests/profinet_session_test.cpp`, `tests/verify_profinet.py` plus captured byte fixtures/provenance and existing web/UI test extensions. Final test naming can follow repository conventions without changing the architecture.

No necessary changes to root DMA/ping replacement CMake, `patches/*`, partition layout or existing OUI/PTR/ICMP logic. No required sdkconfig changes with serialized TX. A version bump to 0.7.0-dev belongs only to an approved implementation/release phase. New worker priority/stack/pool/table limits should be local compile-time constants, not an unbounded configuration surface.

## 20. Approval boundary and remaining uncertainty

Phase A establishes a credible path, **not a finished feature**. Phase B GO is conditioned on implementing the narrow builder/parser, proving current core-task TX serialization remains true, retaining job storage through delayed callbacks, preserving shared RX ownership, and passing host plus hardware gates. Direct unprotected raw TX, a generic DCP command API, all-8892 capture, silent IP guard changes, or a feature claimed complete without a real PROFINET comparison are NO-GO.

Still hardware pending: real device acceptance of the chosen untagged request/delay, exact captured response tagging, actual filter occupancy, loss rates under bursts, CPU/RX impact, heap and task-stack margins, and runtime stop/OTA races. Still normative-reference pending: complete applicable-edition PCP/conformance requirements; this does not prevent an explicitly scoped lab Identify prototype but prevents a certification claim.

**Work stops here for approval. No Phase B implementation, firmware build, flash or OTA was performed.**
