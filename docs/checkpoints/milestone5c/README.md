# Syndetis5C

Portable accepted Milestone 5B checkpoint. Application/About version stays **0.4.0**. The owner reports **5A and 5B hardware validation passed**. No firmware functionality or partition layout changes are introduced by packaging.

ESP-IDF **v6.1** is pinned to `fff9895c82d744c7237be8847347bdd1b07c6643`, with ESP32/WT32-ETH01 and LAN87xx 1.1.0. DHCP, Manual IPv4, ARP conflict protection, four-request Ping, passive CDP/LLDP, OLED/buttons and physically enabled hotspot Web Access are included. Continuous Monitor and OTA are not implemented.

- [Linux setup](LINUX_SETUP.md)
- [USB recovery](RESTORE_USB.md)
- [Migration manifest](MIGRATION_MANIFEST.md)
- [Verification](docs/VERIFICATION.md)
- [External backup checklist](docs/EXTERNAL_BACKUP_CHECKLIST.md)
- [Historical validation](VALIDATION.md)

## Wiring

| Connection | Assignment |
| --- | --- |
| Board / PHY | ESP32 WT32-ETH01 / onboard LAN8720A, address 1 |
| OLED | SH1106, 1.3-inch, 128 x 64, I2C |
| SDA / SCL | GPIO32 / GPIO33 |
| UP / DOWN / OK | GPIO36 / GPIO39 / GPIO5 |
| Each button | External 10 kOhm pull-up to 3.3 V; switch to GND; active LOW |
| Power LED | Hardware powered, no GPIO |
| MDC / MDIO | GPIO23 / GPIO18 |
| Oscillator enable | GPIO16 HIGH |
| RMII reference | External 50 MHz input on GPIO0 |
| Fixed ESP32 RMII | TX_EN21, TXD0 19, TXD1 22, RXD0 25, RXD1 26, CRS_DV27 |

Board settings remain in `main/board.h`: OLED address auto-probe (0x3C/0x3D,
requiring one ACK), orientation 0, column offset 2, I2C 400 kHz. Rendering uses
SH1106 page addressing and clears 132 RAM columns for 128 visible columns.
GPIO16 enables the onboard oscillator, not PHY reset. PHY reset GPIO is -1;
GPIO5 must never be driven as PHY reset. GPIO0 is released for normal boot.
Use a common ground and 3.3 V signals/pull-ups. GPIO36/39 lack internal pulls;
all button internal pulls remain disabled. GPIO5 is a strap: test boot with OK held.

