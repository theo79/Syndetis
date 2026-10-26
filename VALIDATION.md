# Syndetis5C checkpoint validation

Packaging date: 2026-10-26. The owner reports Milestones 5A and 5B hardware validation passed. This session performs packaging/build/software checks only; no device operation. Historical NOT RUN notes below describe their original sessions and do not negate later user reports. Private field addresses and original machine paths have been omitted from this portable copy; original evidence is unchanged outside it. Current packaging results are in docs/VERIFICATION.md.

# Syndetis validation

## Milestone 5B â€” four-request ICMP Ping â€” 2026-10-26

**Software validation PASS. Ping hardware acceptance NOT RUN.** The owner now
reports Milestone 5A Manual IPv4 hardware validation passed; older 5A NOT RUN
statements below record the earlier software implementation stage. They do not
negate that later supplied acceptance. The October 26 DHCP/CDP/LLDP field results
remain preserved. No new hardware observation is claimed in this session.

Preflight inspected actual `master` status, HEAD
`8117bcbc8f41ae9a2197daa16c71dd1535cb7616`, source version 0.4.0, ESP-IDF v6.1,
single 1 MiB factory configuration, current source/tests and recovered Aâ€“G
`1eb79ab4fce5c870f9854d0a32f752c8af838130`. All existing uncommitted work was
retained. Before source edits, a source/configuration snapshot, binary tracked
diff, status, saved 5A app and checksum inventory were created in ignored
`tmp/pre-m5b-20261026/`. No commit, push, branch, GitHub, flash, erase, partition
or recovery-archive modification occurred. The descriptor remains 0.4.0; identify
the unflashed 5B candidate by its path/hash rather than version alone.

### Implementation and ownership

Network Tests now contains DHCP Test, Manual IP, PING and Back; the primary menu
is unchanged. PING offers gateway/custom target and Back. Without an active wired
address, SET MANUAL IP / DHCP TEST / BACK are offered. Losing the address while
the Ping menu is visible refreshes those safe setup choices. Custom target uses
the same saturating decimal-octet adjustment, selected-octet renderer and editor-
only acceleration as Manual IP; all four octets lead to an explicit START screen.
Gateway selection starts one session. Results offer AGAIN/BACK, or CANCEL while
running. Hold Back cancels and leaves the results page; it never changes addressing.

The target selector reuses the historical host-order arithmetic approach, adding
own-IP and management-subnet rejection and routed-gateway validation. Invalid,
loopback/multicast/reserved targets and local network/broadcast addresses are
rejected before any socket or diagnostic packet. Off-subnet targets require the
configured wired gateway; no discovery-based route or DNS lookup is introduced.

The existing DHCP/Manual worker remains the authoritative owner. It now also
executes `network_ping_step()` after its normal ownership step, with no additional
task or SDK Ping callback task. The UI only submits/cancels a request and reads
snapshots. A short diagnostic reservation rejects incompatible DHCP Start/Stop,
Manual APPLY and USE DHCP until socket retirement. DHCP renewal, passive LLDP/CDP,
manual conflict monitoring and scheduled announcements continue normally.

Each session opens one native raw ICMP socket, requires SO_BINDTODEVICE using the
actual Ethernet netif name, and binds the actual wired source IPv4. Either binding
failure closes the socket; there is no default/AP fallback. Inspected pinned SDK
`sockets.c` SO_BINDTODEVICE, `raw.c` raw_bind_netif/raw_sendto and input filtering:
bound raw PCBs use only that netif index and return a route error if it disappears.
The old Aâ€“G netif glue, OTA/persistence architecture and asynchronous SDK Ping
task/session lifecycle were not imported.

Normal completion makes four 24-byte Echo sends (8-byte ICMP header plus 16-byte
payload), paced from actual send-call starts about 1 second apart. Each request
has a 1-second reply budget. Receive/send calls use MSG_DONTWAIT; at most eight
datagrams are drained per 50 ms owner iteration. Identifier, sequence and a fresh
64-bit session cookie distinguish replies. Duplicate/old/malformed packets cannot
count. RX timestamps are recorded by the existing dispatcher path; successful RTT
is measured from send-call start to matching Ethernet RX, not delayed worker reads.
Only successful samples contribute to average RTT; loss uses accepted send count.
NO REPLY never means DEVICE DOWN and does not assign a cause.

The owner admits only a current matching Echo Reply in addition to its original
ARP/DHCP policy. It checks Ethernet/IPv4/ICMP bounds, IP and ICMP checksums, endpoint
addresses, type/code, fragments, payload cookie, ID and sequence. Manual mode still
rejects DHCP takeover. TCP, unsolicited Echo requests, arbitrary ICMP, tagged IP
and AP-directed Ethernet browser traffic remain blocked. Each original RX buffer
still passes discovery first and is consumed/freed exactly once, including errors.

The same gate validates outgoing Echo requests. Cancel closes it immediately;
late native ARP-queued requests from that or another session cannot transmit.
Socket retirement occurs on the next worker step. Physical link loss, address
conflict and owner generation changes immediately invalidate the reservation's
data path/results; live IP/mask/gateway/interface are rechecked on every Ping step.
Native socket reception must also provide the validated reply; an RX timestamp
alone cannot fabricate a reply after stack-input failure.

Success, timeout, cancellation and all setup/send/receive failures close the one
descriptor and release the reservation. Unexpected close failure retains that
descriptor/reservation and shows cleanup retry until closed; EBADF means the
descriptor is already absent. There are no callback objects to outlive a session.
Completed results become invalid if the address generation subsequently changes.
No Monitor, OTA, browser Ping endpoint or additional discovery was implemented.

### Automated regressions â€” PASS

The complete `tests/run-regressions.ps1` run passed all eight C suites plus Node
JSON/dashboard and installed Chrome phone/laptop checks. Targeted final Ping/UI
reruns passed after the immediate-cancel and no-IP-menu refresh refinements.
Only platform/socket I/O is stubbed; production owner, dispatcher, codec, Ping
state machine and OLED renderer/navigation are exercised.

- DHCP and Manual local Ping, gateway selection and routed targets; missing or
  invalid gateway, invalid/self/management/network/broadcast target rejection.
- Four complete replies, partial replies, zero replies/NO REPLY, exactly four
  normal send calls and 1-second spacing/timeouts; RX-based 3 ms RTT despite a
  50 ms processing delay, duplicate rejection and last-millisecond reply handling.
- Cancellation before further transmission/while waiting, immediate queued-TX
  suppression, old-cookie ingress and old-session queued egress rejection.
- Changed native IP before owner publication, link-generation loss/reconnect,
  manual ARP conflict mid-test and completed-result invalidation after mode change.
- Mandatory device/source binding, no AP target/fallback, blocked simultaneous
  DHCP/manual changes and normal addressing retained after Ping socket closure.
- Malformed/truncated/checksum/type/endpoint/ID/sequence/cookie packets, bounded
  receive-flood work, and native receive-input failure without fabricated replies.
- Socket creation, interface/source binding, send/receive/name lookup errors,
  delayed close retry/already-closed descriptor and 32 repeated session retirements.
  Created/closed descriptor counts match; no additional netif/task is created.
- Actual 5A DHCP/Manual ownership and conflict/rate-limit/race suites, including
  32 full mode-transition cycles and existing DHCP timeout/cancel/DNS cleanup.
- Existing synthetic LLDP/CDP parser/table/dispatch, TTL/cache/link-generation,
  button, OLED bounds, HTTP/socket guard, worst-case JSON and browser regressions.
  Chrome at 375Ã—812 and 1280Ã—900 retains hotspot dashboard operation, text safety,
  stale/recovery and no external requests. No new browser controls were added.

Existing TinyCC 0.9.27, Node 24.22.1 and installed Playwright/Chrome were used.
The Windows test stub needed POSIX socket errno definitions absent from TinyCC's
headers; production uses lwIP/newlib values. Tools ran outside the restricted
Windows process environment due to its known DLL relocation failures. Logs:
`tmp/m5b-host-tests.log`, `tmp/m5b-ping-races.log`; outputs `tmp/m5b-tests/`.

### Build, resource impact and preservation â€” PASS

