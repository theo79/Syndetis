# Syndetis

**A compact Ethernet diagnostic tool for the field.**

**Plug in. Test the port. Understand what is happening.**

Syndetis is a handheld Ethernet troubleshooting project built for network technicians, IT engineers, field service teams, and people working around industrial Ethernet. It aims to answer the questions that matter when standing in front of a switch, cabinet, wall outlet, router, or PLC—without opening a laptop for every first check.

The idea is deliberately narrow: **useful network evidence, a small display, three buttons, and as little configuration as possible.** Syndetis is not intended to replace a packet analyzer or a full network tester.

> **Project status — October 2026:** Functional firmware prototype. Ethernet discovery, DHCP, temporary Manual IPv4, and four-request ICMP Ping have been exercised on hardware. The enclosure/productization and browser-based firmware-update work are still ahead. This is a development prototype, not a production-certified instrument.

## What Syndetis can do today

| Capability | What it provides | Status |
| --- | --- | --- |
| **Ethernet link check** | Link up/down, negotiated 10/100 Mbps speed and duplex | Implemented; field tested |
| **Passive device discovery** | Listens for LLDP and Cisco CDP; reports advertised device and port details when available | Implemented; field tested |
| **Neighbor details** | Device name, port ID, advertised management IPv4, and protocol-specific details such as CDP native VLAN | Implemented; field tested |
| **Explicit DHCP test** | Requests an address on demand; shows acquired IPv4, subnet, gateway, and DNS information, or a bounded failure/timeout result | Implemented; field tested |
| **Temporary Manual IPv4** | Configure an address, prefix, and optional gateway on a static-IP network without DHCP | Implemented; hardware tested |
| **IPv4 conflict awareness** | ARP probing/announcements, competing-probe detection, and address relinquishment on observed conflicts | Implemented; software and user-reported hardware checks |
| **ICMP Ping** | Four requests to a gateway or custom IPv4 target; replies, loss, and average successful-reply RTT | Implemented; hardware tested |
| **Local OLED interface** | Simple task-oriented menus operated by three physical buttons | Implemented; field tested |
| **Wi-Fi management hotspot** | Physically enabled local web access at `192.168.4.1`; diagnostic Ethernet browser access remains blocked | Implemented; regression tested |
| **USB firmware flashing** | Build and flash through the ESP-IDF serial workflow | Working |

**Not in the current firmware:** Continuous Monitor, Wi-Fi firmware upload, Ethernet firmware upload, and OTA rollback. These must not be confused with earlier experiments or recovered historical source code.

### A field-first workflow

1. **Connect** Syndetis to an Ethernet port.
2. **Check** whether the link is up and see the negotiated speed/duplex.
3. **Identify** an advertising neighbor using LLDP/CDP—even without an IPv4 lease.
4. **Test addressing** with DHCP, or explicitly enter a temporary Manual IPv4 configuration for static networks.
5. **Ping** the gateway or a known target and see a compact result.

Syndetis reports observations, not guessed diagnoses. For example, no DHCP lease does **not** prove a port is broken; an unanswered Ping does **not** prove a device is offline; and missing LLDP/CDP advertisements do **not** mean the network is empty.

## Real-world validation

The prototype has been tested against real network equipment, including **Cisco infrastructure** and **Siemens industrial Ethernet devices**.

Selected observations from October 2026 field sessions:

- Negotiated an active **100 Mbps full-duplex** Ethernet link.
- Received and decoded **Cisco CDP** from a Cisco IR1101, including device identity, physical port ID, advertised management address, and native VLAN.
- Received **Siemens LLDP** information in earlier field tests.
- Successfully acquired a DHCP lease on a known DHCP-enabled network in approximately **4.4 seconds**, then verified release/retry behavior.
- Found and repaired a real DHCP cleanup defect uncovered during field testing; the original error was caused by passing an invalid zero DNS address to an ESP-IDF network API.
- Subsequently hardware-tested **Manual IPv4** and **four-request Ping**.

The current 10/100 Ethernet hardware reports the speed **it negotiated**; it cannot determine the maximum capability of a gigabit switch port.

Original field photographs and validation records are maintained separately. Any photos added to a public README should first have customer hostnames, internal addresses, and other identifying information redacted.

## Prototype hardware

