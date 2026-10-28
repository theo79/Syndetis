> Historical signed-HTTPS design/evidence, superseded by the user-approved raw-bin HTTP updater. See README.md and M6C_MIGRATION_PLAN.md for the current implementation and validation.

# Milestone 6 implementation plan

Baseline: 260afc42e999845c2d670c11ce81e117920ff4ad, application 0.4.0.

## Readiness review

- app_main owns OLED/buttons; services worker starts Ethernet and optionally the WPA2 hotspot/HTTP dashboard. Wired DHCP/Manual/Ping share a single worker and explicit ownership gates. Discovery borrows every RX frame before the stack classifier.
- No OTA implementation exists. The selected SDK table is factory-only; the CSV mirrors it. NVS is 0x9000/0x6000, PHY 0xf000/0x1000. The proposed otadata at 0x10000 overlaps the old application and requires a separate controlled USB migration. Preserve NVS/PHY offsets. Keep the old recovery artifacts/checksum manifest untouched.
- Existing audit: 905664-byte app, 26176-byte bootloader, 4 MiB configured. Two 0x1e0000 OTA slots fit at 0x20000/0x200000; table remains 0x8000 only if the rollback-enabled bootloader fits below it. Measure before accepting layout.
- No /dev/serial/by-id, ttyUSB or ttyACM device was visible during inspection. Actual flash, silicon revision and security/eFuse state are UNKNOWN. No device reset/read/flash is attempted. Configuration cannot establish these facts.

## Security/access decisions

Use HTTPS on a dedicated, temporary update listener, with a provisioned device certificate/private key and release-signing public key read from NVS. No HTTP upload fallback, embedded shared password, signing private key, default trust key or irreversible secure boot change. Missing identity/trust fails closed. Technicians must trust the issuing CA and use an IP/hostname covered by the certificate; bypassing certificate warnings is not an acceptable production workflow.

A short physical OLED session provides a fresh random one-use authorization code. Same-origin, exact Host/interface checks, bounded attempts, absolute deadlines and exclusive writer ownership gate upload. Both interfaces call the same inactive-slot engine. A signed trailer wraps a normal ESP32 application image; the offline signing tool produces the browser-upload .bin. An ordinary unsigned build is deliberately rejected. This revision-independent application-layer trust does not claim hardware secure boot or resistance to physical flash/NVS replacement.

Ethernet TCP/443 is admitted only during the physical session and only to the current wired address, never the AP address. Existing port-80 dashboard remains hotspot-only. Use existing DHCP or Manual IPv4 addressing; for direct/no-DHCP links, explicitly configure Manual IPv4 and matching technician address before opening updates. No implicit fallback address or customer-network DHCP server.

## First coding step and anticipated changes

1. Add portable ota_engine.[ch]: bounded streamed image prefix/trailer, callback-backed inactive writer, signed metadata, product/chip/version validation, state/timeout/cancel/exclusivity. Add actual-C tests first.
2. Add ota_update.[ch] ESP-IDF adapter: NVS trust/TLS identity, PSA/Mbed TLS verification, async HTTP upload worker, lifecycle supervisor, runtime memory metrics, boot health confirmation.
3. Extend the existing wired owner with an explicit maintenance reservation and narrow TCP/443 RX/TX classification. Preserve manual conflict/link/generation cleanup and reject diagnostic mutations while reserved.
4. Add Settings > Firmware Update and a self-contained browser upload page. Keep OLED rendering off the flash/network worker. Extend UI/Node/Playwright tests.
5. Adopt validated custom dual-slot CSV and rollback settings; set application version 0.5.0. Update build dependencies, test runners, migration/recovery/security and hardware acceptance documentation.
6. Run host/Node/browser regressions, isolated EIM v6.1 build and size reports. Report software evidence separately from unperformed hardware acceptance.

No commits/pushes/tags/releases, hardware migration/flashing/erasing, security provisioning or eFuse changes. No unrelated optimization.

## Implementation refinement

The formerly reserved 128 KiB at 0x3e0000 is now a separate `ota_keys` NVS partition. This preserves existing default NVS/PHY and separates per-device TLS identity/release public trust from generic app artifacts. The new build and verify_build.py validate the exact six-partition layout and rollback bootloader fit. Hardware acceptance remains pending; see RESULTS.md.