Fresh `build-m5b-20261026/` build passed under ESP-IDF v6.1 / lock 6.1.0, GNU
15.2.0, Python 3.11.15, esptool 5.3.1 and LAN87xx 1.1.0, using a separate
`tmp/m5b-sdkconfig` copy. Final incremental builds passed for immediate cancellation
and the no-IP display refinement. A direct Ninja attempt without SDK activation
failed to launch correctly; rerunning in the initialized SDK environment passed.
Five existing SDK private-include CMake warnings and the existing venv-path warning
remain. No application compiler warnings/errors. Logs: `tmp/m5b-clean-build.log`,
`tmp/m5b-final-build.log`, `tmp/m5b-final-ui-build.log`.

| Resource | Final 5B candidate |
| --- | --- |
| Application | **905,792 bytes / 0xDD240** |
| Factory partition | **1,048,576 bytes / 0x100000**, unchanged |
| Remaining | **142,784 bytes / 0x22DC0 (13.62%)** |
| Increase over 898,480-byte 5A | **7,312 bytes** |
| Static DRAM data | **18,247 bytes**, +16 bytes |
| Static DRAM BSS | **59,848 bytes**, +184 bytes |
| Total static DRAM increase | **200 bytes** |
| IRAM text | **89,311 bytes**, unchanged |
| Task stacks | Existing owner 4,096 bytes; main/UI 3,584 bytes; no extra task |
| Active diagnostic resources | At most one native raw socket/PCB/mailbox |

The receive buffer is a bounded 128-byte worker local; other packet/binding locals
are fixed size. Socket/lwIP heap demand and actual worker/UI stack high-water marks
are hardware measurements and **NOT RUN**. Native bounded ARP queues may temporarily
retain stack-owned packets until resolution/expiry; the generation/cookie TX gate
prevents stale Echo emission without flushing the working interface's ARP cache.

Final candidate: `build-m5b-20261026/Syndetis.bin`, descriptor 0.4.0.
SHA256: `c87668b12cee4a797818d77bac6138b82ed06fcd590d5b0e14f2ac86f50dd59b`.
The factory table remains byte-identical to the saved baseline, SHA256
`7f00b6c042a89b15b0cac534f82ed988caf29278ff5700b0c511eb1b5bb7c820`.
The 5A app remains 898,480 bytes with SHA256
`9cf05f74a74a56ad968039fad33569abca850224d790a48a1096d65c033131b2`.
The external original v0.4.0/Aâ€“G preservation package passed all **4,280 checksums**;
neither it nor the original saved flashed firmware was overwritten.

### Changed files

New application files: `main/network_ping.c`, `main/network_ping.h`,
`main/ping_packet.c`, `main/ping_packet.h`, `main/ping_target.h`.
Modified: `main/dhcp_test.c`, `main/dhcp_test.h` (reservation, binding, packet gates,
RX timestamp and worker integration), `main/ui.c` (Ping flow/shared editor),
`main/CMakeLists.txt`; `main/network_policy.h` changes only its classifier comment.
The Manual IPv4 codec/validator, button logic, driver/discovery, hotspot/HTTP source,
SDK configuration, dependency pins and partitions were not changed in 5B.
New tests: `tests/ping_test.c`, `tests/ping_stubs/lwip/sockets.h`.
Existing DHCP/Manual/UI fixtures, netif stub and regression runner were extended;
README and this log were updated incrementally.

### Hardware Ping validation procedure â€” NOT RUN

1. After separately authorized deployment, verify the candidate hash/path; version
   0.4.0 alone cannot distinguish the saved baseline. Keep both recovery packages.
2. With DHCP, Ping a known local responder and the actual gateway. Capture both
   directions: wired source, four Echo requests/unique sequences, roughly 1-second
   spacing, matching replies, correct successful-only RTT and loss. Repeat with
   cold/warm ARP. Accepted socket sends do not prove physical wire transmission;
   ARP resolution/queue timing can affect the first request.
3. With temporary Manual IP and no gateway, Ping a known same-subnet responder.
   Reject own/network/broadcast/invalid targets and off-subnet targets without
   a gateway. Configure the correct gateway and verify a routed responder.
4. Verify 4/4, deliberate partial loss and zero replies. NO REPLY must not imply
   DEVICE DOWN. Check AGAIN creates a fresh session and clears old statistics.
5. Cancel during receive wait and unresolved ARP; capture no later new requests
   from the canceled session. Frames already handed to the MAC cannot be recalled.
6. Unplug/reconnect on another port and inject a manual address conflict mid-test:
   abort, invalidate results, maintain 5A relinquishment and require explicit setup
   and a new Ping. Confirm incompatible configuration actions are rejected until
   socket cleanup, without interrupting normal DHCP renewal/ARP monitoring.
7. Keep the hotspot connected throughout local/routed tests. Capture on both
   interfaces to prove ICMP uses Ethernet only. Confirm AP IP/DHCP/DNS and Web
   Access rules remain intact; Ethernet HTTP/AP-targeted TCP stays blocked.
8. Keep known Cisco CDP and Siemens LLDP traffic running throughout acquisition,
   Ping/cancel, manual conflict and mode transitions; check fields, TTL/cache and
   link invalidation. Repeat at least 20 cycles while logging heap, socket health,
   worker/UI stack high-water marks, responsiveness and cleanup errors.

Known limits: IPv4 literals only, four requests only, no hostnames, ICMP error
diagnosis, Continuous Monitor or OTA. Fragmented/tagged/unsolicited ICMP and
management-subnet targets are deliberately unsupported. Native ARP, scheduling,
filtering and physical packet delivery remain risks for hardware review; software
tests cannot establish wire isolation or field timing. The next step is this
separate hardware acceptance, not additional features.

## Milestone 5A â€” temporary Manual IPv4 â€” 2026-10-26

**Software implementation and automated checks PASS. Hardware validation NOT RUN.**
This extends the preserved restart-lineage 0.4.0 workspace on `master`, HEAD
`8117bcbc8f41ae9a2197daa16c71dd1535cb7616`. No flash, erase, commit, push, branch,
GitHub, partition-layout or recovery-package change was made. The descriptor
remains 0.4.0; identify this unflashed candidate by its binary hash, not version alone.

The owner's October 26 DHCP acquisition (4.4 seconds), release/retry and Cisco CDP /
Siemens LLDP results below remain the field evidence for the prior baseline. They
are not Manual IPv4 acceptance. The old Milestone 5 preflight is a historical
snapshot: the two lineages were reconciled and preserved before this implementation.
Original Aâ€“G source survives under `refs/preservation/syndetis-ag/`; Continuous
Monitor remains unlocated. Ping, Monitor, OTA and new discovery are outside 5A.

### Address owner and UI

The existing `dhcp_test.c` worker/netif owns OFF, DHCP and temporary Manual state.
No second task, netif, persistence or default Ethernet/IP event handler was added.
Explicit APPLY copies a validated draft and invalidates the previous result;
the worker releases DHCP/removes old addressing before probing. Manual activation
never starts DHCP. A generation token invalidates stale review, acquisition and
publication, including lease/mask/gateway changes and physical link changes.
Cancel before APPLY changes only the draft; cancel after accepted APPLY leaves
NO IP and never silently restores an old lease. Active Manual survives navigation.
USE DHCP releases it and opens the on-demand DHCP menu without starting acquisition.

Network Tests adds only Manual IP. YOUR IP has four visibly selected decimal
octets, editable /1â€“/30 prefix and optional gateway (default NONE). REVIEW has
APPLY / EDIT / BACK and explicitly asks to replace active/requesting DHCP. A DHCP
change after review rejects that APPLY and requires a new confirmation. Editing
retains saturating octets, 450 ms initial repeat, 180 ms repeat interval and
50 ms interval after 1,800 ms. Repeat events are enabled only in editor pages;
30 ms debounce, boot-release gating, short-on-release and 500 ms hold Back remain.

The pure validator rejects reserved/non-unicast hosts, network/broadcast hosts,
invalid prefixes, off-subnet/self/network/broadcast gateways and all CIDR overlap
with the management AP. No address settings come from discovery/traffic.
OLED and hotspot API/dashboard show NO IP / DHCP / MANUAL IP, actual address,
manual prefix and optional gateway. Manual mode never populates successful DHCP
lease fields. DHCP HTTP actions are rejected/disabled while Manual owns the link;
manual configuration and USE DHCP remain physical actions.

### Conflict protection and pinned SDK review

Reviewed RFC 5227 Sections 2.1, 2.3, 2.4(a) and 2.5:
<https://www.rfc-editor.org/rfc/rfc5227.html>.
The manual attempt waits a randomized 0â€“1 seconds, sends three zero-sender ARP
requests with randomized 1â€“2 second spacing, then waits two seconds after the
third probe. It assigns the address and sends two request-form announcements,
two seconds apart. One `esp_random()` sample per delay maps to millisecond buckets
without a retry/busy loop. Timing is driven by the existing 50 ms worker; actual
wire/scheduler latency is not a hard real-time guarantee.

