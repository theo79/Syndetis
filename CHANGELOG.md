# Changelog

## v0.7.0 — November 27, 2026

- Added OLED-initiated PROFINET DCP Identify All discovery on the local Ethernet segment. Discovery works without an IPv4 address on the tester's wired interface. Passive LLDP/CDP remains available under Switch Info.
- Added PROFINET scan progress, station inventory, advertised properties, potential duplicate indicators, and historical results after link loss. The Wi-Fi dashboard shows discovery results and counters.
- Added PROFINET scan state and counters to JSON diagnostic reports. The per-device inventory requires the existing network identifier opt-in.
- Recorded the project owner's Siemens SIMATIC S7-1200 field acceptance and packaged the tested OTA image.

[Release manifest](docs/v0.7.0-release.md) · [Field acceptance](docs/v0.7.0-field-acceptance.md)

## v0.6.0 — November 26, 2026

- Added browser JSON diagnostic reports with optional network identifiers and snapshot freshness information.
- Added the OLED Diagnostic Report screen to guide users to the dashboard.

[v0.6.0 release](https://github.com/theo79/Syndetis/releases/tag/v0.6.0)