| Part | Current implementation |
| --- | --- |
| Controller | ESP32 on **WT32-ETH01** |
| Wired networking | **LAN8720**, 10/100 Ethernet |
| Display | **SH1106**, 128 × 64 monochrome OLED |
| Controls | Three buttons: **UP**, **DOWN**, **OK** |
| Firmware framework | Native **ESP-IDF v6.1** |
| Current flash layout | Single **1 MiB factory application partition** |

Short OK selects; holding OK goes back or cancels the active operation. The interface is designed to be understandable without a manual for routine tests.

The electronics are still prototype hardware. Final power integration, enclosure validation, and reliability work are separate from the functional firmware milestones.

## Design and engineering principles

- **Passive discovery stays passive.** Syndetis listens to LLDP/CDP traffic; it does not actively scan an entire switched network or advertise itself as a neighbor.
- **Network state has one owner.** DHCP and Manual IPv4 cannot control the wired interface at the same time. Stale results are rejected when connections or configurations change.
- **Manual IPv4 is temporary.** It requires deliberate application, is not silently persisted, and is cleared when the physical connection changes. ARP-based conflict checks reduce risk but cannot guarantee an unused address.
- **Ping stays Ping.** It uses ICMP, not an ARP response disguised as reachability. It can run with DHCP or an explicitly configured Manual IPv4 address.
- **The diagnostic Ethernet port is not a general web server.** The existing management interface belongs to the physically enabled Wi-Fi hotspot.
- **Keep it simple.** The product should answer field questions quickly rather than accumulate menus, graphs, scanners, and protocols.

## Building the firmware

The project targets **ESP-IDF v6.1** and the ESP32. Use the Espressif setup instructions for your operating system to install that release and activate its environment.

From the root of the standalone ESP-IDF project:

```bash
idf.py build
```

To flash over USB serial (replace the port with the one detected on your machine):

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

On Windows, the serial port may look like `COM4`. The WT32-ETH01 prototype may require an external USB-to-serial programmer and the appropriate boot-mode wiring. **Do not run `erase-flash` as a normal update procedure.**

**Linux migration:** The portable `Syndetis5C` source checkpoint is being prepared for the private GitHub repository. Once published, use the `syndetis-5c` branch and its `LINUX_SETUP.md` for the exact clone, toolchain, test, and recovery steps. The checkpoint name is a packaging label; **it does not mean Continuous Monitor was added**.

### Firmware and recovery status

The hardware-tested Milestone 5B application was reported as **905,792 bytes** within the current **1,048,576-byte** application partition. The `About` screen may still identify this development lineage as **0.4.0**; no release number should be inferred from the `Syndetis5C` folder name.

USB recovery artifacts and historical code archives are preserved separately. Earlier A–G firmware contained Ping and OTA experiments, but it belongs to a **different development lineage** and must not be merged into the current firmware without review.

## Roadmap

| Phase | Focus | State |
| --- | --- | --- |
| Core field diagnostics | Link, LLDP/CDP, DHCP, Manual IPv4, Ping, OLED/buttons | **Functional prototype validated** |
| Portable development checkpoint | Standalone `Syndetis5C` source, private GitHub branch, Linux reproducibility and recovery documentation | **Next checkpoint** |
| **Milestone 6 — firmware updates** | **Wi-Fi hotspot and Ethernet firmware upload**, physical authorization, authenticated update admission, safe inactive-slot flashing and rollback | **Planned** |
| Product engineering | Enclosure integration, practical USB-C power, field ergonomics, extended reliability testing | **Next phase** |

### Why OTA is a separate milestone

The current device uses a **single factory application partition**. Reliable application OTA with rollback needs a validated flash/partition migration, inactive application slots, and a tested recovery plan. The initial transition is expected to require USB programming.

The intended update experience is: connect by the explicitly enabled Wi-Fi hotspot **or** an explicitly enabled, tightly restricted Ethernet maintenance service; select an application image; approve locally on the device; validate the update; write the inactive slot; reboot safely. **Neither Wi-Fi nor Ethernet OTA is implemented yet.**

Continuous Monitor, packet capture, broad scanning, persistent traffic history, microSD logging, and desktop reporting are deliberately outside the present firmware scope.

## Why this project exists

A network technician in the field often needs a few reliable answers before reaching for a laptop:

**Is the port active? What device am I connected to? Did I get an address? Can I reach the target?**

Syndetis is an attempt to make those first answers fast, clear, and portable—using actual measurements instead of speculation.

**Syndetis is a working prototype and an ongoing engineering project.** The next challenge is turning validated firmware into a dependable handheld instrument.