The existing dispatcher still borrows each frame for passive discovery before
the IP owner consumes or frees it once. Its bounded ARP observer validates Ethernet/
IPv4 ARP lengths/types/opcode, rejects reflected own-MAC packets and detects both
existing sender claims and simultaneous probes during probing/final wait. No heap
allocation, cache, general ARP discovery or driver/filter change was introduced.
Active Manual admits ARP only to lwIP; late DHCP and Ethernet TCP are rejected.
Normal non-conflicting ARP requests, including probes, are passed to lwIP for replies.

Any later conflicting sender claim immediately closes RX/TX, invalidates the active
snapshot/generation, and displays ADDRESS IN USE. Stack/address removal follows on
the worker. This chooses RFC 5227 immediate relinquishment, with no defensive ARP.
The saturated conflict count persists across editing, mode changes and reconnection
until reboot. After ten conflicts, an explicit attempt is allowed at most once per
60 seconds measured from the previous accepted attempt; there is no automatic retry.

Inspected installed SDK `esp_netif_lwip.c`, `esp_netif_handlers.c`, lwIP `netif.c`
and `etharp.c`. DHCP remains STOPPED during manual configuration. Initial lwIP
link-up GARP occurs independently of the ESP-NETIF periodic GARP flag, so stack TX
and RX remain gated through assignment/startup until our first timed announcement.
No SDK patch is used. The existing stop/RX-drain and DNS borrow/restore design is
preserved. Tests now model that `set_ip_info`, including zero-address cleanup,
clears global DNS; cleanup therefore saves resolver slots even when cancellation
precedes DHCP Start. No AP pointer or AP IP/DHCP-server DNS API is used.

No probes receiving replies is not a guarantee of an unused address. Silent,
filtered, delayed and malicious/spoofed ARP remain limits. Frames already handed
to the MAC before a cancellation/conflict cannot be recalled. Tagged IPv4/ARP is
not a new supported mode; physical VLAN/network configuration remains unchanged.

### Executed regressions â€” PASS

- Actual C LLDP/CDP parser/table lifecycle, long fields, bounds, expiry/withdrawal,
  cache/session behavior and buttons, including editor-only repeat thresholds.
- Actual OLED UI/navigation with I/O stubs: octet saturation, prefix bounds,
  optional gateway, cancel/no-APPLY, explicit APPLY, changed-DHCP review rejection,
  probing cancel, active-address navigation, USE DHCP without automatic Start,
  and bounds for every new page plus all existing full-length neighbor pages.
- Actual existing DHCP worker/dispatcher tests, timeout/retry, primary/cleanup
  failures, DNS restoration, AP sentinels and 32 timeout/cancel cycles.
- Actual shared DHCP/Manual owner: /1â€“/30 validation, gateways/AP overlap, three
  timed probes/two announcements and frame fields, reflected/malformed packets,
  competing probes, late conflict relinquishment and persistent rate limiting.
- Actual owner races: cancellation before/during probes; link loss in wait/probe/
  active states; link or conflict during SDK setter/start/announcement calls;
  an old DHCP poll finishing after APPLY; mask-change generation invalidation;
  stale review tokens, blocked late DHCP ingress, ARP TX errors and cleanup retry.
- Thirty-two repeated full DHCP/Manual transition cycles, checking passive state,
  DNS restoration, unchanged AP sentinels, closed gates and discovery delivery.
  This is deterministic I/O modeling; it is not a physical heap endurance result.
- Actual HTTP actions/socket guards, serializer worst-case four-neighbor bound,
  escaping/null/empty behavior, Manual fields and absent DHCP lease/DNS claims.
- Node dashboard tests and installed Chrome at 375Ã—812 and 1280Ã—900: serialized
  polling, stale/recovery, long/XSS text safety, DHCP actions, manual mode/address/
  prefix/NONE gateway, disabled DHCP controls, no overflow or external requests.

Runner: `tests/run-regressions.ps1`, existing TinyCC 0.9.27, Node 24.22.1 and
installed Playwright/Chrome. Browser execution requires its installed module path
in NODE_PATH. TinyCC/SDK utilities require execution outside the restricted Windows
process environment because of DLL relocation failures. Logs are ignored
`tmp/m5a-host-tests.log` and `tmp/m5a-owner-races.log`; Chrome fixtures are in
`tmp/m5a-screenshots/`. Hardware I/O, RTOS scheduling and real packet delivery are
not exercised by these tests.

### Clean build and resources â€” PASS

Fresh `build-m5a-20261026/` build, exit 0, ESP-IDF v6.1 / lock 6.1.0, GNU 15.2.0,
Python 3.11.15, esptool 5.3.1, pinned LAN87xx 1.1.0. Used a separate
`tmp/m5a-sdkconfig` copy, preserving original sdkconfig and both prior build paths.
The launcher reported a venv-path mismatch warning; dependency checks and build
passed. Five existing SDK private-include CMake warnings remain; no application
compiler warnings/errors. Log: `tmp/m5a-clean-build.log`.

| Resource | Result |
| --- | --- |
| Application | **898,480 bytes / 0xDB5B0** |
| Factory partition | **1,048,576 bytes / 0x100000**, unchanged |
| Remaining | **150,096 bytes / 0x24A50 (14.31%)** |
| Increase over saved flashed 0.4.0 | **7,344 bytes** |
| Bootloader | **26,176 bytes**, 2,496 bytes free |
| Static DRAM data | **18,231 bytes**, unchanged |
| Static DRAM BSS | **59,664 bytes**, +192 bytes |
| Worker task/stack | Same one worker, **4,096 bytes**; no extra task/socket/netif |
| Existing main/UI task stack | **3,584 bytes**, unchanged |

New bounded locals include a 60-byte transmit frame and 147-byte UI label array.
Runtime free/minimum heap and task stack high-water marks are **NOT RUN**.
The compiler/link size check is not a substitute for those measurements.
New binary SHA256:
`9cf05f74a74a56ad968039fad33569abca850224d790a48a1096d65c033131b2`.
`esptool image-info` reports version 0.4.0, valid checksum and validation hash.
The new partition binary matches the preserved factory table SHA256
`7f00b6c042a89b15b0cac534f82ed988caf29278ff5700b0c511eb1b5bb7c820`.

The external preservation package was reverified: **all 4,280 checksums PASS**.
Original saved flashed app remains 891,136 bytes with SHA256
`1bf4cee85e68e501ff0a009959ac436086b775637d2bea941d38b9c90fd4b0d1`.
This new candidate has not replaced that recovery baseline.

### Source changes in this milestone

New application files: `main/manual_ipv4.c`, `main/manual_ipv4.h` (pure validation
and ARP encoding/conflict classification).
Modified application files: `main/dhcp_test.c`, `main/dhcp_test.h`,
`main/button_logic.c`, `main/button_logic.h`, `main/buttons.c`, `main/buttons.h`,
`main/main.c`, `main/ui.c`, `main/ui.h`, `main/dashboard.c`, `main/dashboard.html`,
`main/CMakeLists.txt`. Existing uncommitted Ethernet/discovery/services changes
were retained; no new changes to those files, partition configuration or dependencies.
New tests: `tests/manual_ipv4_test.c`, `tests/m4_stubs/esp_random.h`,
`tests/run-regressions.ps1`; existing core/UI/DHCP/JSON/dashboard/browser fixtures
and the byte-order stub were extended. README was updated incrementally.

### Required hardware acceptance â€” ALL NOT RUN

1. Cold boot/reboot and unplug/replug: NO IP, Web Access OFF, unchanged splash,
   link/speed/duplex, buttons and passive Siemens LLDP/Cisco CDP. A retained draft
   must never configure the newly connected port without another review/APPLY.
2. Edit all octets/prefix/gateway, hold acceleration and cancel every page while
   DHCP is idle/requesting/acquired. Confirm no traffic/address mutation before
   APPLY. Let a real lease arrive during editing and after review; confirm the
   explicit replacement choice and fresh review requirement.
3. Packet capture of an available address: randomized initial/inter-probe delays,
   three zero-sender probes, final 2 s wait, exactly two timed announcements, no
   premature source-IP traffic/automatic GARP. Verify ARP replies while active.
4. Known occupied IP and simultaneous competing probes, including conflict during
   final wait/assignment: ADDRESS IN USE, no published active Manual address, no
   retained route/address after cleanup. No "guaranteed unused" conclusion.
