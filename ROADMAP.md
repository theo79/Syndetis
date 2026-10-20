# Syndetis roadmap

Milestone 1 (0.1.0): OLED, buttons, code-drawn 3-second splash, information navigation.
Hardware testing passed as reported by the owner.

Milestone 2 (0.2.0): physical Ethernet status without an Ethernet IP, physically
enabled management hotspot and offline read-only dashboard. The DHCP INIT/IP-setter
startup correction is retained. Historical build/hardware notes are in VALIDATION.

Milestone 3 (0.3.0): passive LLDP/CDP Switch Info independent of IP/Web Access;
bounded protocol-specific neighbors with TTL/expiry and link-session clearing;
hotspot password Syndetis; short OK on release and one Back on a 500 ms hold.
Implemented; build and software checks are recorded in VALIDATION. Hardware
acceptance remains pending beyond owner-reported LLDP reception with a Siemens
S7-1200 and CDP reception with a Cisco device.

Current menu: Link & IP, Switch Info, Device Info, Web Access, About. Five items
in four visible scrolling rows. Submenus retain explicit Back; long OK goes back
one level without changing Web Access state. The main menu ignores long OK.

Final planned menu:
1. Link & IP
2. Switch Info
3. Find Devices
4. Network Tests
5. Settings
6. Web Access
7. About

Add entries only when implemented. No fake results/Coming soon pages. Future
editors use explicit Apply/Cancel. Keep the existing three physical buttons.

After hardware verification, future work remains:
- Explicit DHCP or static Ethernet IPv4, with no silent fallback/mode switching.
- Active device discovery only when explicitly implemented, with source/evidence.
- DHCP test, gateway/custom IPv4 ping and TCP port check, with responsive UI and
  cancellation. Wi-Fi management must never route Ethernet diagnostic tests.
- Browser access through Ethernet IP when reachable. Retain flash-only assets,
  shared state/operations and explicit stale/disconnected status.

Critical DHCP rule: physical link and Layer 2 discovery work without any lease
or assigned IP. DHCP failure must never hide them. Hotspot DHCP server is for
management clients only; no Ethernet DHCP client is started in this milestone.

Physical access control applies to the whole HTTP server across interfaces.
OFF after reset; only physical Enable starts it. Disable closes listener and
sessions and stops hotspot clients. No dashboard login while enabled: future
reachable Ethernet LAN users will have access during that enabled period.

Milestone 3 limits: four records, four queued frames, 1518 received bytes,
255-byte text/identity fields, single 802.1Q only, selected IPv4/IPv6 address,
LLDP PVID/CDP native VLAN only. No QinQ/stacked tags/jumbo, other discovery
protocols, complete VLAN list, LLDP-MED/DCBX/capabilities/software/platform fields,
or Unicode rendering. Exact supported/unsupported input and semantics are in README.

Hardware gate (reception tested; remaining checks pending):
- [ ] Short select on release; 500 ms hold Back without first activating, once per hold.
- [ ] Back from Web Access preserves state; main-menu hold does nothing.
- [ ] New Syndetis password/OLED details work after forgetting old credentials.
- [ ] Known LLDP/CDP advertisements match switch configuration.
- [ ] Discovery works WEB OFF without Ethernet IP; OLED/browser agree when enabled.
- [ ] Unplug/new port clears old data; TTL/withdrawal and late advertisements behave truthfully.
- [ ] Waiting/no-advertisement after 30 seconds never claims no switch/cable failure.
- [ ] Existing link, splash, debounce, scrolling, physical web control and boot OFF regressions pass.

Do not add later milestones before device acceptance. No Ethernet IP configuration,
scanning, ping/TCP tests, SD or OTA is implemented in 0.3.0.