Wiring references retained from the working checkpoint:
[WT32-ETH01 V1.4 schematic](https://github.com/egnor/wt32-eth01/blob/main/WT32-ETH01_V1.4.schematic.pdf)
and [manufacturer datasheet](https://files.seeedstudio.com/products/102991455/WT32-ETH01%20datasheet.pdf).

## Build and USB recovery

Follow LINUX_SETUP.md for the pinned SDK and a fresh build. Follow RESTORE_USB.md for saved recovery images. Full accepted configuration is sdkconfig.pinned; partitions.csv records the unchanged single factory layout.

## OLED and controls

The existing code-drawn three-second splash, input suppression, 30 ms debounce
and release gating are preserved. UP/DOWN move once per press outside editing. **Short OK selects
on release; holding OK for 500 ms goes Back once and suppresses short selection.**
Main-menu long OK does nothing. Information screens scroll full wrapped fields.

Menu: **Link & IP**, **Switch Info**, **Network Tests**, **Device Info**,
**Web Access**, **About**; six entries in four visible rows. Switch Info retains
its name, Status/details and separate LLDP/CDP records. The title bar separates
physical Ethernet (`EUP`, `EDN`, `E..`, `ERR`) and Web Access state.
Link & IP shows real link/speed/duplex/MAC, NO IP / DHCP / MANUAL IP and current tester IPv4
settings. Neighbor advertised addresses remain explicitly separate from tester IP.
Device Info/About read the firmware version from the application descriptor.

## Network Tests -> DHCP Test

OLED submenu: **Start**, **Status / Results**, **Stop / Release**, **Back**.
Start with link down reports **Connect Ethernet** and sends nothing. Otherwise
the standard Ethernet DHCP client starts on a dedicated worker. Results show
**Requesting address** and elapsed time for up to 30 seconds. Duplicate starts
are rejected during acquisition, a held lease or cleanup.

Success reports **Address acquired**, actual IPv4, subnet mask, gateway, supplied
DNS (or Not supplied) and acquisition duration. The standard client renews while
active. The lease remains for this session until Stop / Release, link loss,
lease loss or reboot. No lease/mode is persisted and reconnect never restarts DHCP.

**OLED Back while requesting cancels acquisition**, including short OK returning
from Results. After success, navigating away keeps the lease active. Browser
navigation does not cancel; use Stop / Release. Stop cancels a request or releases
a held lease, stops the client, clears Ethernet IPv4 and restores pre-test DNS. Cleanup never
restarts the Ethernet driver, changes discovery records or creates a link session.

Timeout reports **No lease within 30 s** and returns to passive mode.
Other distinct outcomes are Cancelled, Stopped / released, Link lost, Address
lost, Management subnet overlap and API failure. No-reply does not mean cable,
switch or internet failure, or prove that no DHCP server exists. Link/address loss requires a new explicit Start.
An offered subnet overlapping `192.168.4.1/24` (including broader containing
subnets) is released/rejected. Hotspot addressing is never changed. Cleanup
errors appear separately from the primary result, name the API/code and require
Stop / Release retry before another Start. Global resolver slots are saved and
temporarily cleared in TCP/IP context before acquisition, then restored after
stop; AP DHCP-server DNS and hotspot configuration are independent and untouched.
The rejected ESP-IDF zero-address DNS setter is not used for cleanup.

The worker serializes actions and samples live stack state every 50 ms; queued
IP events cannot grant a lease after cancellation. Link-generation and pending
cancellation checks guard publication. Receive callbacks stay short. The worker
closes the receive gate, drains in-flight callbacks and synchronizes stack stop.

## Network Tests -> Manual IP (5A)

YOUR IP edits four decimal octets. PREFIX defaults to /24 and supports /1–/30.
Gateway defaults to NONE; select NONE or edit its four octets (0.0.0.0 means NONE).
UP/DOWN saturate rather than wrap. Held adjustment begins after 450 ms, repeats
every 180 ms, then every 50 ms after 1,800 ms; repeats apply only inside editors.
Short OK advances the octet or accepts the prefix. Hold OK returns to the Manual
menu; retained drafts never change the active configuration.

REVIEW displays address/prefix and gateway, with APPLY, EDIT and BACK. Only APPLY
submits a configuration. Invalid host/network/broadcast addresses, invalid or
off-subnet gateways, prefixes outside /1–/30, and any subnet overlapping the AP
192.168.4.0/24 are rejected. Addresses and prefixes are explicit technician input;
LLDP/CDP/ARP do not supply settings. If DHCP is active/requesting, REVIEW explicitly
asks REPLACE DHCP? A changed lease or link generation rejects the old review token
and requires another review/APPLY. Canceling an editor or review leaves DHCP alone.
Once APPLY is accepted, the previous configuration is released; canceling probing
returns to NO IP rather than silently restoring an old lease.

The existing DHCP worker and netif are the sole addressing owner. They first
release/stop DHCP and clear the previous address/routes, then probe without an
assigned address. Manual mode never starts the DHCP client. USE DHCP releases
manual configuration and opens the existing DHCP menu; acquisition still requires
Start. Both modes clear on physical disconnect/reboot; reconnect never reapplies
a draft. An applied manual address survives normal menu navigation on that link.

Manual conflict protection follows [RFC 5227](https://www.rfc-editor.org/rfc/rfc5227.html):
initial randomized 0–1 s wait, three zero-sender ARP probes 1–2 s apart, a 2 s
final wait and two request-form announcements 2 s apart. A bounded RX observer
detects owners and competing probes until the first announcement. Later conflicting
sender claims immediately close RX/TX gates, invalidate the active result and show
ADDRESS IN USE; the worker removes the netif/address. It relinquishes rather than
defending. After ten conflicts, explicit attempts are limited to one per 60 s;
editing, DHCP and reconnect do not reset that counter (reboot does).

No reply does **not** guarantee an address is unused. Silent/filtering peers and
ARP spoofing remain limitations. This is untagged wired IPv4 only; it adds no ARP
discovery or Internet test. The owner subsequently reports 5A hardware validation
passed. This development session performs no new hardware tests; the historical
software-stage checklist remains in VALIDATION.md.

The RX observer borrows frames before stack handoff without allocation or an ARP
cache. Discovery still observes each frame; each original is consumed/freed once.
Manual stack ingress admits ARP only, including normal requests to which lwIP
responds once active. Late DHCP and all Ethernet TCP remain blocked. Startup stack
TX is gated to suppress automatic lwIP GARP until the controlled first announcement.
DNS borrow/restore remains on the TCP/IP thread and never changes AP server DNS.
Dashboard status shows actual addressing mode/prefix/gateway and keeps DHCP lease
fields empty during Manual mode. Manual configuration/USE DHCP are physical actions.

## Network Tests -> PING (5B)

PING offers **Ping gateway**, **Custom IP**, and **Back**. Without a usable wired
address it offers SET MANUAL IP, DHCP TEST and BACK; neither choice automatically
assigns an address. Custom IP reuses the same accelerated four-octet editor;
accept all four octets, then explicitly START. Target validation rejects the
tester's own address, zero/loopback/multicast/reserved targets, local network/
broadcast addresses and the management 192.168.4.0/24 subnet. A routed target
requires a valid configured Ethernet gateway; there is no route inference.

A normal session sends exactly four 16-byte-payload ICMP Echo requests, about one
second apart, with a one-second reply budget per request. Results show replies,
loss over requests accepted for transmission and average RTT over successful replies only.
RTT uses the matching Ethernet RX timestamp rather than the 50 ms worker polling
time. NO REPLY means no matching reply within those budgets; it does not diagnose
a device, cable, switch, firewall or Internet failure. AGAIN explicitly starts
a fresh session. Short OK/CANCEL or hold Back cancels a running session; Back
returns to the Ping menu. Cancellation/errors may stop before four transmissions.

The existing address worker also owns Ping socket I/O: no Ping task or SDK callback
task is created. One raw socket is bound to both the current Ethernet netif and
source IPv4 address. A binding failure never falls back to AP/default routing.
The owner's diagnostic reservation rejects DHCP/manual mutations until the socket
is retired; ordinary DHCP renewal, manual ARP monitoring and discovery continue.
Link loss, conflict, changed live IP/mask/gateway/interface or owner generation
aborts the session and invalidates stale results. The socket closes on all terminal
paths; an unexpected close failure keeps the one descriptor/reservation for retry.

Only matching Echo Replies enter the Ethernet stack during an outstanding request.
The bounded classifier checks IPv4/ICMP checksums, endpoints, type/code, lengths,
fragmentation, identifier, sequence and a fresh session cookie. Native raw-PCB
interface binding also filters socket reception. Ethernet TX checks the matching
request/generation, suppressing queued stale ARP-resolved requests after cancellation
or a new session. ARP and existing DHCP policy stay intact; TCP, unsolicited Echo
requests and hotspot-targeted Ethernet browser traffic remain blocked. There are
no browser Ping actions, DNS target lookup, Continuous Monitor or OTA features.

No reply does not establish a root cause. Native ARP resolution, packet loss,
driver/scheduler latency and ICMP filtering affect results; already transmitted
frames cannot be recalled. Host tests do not prove physical isolation or wire
timing. Ping hardware acceptance is **NOT RUN** in this session; use the 5B
procedure in VALIDATION.md.

## Web Access and dashboard

Browser access is OFF after every reset. Only physical **Web Access -> Enable**
starts the WPA2-Personal/AES hotspot and HTTP server. Returning/Back does not
change access state. Disable stops HTTP and its sessions and stops Wi-Fi clients;
both shutdowns are attempted independently and failed cleanup remains visible.
Ethernet/discovery and any explicit DHCP session continue through Web Access changes.

SSID `Syndetis-XXXX` derives from the SoftAP MAC. The intentionally retained
prototype password is **Syndetis**, in `main/access_config.h`: **change it for
your own builds before deployment/distribution**. Password is shown physically
on OLED, not routine logs or status JSON. Forget old saved credentials if needed.
Connect to the hotspot and open **http://192.168.4.1/**; remain connected if the
phone reports no internet. There is no dashboard login or HTTPS.

The AP remains `192.168.4.1/24`; its DHCP **server** is separate from Ethernet's
DHCP **client**. The working startup fix is retained: no AP IP setter/manual
DHCP override; SDK default event handlers manage its server lifecycle.
DHCP operations never restart Wi-Fi/HTTP or disconnect management clients.

All endpoints remain **management-hotspot only**. Ethernet ingress passes only
ARP and unfragmented UDP DHCP replies (67 -> 68) to the active IP stack. **All
Ethernet TCP is dropped**, including traffic addressed to the AP IP. Each HTTP
handler also verifies socket local address `192.168.4.1` and a management-subnet
peer. Ethernet dashboard access is deferred. There is no routing, bridging or NAT.

Routes: GET `/`, GET `/api/status`, POST `/api/dhcp/start`, POST `/api/dhcp/stop`.
POST requires exact Host `192.168.4.1`, Origin `http://192.168.4.1`, Content-Type
`application/json` and body `{}`. Wrong origins/hosts/types/bodies are rejected;
there is no CORS permission. Operations enqueue promptly (202); conflicts or
unavailable starts return 409. No browser operation enables Web Access.
All routes disappear with physical disable. OLED and browser share DHCP state/actions.

HTML/CSS/JS is embedded from `main/dashboard.html`, with no CDN/fonts/internet
requests. Polling is serialized, about once/second, with timeout, stale labeling
and recovery. Controls disable while stale or awaiting an action. Network text
uses JSON escaping and `textContent`. Status is bounded to 16,384 bytes, including
four worst-case neighbor records. DHCP results never reuse stale addresses as current.

## Passive Switch Info and limits

LLDP/CDP reception continues before, during and after DHCP, with Web Access OFF
or ON. All-multicast remains enabled on the existing ESP32 EMAC; promiscuous mode
is not enabled. The driver/PHY are never restarted by DHCP or web operations.
There are no transmitted discovery queries or advertisements. IPv6, AutoIP,
forwarding and saved DHCP leases are disabled; passive mode emits no IP traffic.

The single receive dispatcher borrows each frame for the existing bounded
four-frame discovery queue. It then transfers eligible DHCP/ARP buffers once
to the active stack, or frees the original. Stack receive errors also consume
the buffer. No shared owned buffer, duplicate free or netif glue replacement.
Real cable changes still clear neighbors and invalidate queued old-session frames.

Supported: LLDP Ethernet II (0x88CC), CDP v1/v2 over LLC/SNAP with checked checksum,
untagged or one 802.1Q tag for discovery, up to 1518 received bytes; 255-byte
identity/name/description values. LLDP chassis/port/TTL and optional system name,
port description, management IPv4/IPv6 and PVID; CDP device/port, address/management
TLVs and native VLAN. CDP address fallback is labeled Advertised IP, not asserted
management-only. Addresses prefer explicit management, then IPv4 before IPv6.
Control/non-ASCII bytes display as `?` with a note; raw identity keys stay intact.

Four total protocol-specific neighbor records; no LLDP/CDP merging or live-record
eviction. Updates replace optional fields; expired slots may be reused. Age/TTL,
withdrawal, queue/capacity/limited/invalid counters are shared by OLED/browser.
Link down clears identities immediately. A 30-second observation interval reports
No advertisement received and keeps listening; it never diagnoses a cable fault.

Unsupported: stacked tags/QinQ, jumbo, LLDP LLC/CDP Ethernet II, other discovery
protocols, LLDP-MED/DCBX/capabilities/platform/software, full VLAN lists, Unicode,
multiple displayed addresses and text over 255 bytes. Tagged Ethernet IPv4 is
not added. No saved static addressing, TCP probes, scanning, OTA or SD features.


## Continuing development

Run the declared tests using `python3 scripts/run_tests.py`. Verify packaged files with `python3 scripts/verify_checksums.py` and an independent build with `python3 scripts/verify_build.py`. Build outputs and downloaded components are regenerated. Firmware inputs match the accepted source after LF normalization.

Next: Milestone 6 — Wi-Fi and Ethernet OTA with physical authorization and an explicitly planned partition migration. Continuous Monitor remains deferred.