5. Inject a late owner claim before/after the second announcement: immediate
   invalidation, no further address use, cleanup and explicit edit/APPLY required.
6. Trigger ten conflicts, change draft/mode/port and retry: preserve the 60 s
   attempt limit and no automatic retry. Verify UI remains responsive while waiting.
7. Disconnect during editor/wait/probes/assignment/active mode, reconnect on another
   port and observe no stale address, announcement, DHCP lease or automatic restart.
8. Repeated DHCPâ†’Manualâ†’USE DHCPâ†’explicit Start; observe DHCP RELEASE, no competing
   owners/late ACK takeover, correct optional gateway, no Manual DNS claims and
   restoration of borrowed DNS after normal/error cleanup.
9. Exercise invalid hosts, /1â€“/30 endpoints, network/broadcast/self/off-subnet
   gateway and narrow/broad AP-overlap rejection without changing AP configuration.
10. Keep hotspot clients and continuous known LLDP/CDP running throughout cycles;
    verify field accuracy, TTL/cache/link invalidation, AP address/leases/DNS,
    dashboard usability and Ethernet HTTP rejection (including AP-targeted TCP).
11. Repeat transitions/cancellation/cleanup retry at least 20 times while recording
    free/minimum heap, owner/UI stack high-water marks and serial errors.

After 5A acceptance, 5B may adapt the recovered four-request Ping target/statistics
and lifecycle components to this owner's generation/admission boundary. It will
need a separate bounded ICMP ingress policy and SDK session-retirement tests.
Do not restore the old netif glue, persistence, OTA or partition assumptions.

## Milestone 5 preflight â€” 2026-10-26 â€” implementation blocked

The owner now reports **Milestone 4 hardware validation passed**: DHCP lease
acquired in **4.4 seconds**, tester **[private tester address omitted]**, mask **255.255.255.0**,
gateway/DNS **[private gateway/DNS omitted]**, with successful DHCP release/retry and passive
discovery checks. These are supplied results, not tests performed in this session.
Earlier pending/NOT RUN statements below are historical and do not negate this
new report. Previously recorded Cisco and Siemens evidence is retained.

Preflight inspected branch `master`, HEAD `8117bcb` (0.3.0 checkpoint), descriptor
0.4.0, all application sources, test inventory, README, recovery instructions,
and existing validation notes. At entry, 22 tracked files were modified and 12
entries were untracked; all existing work was retained. No AGENTS.md was found
in the repository. ESP-IDF v6.1 remains the configured SDK; no SDK/dependency or
partition changes were made.

The supplied mission assumes systems absent from this checkout:

- Network Tests contains only DHCP Test and Back. There is no existing ICMP
  Ping/Monitor implementation or accelerated octet editor to reuse.
- There is no OTA application implementation, physical OTA authorization, or
  flash-write exclusion mechanism. `sdkconfig` selects
  `CONFIG_PARTITION_TABLE_SINGLE_APP=y`, with TWO_OTA disabled. Adding dual slots
  would violate the instruction to keep the current partition layout unchanged.
- `tmp/firmware-checkpoints/` contains a verified v0.3.0 checkpoint, tagged-source
  material and an unverified pre-M4 image, but no v0.4.0 restore package/procedure.
  All **11 v0.3.0 manifest checksums PASS**; artifacts were not overwritten.
  `build/Syndetis.bin` exists at **891,136 bytes**, but an existing build directory
  is not a verified, independently saved v0.4.0 recovery checkpoint.

Current ownership is isolated in `dhcp_test.c`: one Ethernet ESP-NETIF, one
4096-byte-stack worker, critical-section state copies, explicit request mailbox,
link-session guards, RX/TX gates and in-flight RX drain. DHCP completion polls
live stack state rather than registering IP callbacks. Cleanup synchronously
stops/removes the interface, clears current/old IPv4 and restores borrowed global
resolver slots in TCP/IP context. AP DHCP-server DNS remains separate.
`receive_dispatch.h` borrows frames for bounded passive discovery, then transfers
eligible frames to ESP-NETIF once or frees them. `network_policy.c` currently
admits only ARP/DHCP; ICMP is not admitted. Ethernet TCP and non-hotspot browser
requests remain blocked. Discovery has independent generation/TTL/cache handling.
The three-button logic retains debounce and short/hold OK; Web Access boots OFF.

No firmware implementation was selected or changed: safe manual-address
integration requires the intended baseline or clarification of the missing
subsystems. No manual address can be assigned by these documentation changes.
There is no new ARP probing, lifecycle, UI or conflict-detection implementation
to claim as tested. Do not treat existing ARP admission as RFC 5227 support.

Executed existing regressions: host core, UI, JSON serializer, DHCP worker/RX
dispatcher, HTTP actions and socket guard **PASS**; Node JSON/dashboard **PASS**.
TinyCC initially could not start under the sandbox (Windows DLL relocation);
rerunning the existing runner outside the sandbox passed those suites. Browser
test initially could not resolve Playwright; supplying the installed runtime
module path allowed the separate Chrome phone/laptop rerun to **PASS**, including
layout, stale/recovery, text safety and absence of external requests.

Milestone 5 tests, physical ARP conflict checks and new firmware clean build:
**NOT RUN**, because implementation is blocked at preflight. Existing image size
is **891,136 bytes**, partition **1,048,576 bytes**, free **157,440 bytes**;
these are existing artifacts, not a new build result. There is no firmware
resource increase; runtime heap/stack measurements were not performed.

Before hardware acceptance: reconcile the baseline and save/verify its recovery
package; then implement and test validation, explicit APPLY/cancel/DHCP races,
RFC 5227 probing/announcements/rate limiting, immediate conflict relinquishment,
generation-safe cleanup, Ping/Monitor reuse, AP isolation and OTA exclusion.
Physical testing must include occupied addresses, competing probes, late
conflicts, link loss/reconnect, local/routed targets, repeated DHCP/manual
transitions, simultaneous LLDP/CDP and hotspot use, and heap/stack checks.
Mocks cannot prove physical ARP conflict handling.

No flash, erase, commit, tag, push, remote or GitHub change was performed.
For later separately authorized USB validation, use README's 3.3 V UART/IO0
procedure and an actual COM port with `idf.py -p COM_PORT flash`, release IO0
and reset, then `idf.py -p COM_PORT monitor`. Do not flash this as Milestone 5.
Verified historical rollback instructions remain in RESTORE_0.3.0.md; they are
not a v0.4.0 rollback procedure.

## Targeted DHCP repair â€” 2026-10-26

Project descriptor/README remain **0.4.0**, ESP-IDF **v6.1 / 6.1.0**, on
`master`. At entry, 22 tracked files were modified and 12 untracked entries
(including the DHCP implementation/tests, network policy and restore notes)
were present. Existing Milestone 4 work and the v0.3.0 baseline are preserved.
This repair changes only DHCP lifecycle/reporting, related tests and these notes.
No flash, erase, commit, push, GitHub change or branch change was performed.

### Field report and proven defect

Owner-reported 2026-10-26 Cisco IR1101 observation: link UP, 100 Mbps/full
duplex, passive CDP neighbor received with advertised management IPv4/native
VLAN, zero observed frame/drop counters. Internal identity/address/MAC values
are intentionally omitted here. Explicit DHCP Start waited 30.0 seconds, then
displayed API failure, no current IPv4, `esp_netif_set_dns_info(clear)`, `0x5001`.
This is supplied field evidence, not a new device test of the repaired image.
No separate photographs/evidence package was supplied; none is required to repair
the firmware. No image links or hardware results have been invented.

**Proven by source inspection:** timeout sets `DHCP_TIMEOUT` and calls cleanup.
The old cleanup constructed `esp_netif_dns_info_t dns = {0}` and passed it to
`esp_netif_set_dns_info(netif, i, &dns)`. In the installed pinned SDK,
`components/esp_netif/lwip/esp_netif_lwip.c:2220`, `ESP_IP_IS_ANY(dns->ip)` rejects
this zero IPv4 address with `ESP_ERR_ESP_NETIF_INVALID_PARAMS` (`0x5001`) before
dispatching to the TCP/IP thread. It is not a stale-netif or address-type problem
in this call. Cleanup then overwrote the primary state with `DHCP_ERROR`.
The original host stub incorrectly accepted zero DNS and hid the defect.

**Unproven:** why acquisition timed out on that network. Server availability,
VLAN/server policy, packet delivery and offer/ACK handling require hardware logs
and packet capture. No lease within 30 seconds does not prove server absence,
no replies, or a network fault. No successful physical DHCP acquisition is claimed.

### Correction and isolation

Primary state/`error`/`error_step` are retained; cleanup has independent
`cleanup_error`/`cleanup_code`/`cleanup_step`, shown separately on OLED and browser
and exported as separate JSON fields. A failed cleanup blocks Start until an
explicit Stop / Release retry succeeds. Timeout wording is now **No lease within
30 s**, since the worker measures lease acquisition, not absence of all replies.

The firmware has one IP client and global resolver DNS (generated config has
`CONFIG_ESP_NETIF_SET_DNS_PER_DEFAULT_NETIF` disabled). Before DHCP starts, all
resolver slots are saved and cleared using lwIP's supported `dns_setserver(i,
NULL)` inside synchronous `esp_netif_tcpip_exec`. This avoids inherited DNS on
a later lease with no DNS options. After RX drain and client stop, the exact saved
slots, including empty and fallback slots, are restored in TCP/IP context.
Failed restoration retains the snapshot for retry. A compile-time guard rejects
per-default-netif DNS configuration until its different ownership is reviewed.

SDK `esp_netif_stop_api` releases/stops/cleans the Ethernet DHCP client, resets
its IP, removes that interface and updates default routing. It does not stop the
AP. SDK DNS getters/setters select `dhcps_dns_*` storage for DHCP-server netifs;
`components/lwip/apps/dhcpserver/dhcpserver.c` keeps that storage in `dhcps_t`,
independent from `dns_getserver`/`dns_setserver`. DHCP cleanup never receives the
AP pointer, never sets AP IP, and never calls Wi-Fi, HTTP or AP DHCP-server APIs.
Thus it cannot set hotspot network configuration/address or server DNS; global
resolver state returns to its pre-test values. No other IP client may be added
without revisiting this ownership assumption.

RX/TX gates, callback drain, link-generation guards, explicit-only reconnect,
overlap rejection and Ethernet TCP blocking are preserved. Discovery, Ping,
Monitor, OTA, Wi-Fi startup and physical controls were not changed. The small
OLED/browser edits only display DHCP cleanup diagnostics. Web Access remains
physical-enable only, boot OFF, at `http://192.168.4.1`.

### Executed software checks

- **PASS:** all six host C suites rebuilt with TinyCC 0.9.27: core LLDP/CDP,
  OLED, JSON serializer, actual DHCP worker/dispatcher, HTTP actions, socket guard.
- **PASS:** Node dashboard and JSON suites; Playwright installed Chrome at
  phone/laptop sizes. Primary/cleanup JSON fields and browser display checked.
- **PASS:** SDK-faithful zero-DNS rejection reproduces `0x5001`; simulated known
  lease/settings/DNS, lease without DNS, timeout, cancellation before/after Start,
  Stop / Release, link-loss/reconnect, address loss, overlap and start failure.
- **PASS:** 32 Start -> Timeout -> Retry and 32 Start -> Cancel -> Start cycles;
  assert stopped client/interface, closed RX/TX, no pending command/inflight RX,
  no current IPv4/DNS and exact pre-test resolver restoration after each cycle.
  Discovery dispatch continues during and after each cycle; original buffers
  are freed once. Existing actual LLDP/CDP parser/lifecycle fixtures also pass.
- **PASS:** timeout plus cleanup failure keeps timeout; simultaneous primary API
  and cleanup failures retain both names/codes. Start blocked until successful
  cleanup retry; failed DNS restore keeps its saved state. AP address/server DNS
  sentinels survive cleanup and setter/stop stubs assert Ethernet target ownership.

Logs: ignored `tmp/dhcp-fix-host-tests.log`; reproducible suite runner
`tmp/run-dhcp-fix-checks.ps1` uses README compile commands and installed local
tools. TinyCC requires execution outside the sandbox due to Windows DLL
relocation errors. Initial build launcher lacked `ESP_IDF_VERSION`; corrected
process environment uses installed SDK paths without changing system settings.

**Clean firmware build â€” PASS:** `idf.py --version` reported ESP-IDF v6.1;
`idf.py fullclean build` completed with exit 0 using the existing `build/` path,
GNU 15.2.0, SDK Python 3.11.15 and locked LAN87xx 1.1.0. Application
`build/Syndetis.bin`: **0xD9900 / 891,136 bytes**, **1,048,576-byte** partition,
**157,440 bytes free (15.0%)**; +256 bytes versus the recorded Milestone 4 image.
Bootloader **26,176 bytes**, **2,496 bytes free**. Both size checks pass.
No application compiler warnings/errors; five existing SDK CMake private-include
warnings remain. Log: ignored `tmp/dhcp-fix-clean-build.log`.

### Hardware acceptance â€” NOT RUN on repaired firmware

After separate flash authorization, use README's USB-UART/IO0 procedure and
`idf.py -p COM_PORT flash`, release IO0/reset, then `idf.py -p COM_PORT monitor`.
Do not erase NVS. Retain the v0.3.0 restore artifacts/procedure. Capture serial
output and Ethernet DHCP traffic with timestamps; keep field identities private.

1. Known isolated DHCP server: Start; capture DISCOVER/OFFER/REQUEST/ACK, compare
   actual lease/mask/gateway/DNS with OLED/browser, wait for renewal, then Stop.
   Capture RELEASE, check current address clears; repeat with DNS options omitted.
2. Verified server-disabled test segment: repeat Start -> 30 s timeout -> Retry
   at least 20 times. Record primary result and separate cleanup errors, confirm
   client traffic stops between tests, no stale IPv4/DNS and no heap degradation.
3. Repeat Start -> Back/Stop cancel -> Start at least 20 times, including immediate
   cancel and near-ACK cancellation. Check no late address, extra client/request
   or automatic DHCP after unplug/replug. Repeat link loss during lease/request.
4. Feed continuous known CDP and LLDP before/during/after all cycles; compare
   advertised fields/counters/expiry and verify no new discovery/link session is
   created by DHCP cleanup. Host dispatch tests do not prove physical reception.
5. With hotspot physically enabled and a dashboard client connected, repeat
   success/timeout/cancel/release and overlap rejection (192.168.4.0/24 plus a
   broader containing subnet). AP remains 192.168.4.1/24 with unchanged management
   leases/DNS, dashboard usable; Ethernet HTTP remains blocked including AP-targeted
   TCP. Repeat Web Access OFF/ON and reboot (boot OFF/passive).

Remaining Milestone 4 checklist below still applies. Static IPv4/Ping/Monitor
work remains deferred until DHCP hardware validation; no Milestone 5 code added.
The following sections are historical snapshots; their descriptions of global
DNS clearing and timeout wording are superseded by this repair.

## Milestone 4 â€” firmware 0.4.0

Date: 2026-10-20. Starting commit/tag: `8117bcbc8f41ae9a2197daa16c71dd1535cb7616`,
annotated `v0.3.0`. Working tree was clean at start. The tag, remotes and GitHub
were not changed. Milestone 4 adds only the explicit Ethernet DHCP test and its
OLED/browser controls. No flash, erase, publication or push was performed.

### Checkpoint and rollback â€” PASS

Before rebuilding, existing application/bootloader/partition binaries and flash
arguments were copied to ignored `tmp/firmware-checkpoints/pre-m4-existing-unverified/`.
The pre-existing `Syndetis_flashed.bin` is retained without asserting tagged provenance.
An isolated extraction of `v0.3.0` was built under the pinned SDK. All 49 extracted
tracked inputs were compared to the tag with Git LF/CRLF normalization. Saved
sdkconfig, locked component, build log, ELF, flash binaries/arguments, provenance
and SHA-256 manifest are in `tmp/firmware-checkpoints/v0.3.0-8117bcb-verified/`.
The reproduced app is **0xD6870 / 878,704 bytes**. `esptool image-info` reports
Syndetis **0.3.0** and a valid image hash. This is a verified reproduction, not a
claim of byte identity with the currently flashed device. The exact USB restore
procedure is in `RESTORE_0.3.0.md` and copied alongside the artifacts.

### Clean build â€” PASS

`idf.py fullclean` followed by `idf.py build` succeeded (exit **0**) under
**ESP-IDF v6.1**, dependency/header **6.1.0**, GNU **15.2.0**, Python **3.11.15**,
esptool **5.4.0**, ESP32 / 4 MiB flash, LAN87xx **1.1.0**. A final incremental
build after dashboard wording changes also succeeded and produced the delivered
image. App **0xD9800 / 890,880 bytes**, factory app partition **0x100000 /
1,048,576 bytes**, remaining **0x26800 / 157,696 bytes (15.0%)**.
Bootloader **0x6640 / 26,176 bytes**, **0x9C0 / 2,496 bytes** free. Both size
checks pass. The partition table is byte-identical to the verified 0.3.0 backup.
No application compiler warnings/errors; five existing SDK CMake private-include
warnings between esp_wifi/wpa_supplicant remain. Logs: ignored
`tmp/m4-final-build.log` (clean) and `tmp/m4-final-incremental.log` (final image).

### Software results

- **PASS:** actual DHCP worker compiled with host TinyCC 0.9.27 and SDK I/O
  stubs. Boot passive/no transmit; start with/without link; duplicate/conflicting
  starts; acquisition/settings/duration; 30-second timeout; cancellation before
  and after worker start; ignored late state; stop/release; link-generation
  changes and reconnect; lease loss and explicit-only retry; narrow/broad
  management-subnet overlap; API failure and cleanup-error retry.
- **PASS:** actual shared receive dispatcher and DHCP receive path: discovery
  observes the borrowed frame; one owner consumes/frees each original, including
  stack-input errors; no stack delivery in passive mode; Ethernet TCP rejected.
  ARP/DHCP are admitted while active; unrelated UDP, fragments and tagged IPv4
  are rejected. Existing LLDP/CDP parser/lifecycle fixtures remain passing.
- **PASS:** actual HTTP handlers with I/O stubs: 202 asynchronous acceptance,
  409 conflicts, bounded two-byte body/partial reads, exact `{}` validation,
  Host/Origin/content-type rejection, and hotspot guard on GET/POST routes.
  Socket guard tested separately: accepts AP-local/management peer only,
  rejects Ethernet-local addresses, outside peers and socket lookup failure.
- **PASS:** parser/table/button regression (actual portable C): LLDP/CDP checksums,
  long fields, mutation/bounds, expiry/withdrawal, link sessions and short/hold.
- **PASS:** actual OLED UI with I/O stubs: six-item menu, DHCP submenu, requesting
  Back cancellation, successful lease survives navigation, Stop / Release;
  unchanged Web Access Back behavior and long-field rendering within bounds.
- **PASS:** actual HTTP serializer: worst-case four escaped records plus DHCP fit
  the unchanged 16,384-byte response bound; null/empty, escaping and hostile text.
- **PASS:** dashboard script in Node: serialized polling, stale/recovery,
  current DHCP address rendering/clearing, action POST/body/duplicate suppression,
  202/409 feedback, disabled controls on stale status, existing neighbor rendering.
- **PASS:** Playwright with installed Chrome, 375 x 812 and 1280 x 900: actual
  embedded dashboard, Start/Stop POSTs and current address transition; existing
  long-name wrapping, injection-as-text, null/empty, expiry/link clearing,
  stale/recovery, one poll at a time, no external resources or JavaScript errors.
  Screenshots remain ignored in `tmp/screenshots/`; these are synthetic fixtures.

Commands are in README. Only I/O is stubbed in host checks; these do not exercise
real DHCP servers, PHY/RF or TCP stacks. Compiler tools require execution outside
the restricted process environment on this Windows host (DLL relocation failures
inside it); no toolchain or system setting was changed.

### Pinned SDK integration review

Inspected local ESP-IDF v6.1 sources: `esp_eth_netif_glue.c` would replace the
input callback, so it is deliberately not attached. A small driver configuration
uses the existing driver handle, guarded transmit and RX free callback. Public
`esp_netif_action_start/stop` are called only by the DHCP worker; no default
Ethernet netif event handlers are registered. Start uses AUTOUP without GARP or
IPv6 flags, then explicitly starts the standard client. Stop's implementation
calls DHCP release/stop/cleanup and removes the stack interface; postconditions
are checked, followed by explicit stopped state and clearing cached IPv4/DNS.
The netif allocation is retained for reuse, never freed under receive callbacks.

`ethernetif_input` consumes buffers on success, allocation failure and input failure.
The dispatcher therefore never frees a buffer after handoff. A gate/in-flight
counter drains callbacks before the synchronous TCP/IP-thread stop. IPv4/ARP
traffic is gated independently of discovery; all-multicast filtering, parser,
queue, identity keys, PHY/MAC and real link-session clearing remain intact.
DHCP start/stop does not call discovery_link or esp_eth_start/stop.

The worker reads live IP state every 50 ms and ignores queued IP notifications.
Cancellation/link generation changes suppress stale publication; IP loss ends
the session without waiting for the SDK's delayed LOST_IP event. Renewal is left
to the standard client while active. Ethernet's receive filter rejects all TCP
before the stack, including packets to the AP address, so the wildcard HTTP
listener cannot be reached via Ethernet. Local/peer socket checks guard every
handler as a second boundary. HTTP input and socket policy checks are software
verified; physical Ethernet reachability still needs the checklist below.

SDK client DNS clearing uses global resolver slots; AP DHCP-server DNS is stored
separately in the server object. There is no other IP client in this firmware.
AP startup fix/address and physical Web Access worker are preserved. Compile-time
checks require IPv6, AutoIP, saved leases and forwarding disabled. No route/NAT/
bridge/scanning/static address or other diagnostics were added.

### Milestone 4 hardware checklist â€” ALL PENDING

1. [ ] Boot without DHCP: existing LLDP/CDP identification still works; capture
   confirms no Ethernet DHCP, IPv6 or other unsolicited IP traffic in passive mode.
2. [ ] Start DHCP with a server; verify actual IP/mask/gateway/DNS and acquisition
   time. Verify renewal while active; reject duplicate Start.
3. [ ] Observe discovery continuously before, during and after DHCP.
4. [ ] Start without a server; 30-second no-reply outcome leaves link/discovery working.
5. [ ] Cancel requesting with Back/Stop; no late address becomes current.
6. [ ] Stop after success; current IPv4 disappears, RELEASE is observed where
   possible, discovery continues. Check offered subnet overlap is released/rejected
   without changing the AP address; check readable cleanup failures/retry.
7. [ ] Unplug/replug after success: no stale lease, no automatic restart. Also
   force lease loss while linked: Address lost, passive until explicit Start.
8. [ ] Hotspot/dashboard remains usable during acquisition, timeout and release.
9. [ ] Disable/re-enable Web Access without disturbing Ethernet DHCP or discovery.
10. [ ] Reboot after success: passive Ethernet and Web Access OFF; recheck splash,
    short-on-release/500 ms hold and boot with GPIO5 OK held.
11. [ ] Repeat the known Siemens S7-1200 LLDP and Cisco CDP field checks.
12. [ ] Confirm dashboard, status and DHCP controls are inaccessible via Ethernet,
    including Ethernet packets aimed at the AP address; same-origin hotspot
    controls work and foreign-origin browser actions fail.

Firmware 0.3.0 reception with those two devices is the owner's verified baseline.
No Milestone 4 hardware success is claimed. Stop here pending hardware acceptance.

## Milestone 3 â€” firmware 0.3.0

Date: 2026-10-19. Implemented passive LLDP/CDP Switch Info, password `Syndetis`
and global debounced short/release versus 500 ms hold/Back behavior. The sections
below this milestone are historical. The original implementation session did
not flash or erase hardware. Owner-reported evidence added for the 0.3.0
checkpoint: LLDP reception tested with a Siemens S7-1200; CDP reception tested
with a Cisco device. No deployment organization or field network addresses are
recorded. Reception does not establish full field accuracy or lifecycle acceptance.

### Build and software checks

Clean build: **PASS**, `idf.py fullclean` then `idf.py build`, exit **0**.
SDK **ESP-IDF v6.1**, header/dependency version **6.1.0**, GNU **15.2.0**
(`esp-15.2.0_20251204`), Python **3.11.15**, esptool **5.4.0**, ESP32 target.
Official LAN87xx remains pinned to **1.1.0**. Firmware version is **0.3.0**.
Application image **0xD6800 / 878,592 bytes**, leaving **0x29800 / 169,984
bytes (16%)** in the 1 MiB factory application partition. Bootloader image
**0x6640 / 26,176 bytes**. Both size checks passed. Five existing SDK CMake
private-include dependency warnings between `esp_wifi` and `wpa_supplicant`
remain; no application compiler warnings/errors.

The following checks passed with synthetic data and stubbed hardware I/O:

- `tests/core_test.c` compiles the actual portable parser, table and button
  logic with host TinyCC **0.9.27**. Valid LLDP/CDP fields and missing optional
  fields; IPv4/IPv6; explicit versus fallback CDP addresses; LLDP mandatory
  ordering; CDP LLC/SNAP and checksum vectors (including Cisco odd-byte
  convention); truncated, malformed, oversized and tagged frames; long raw
  identity/text bounds; empty versus absent values; 10,000 bounded mutations;
  TTL expiry and zero-TTL withdrawal for both protocols; capacity/drop/reuse;
  late advertisements; link-session clearing and rejection of old-session or
  already-expired queued data. OK release, hold threshold, one-shot Back,
  suppressed short action, bounce and splash/reset release gating also pass.
- `tests/ui_test.c` compiles the actual OLED UI with test-only I/O stubs.
  Back changes one parent level and never calls Enable/Disable; main-menu Back
  does nothing. Five-item viewport, neighbor/detail navigation, full supported
  long-string scrolling and all drawn coordinates within 128 x 64 pass.
- `tests/status_json_test.c` and `tests/status_json.test.cjs` exercise the actual
  HTTP serializer: four worst-case escaped records fit the 16 KiB response;
  quotes, backslashes, controls and network markup round-trip as text, and
  absent/null stays distinct from present/empty.
- `node tests/dashboard.test.cjs`: serialized polling, text-only discovery
  rendering, clearing, age, stale/recovery, malformed JSON, timeout and
  Ethernet-error states pass.
- `node tests/dashboard.browser.cjs`: headless installed Chrome, 375 x 812
  and 1280 x 900. Long names wrap without horizontal overflow; hostile markup
  stays text; current/expired, unavailable/empty, link clearing and recovery
  pass. No extra/external requests or JavaScript errors. Screenshots in ignored
  `tmp/screenshots/` are synthetic fixtures, not device observations.

Host test stubs/tools are not firmware components. Commands are in README.

### SDK/lifecycle implementation review

The installed pinned SDK supports `esp_eth_update_input_path` and
`ETH_CMD_S_ALL_MULTICAST` for the existing ESP32 EMAC driver. All-multicast
reception is enabled; promiscuous mode is unnecessary. The SDK DMA path removes
the four-byte FCS before delivery. Existing driver/pins/physical link monitoring
remain in place; discovery adds no Ethernet netif, IP, DHCP client or transmit
call. The callback filters/copies into four fixed queue slots without waiting;
the worker parses outside the callback. Every callback path frees its SDK RX
buffer. Records and queue storage are bounded. Link changes clear records and
advance a session generation; the worker rechecks it after parsing.

OLED and HTTP read the same table. Four LLDP/CDP records stay separate by
protocol/source/raw identity/port. Capacity, queue and parsing limits are
reported. A 30-second observation interval does not stop listening. Expired
identities are marked Expired until reused; withdrawal removes matching data.
Link down clears identity immediately. Missing data is never inferred.

Single 802.1Q tagging is supported. Stacked tags/QinQ, jumbo frames, alternate
LLDP LLC/CDP Ethernet-II framing, other discovery protocols and address families
beyond IPv4/IPv6 are unsupported. LLDP-MED/DCBX, capabilities, software/platform
and full VLAN lists are not extracted. README details these limits, field
lengths and text sanitization. Advertised VLAN is only PVID/native VLAN;
advertised addresses belong to the neighbor, not to Syndetis.

Web Access remains physical-only and OFF after boot; Back never changes it.
The previous default-AP/DHCP lifecycle correction is retained: no IP setter or
manual DHCP override, default AP address 192.168.4.1, default handlers manage
DHCP start/stop. HTTP and Wi-Fi cleanup/reuse and operation/name/hex error logs
remain unchanged. Repeated on-device enable/disable is still a hardware check.

### Hardware checks â€” reception tested; remaining acceptance pending

- [ ] Short OK selects only on release; stable 500 ms hold goes back once,
  never activates first, and requires release before another action.
- [ ] Back from Web Access leaves its enabled/disabled state unchanged.
- [ ] Forget saved credentials if necessary; reconnect using exactly `Syndetis`.
- [ ] Known LLDP/CDP switch identities/ports/advertised fields match configuration.
- [ ] Discovery works with Web Access OFF and no Ethernet IP; OLED/browser agree
  when Web Access is enabled.
- [ ] Unplug immediately clears identity; another port/switch cannot show the
  previous session as current, including queued-frame races.
- [ ] No-advertisement ports show Waiting then No advertisement after 30 seconds;
  a later advertisement is accepted. Check TTL expiry and withdrawal.
- [ ] Packet capture confirms no discovery advertisements/queries or Ethernet
  DHCP traffic from Syndetis; multicast reception works on the physical EMAC.
- [ ] Existing link speed/duplex/status, wiring, SH1106 rendering, three-second
  splash, navigation, physical Web Access, repeated enable/disable and boot-OFF
  behavior remain correct. Enabled hotspot supplies management DHCP leases.

Changed implementation: root/main CMake files; `main/access_config.h`,
`board.h`, `buttons.c/.h`, `button_logic.c/.h`, `neighbors.c/.h`,
`discovery.c/.h`, `ethernet.c`, `ui.c`, `display.c`, `dashboard.c/.html`.
Validation additions/updates are in `tests/`; README, VALIDATION and ROADMAP
describe the delivered milestone and pending hardware gate.

## Web Access startup correction â€” firmware 0.2.0

Hardware report: Web Access failed with `WiFi initialization: 0x5007`, HTTP
stopped and hotspot stopped. This section records the correction, not a hardware
success. The initial-build results below are historical.

### Exact root cause and pinned-SDK verification

The failing API was **`esp_netif_set_ip_info(ap_netif, &ip)`** in `prepare_wifi`,
after AP netif creation/attachment/default-handler registration and **before
`esp_wifi_init()`**. The outer WiFi initialization label obscured that API.

The installed ESP-IDF v6.1 / lock version 6.1.0 source was inspected directly:

- `components/esp_netif/include/esp_netif_types.h`: 0x5007 is
  `ESP_ERR_ESP_NETIF_DHCP_NOT_STOPPED`; DHCP INIT is zero, STARTED and STOPPED
  are distinct states.
- `components/esp_netif/lwip/esp_netif_lwip.c`, `esp_netif_new_api`: the AP
  object is zero-initialized, so a newly created DHCP server is in **INIT**,
  not STARTED. The original setter ran before Wi-Fi startup; the error does
  **not** demonstrate that it was actively serving leases.
- The same file, `esp_netif_set_ip_info_api`: with `ESP_NETIF_DHCP_SERVER`,
  **any state other than STOPPED**, including INIT, returns 0x5007.
- `components/esp_netif/esp_netif_defaults.c`, `_g_esp_netif_soft_ap_ip`,
  and `include/esp_netif_defaults.h`: the selected default AP configuration
  already supplies **192.168.4.1**, gateway **192.168.4.1**, mask
  **255.255.255.0**, DHCP server and AUTOUP flags.

The smallest functional fix removes the redundant IP setter and retains that
default. No manual DHCP stop/start, new interface, or Ethernet DHCP client was
added. Firmware version and dependency pins remain unchanged.

### Lifecycle and failure-path review

- The registered default `WIFI_EVENT_AP_START` handler in
  `components/esp_wifi/src/wifi_default.c` calls `wifi_start`, then
  `esp_netif_action_start`. `esp_netif_start_api` brings the AP netif up and
  starts its DHCP server from INIT.
- `WIFI_EVENT_AP_STOP` calls `esp_netif_action_stop`; `esp_netif_stop_api`
  stops DHCP and returns its normal state to INIT. The application never
  manually puts DHCP into STOPPED, which would suppress automatic restart.
- A successful disable stops HTTP and Wi-Fi but retains the initialized Wi-Fi
  driver, AP netif and one set of handlers. Subsequent enable reuses them and
  starts Wi-Fi/HTTP again; ordered AP stop/start events manage DHCP each cycle.
- If HTTP start/route registration fails after AP start, cleanup stops any
  retained HTTP handle and then Wi-Fi. Both cleanup operations are attempted
  independently. Each handle/running flag is cleared only on successful stop;
  failed shutdown remains an error with Retry cleanup. These paths are preserved.
- All Web Access startup/stop failures now log operation names,
  `esp_err_to_name()` and hexadecimal codes, including individual HTTP API
  failures. Passwords are not logged. OLED keeps a short operation/code plus
  separate HTTP/hotspot state lines; cleanup failures take display priority.
- Physical menu remains the only requester of enable/disable; boot state is
  still OFF. Ethernet monitoring, board wiring, display/splash, buttons and
  navigation were not edited. No Ethernet netif/client was added.

This is an implementation and SDK-source review of repeated-cycle behavior,
not an executed on-device cycle or fault-injection test.

### Clean build â€” PASS

`idf.py --version` reported **ESP-IDF v6.1**. From the project root, after SDK
activation, **`idf.py fullclean` followed by `idf.py build` succeeded**, final
exit code **0**, `Project build complete`. GNU **15.2.0**, Python **3.11.15**,
esptool **5.4.0**, ESP32 target; LAN87xx **1.1.0** remains pinned.

Application image: **0xD3C10 / 867,344 bytes**. Application partition free:
**0x2C3F0 / 181,232 bytes (17%)**. Image and bootloader size checks passed.
No application compiler warnings/errors; five existing SDK CMake private-include
dependency warnings between `esp_wifi` and `wpa_supplicant` remain. Build logs
are in `build/log/`.

Changed files: `main/services.c`, `main/dashboard.c`, `README.md`, `VALIDATION.md`.
No hardware flash, erase or publication was performed. Hardware verification is
still required: physical Enable obtains a management lease at 192.168.4.x and
opens the dashboard; Disable closes active access; repeated re-enable works;
reset returns OFF; Ethernet remains operational with Web Access OFF. Also check
that any future failure prints the precise API/name/code and a readable OLED error.

## Milestone 2 initial build â€” firmware 0.2.0 (historical)

Date: 2026-10-19. Scope: Ethernet physical link monitoring without Ethernet IP,
physical-only Web Access, WPA2 hotspot and flash-served read-only dashboard.

### Clean build â€” PASS

- Exact SDK: **ESP-IDF v6.1**, header/lock version **6.1.0**, confirmed by
  `idf.py --version`; installed at `[original machine path omitted]`.
- Compiler: Xtensa GNU **15.2.0**, tool package `esp-15.2.0_20251204`.
- SDK Python **3.11.15**; esptool **5.4.0**. Target **esp32**, flash **4 MiB**,
  FreeRTOS tick **1000 Hz**. Project/app descriptor version **0.2.0**.
- Official managed PHY component: **espressif/lan87xx 1.1.0**, pinned in
  `main/idf_component.yml` and `dependencies.lock`. ESP-IDF 6.1 no longer bundles
  this PHY-specific driver. No generic Ethernet initialization wrapper or Arduino.
- Final commands, after ESP-IDF activation, from the project root:

  ```powershell
  idf.py fullclean
  idf.py build
  ```

- Both final commands succeeded; build exit code **0**, `Project build complete`.
  All eight application C files, embedded HTML, PHY driver and bootloader built.
- Application `build/Syndetis.bin`: **0xD39D0 / 866,768 bytes**.
  Default application partition: **0x100000 / 1,048,576 bytes**;
  **0x2C630 / 181,808 bytes (17%) free**. Partition size checks passed.
- Bootloader: **0x6640 / 26,176 bytes**; partition table: **3,072 bytes**.
- No application compiler warnings/errors in the final build logs. ESP-IDF's
  CMake component validation emitted five private-include/dependency warnings
  between its own `esp_wifi` and `wpa_supplicant` components. SDK files were not
  modified. Logs are under `build/log/` (ignored generated output).
- The first attempt failed because the migrated PHY header/component was missing
  and GCC detected a potentially truncated SSID copy. Both were fixed, then the
  complete clean build above was rerun successfully. A stale RMII Kconfig symbol
  was also removed; external input clock selection is explicit in the MAC config.

### Software checks â€” PASS within the limits below

- `node tests/dashboard.test.cjs`: executes the actual embedded JavaScript in
  a DOM/fetch/timer harness. Passed live field rendering, serialized requests,
  disconnect/stale retained values, automatic recovery, tab-age stale detection,
  malformed JSON rejection, timeout and explicit Ethernet initialization errors.
- `node tests/dashboard.browser.cjs`: Playwright with installed Chrome, at
  **375 x 812** and **1280 x 900**. Passed no horizontal overflow, link/speed/duplex
  changes, stale display and recovery, one request at a time, no external resource
  requests and no page JavaScript errors. Phone/live and laptop/stale screenshots
  were visually inspected. These tests use explicit intercepted fixtures, not
  measured network results or a real device. They start no device HTTP listener.
- Source review: unchanged SH1106 configuration/renderer, button module and splash
  drawing/timing. New Ethernet pins agree with the manufacturer schematic mirrored
  in README; PHY address is 1, GPIO16 enables the oscillator, clock input is GPIO0,
  and the PHY reset GPIO is explicitly -1 so the SDK default cannot drive GPIO5.
- Physical-access boundary review: only OLED input calls the Web Access request
  function. Boot starts Ethernet monitoring but does not initialize/start Wi-Fi
  or HTTP. The only HTTP routes are read-only GET `/` and GET `/api/status`.
  No enabled state is persisted. HTTP is really started/stopped rather than gated
  behind a page flag. Disable attempts HTTP and AP stop independently; error state
  preserves failed shutdown flags and offers Retry cleanup. A partial startup
  rolls back services that already started.
- Concurrency review: one brief critical-section status snapshot is copied by
  OLED/HTTP. Ethernet callbacks only copy cached negotiated state. Service work is
  queued to a worker; OLED drawing remains on the main task. No cable/IP/client
  or browser-connection wait is used.
- Ethernet has no netif/IP attachment; its receive callback frees frames without
  delivering them to an IP stack. Only the AP netif exists when prepared. AP-only
  mode is explicit, and hotspot DHCP server support is enabled. Generated config
  has IPv4 forwarding and IPv6 disabled. No station interface, NAT, discovery,
  ping, TCP probe, SD, OTA or Ethernet DHCP client is started.
- Password is defined once in `main/access_config.h`, displayed only through the
  physical details screen while enabled, and excluded from routine logs/status
  JSON. Read-only dashboard consistently labels Ethernet IP Not configured.

Builds ran outside the restricted process environment because the installed
Windows toolchain cannot execute correctly within it. No SDK installation or
system configuration was changed.

These are compilation, browser-fixture and source-review results. They do not
prove RF/PHY operation, actual shutdown behavior, debounce timing or heap stability
on the board. Startup/shutdown failure handling was reviewed in source; it has
not been fault-injected on hardware.

### Milestone 2 hardware checklist â€” ALL PENDING

1. [ ] Boot preserves splash/menu; WEB OFF and no hotspot.
2. [ ] Ethernet link monitoring works with browser OFF; negotiated values clear
   when down, and initialization errors appear explicitly.
3. [ ] Physical Enable starts WPA2 hotspot and offline dashboard. Verify readable
   SSID/password/URL in scrolling Connection details on OLED.
4. [ ] OLED/browser agree on unplug/replug, speed, duplex and actual Ethernet MAC;
   Ethernet IP stays Not configured.
5. [ ] Main-menu navigation leaves access enabled; WEB ON persists without
   colliding with the Ethernet indicator.
6. [ ] Disable with dashboard/API sessions open: HTTP connections close, hotspot
   disappears, clients disconnect, requests fail and cached dashboard becomes
   Tester unreachable/stale. Ethernet/OLED continue operating.
7. [ ] Re-enable without reboot and recover live browser data.
8. [ ] Reset while enabled: returns OFF, no hotspot or listener until physical Enable.
9. [ ] Repeat enable/disable at least 20 times; monitor free/minimum heap and serial
   errors for crashes/leaks/failing starts. Check cleanup/error retry where feasible.
10. [ ] Existing UP/DOWN/OK, debounce/hold behavior, scrolling/page return, splash
    duration and splash input suppression remain correct. Test cold boot/reset
    with GPIO5 OK held LOW and released.

Full Windows build, COM-port flash/monitor instructions, wiring and access-boundary
notes are in README. Stop here until these device checks pass.

## Milestone 1 historical record â€” 0.1.0

The previous milestone clean-built with ESP-IDF v6.1 / GNU 15.2.0 on 2026-10-19.
Application size was **160,096 bytes**; OLED/buttons/splash/navigation were then
implemented without networking. Its hardware testing has since **passed as
reported by the owner** in the Milestone 2 request. That report does not imply
Milestone 2 hardware acceptance.

No hardware was flashed, erased, monitored or operated during this implementation.
No changes were published. No later networking milestone was implemented.
