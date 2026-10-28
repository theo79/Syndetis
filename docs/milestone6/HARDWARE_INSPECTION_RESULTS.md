# M6B hardware inspection and backup — BLOCKED

Inspection date: 28 October 2026 (Europe/Athens).

## Verdict

Hardware inspection: **BLOCKED** before serial communication.
Complete flash backup: **BLOCKED / NOT PERFORMED**.
Migration readiness: **NOT ESTABLISHED**. Do not install v0.5.0.

## Environment and connection

Repository: `/home/tanasto/Documents/Syndetis`.
Host: Ubuntu/Kubuntu 26.04.1 LTS, x86_64 Linux.
Activated existing EIM environment with `. /home/tanasto/.espressif/tools/activate_idf_v6.1.sh`.
Verified ESP-IDF v6.1, Python 3.14.4, esptool/espefuse 5.6.0.
Installed command help verified for esptool global options/read-flash and espefuse summary. No device command executed.

Sandbox did not expose the adapter. Host-level read-only filesystem checks confirmed:

```text
/dev/serial/by-id/usb-1a86_USB_UART-LPT-if00-port0 -> ../../ttyUSB0
/dev/ttyUSB0: character device, root:dialout, mode 0660
readlink -e: /dev/ttyUSB0
fuser -v /dev/ttyUSB0: no owner reported, exit 1
current user: tanasto (uid 1000), no dialout membership
read/write accessibility test: PORT_ACCESS_DENIED
```

`ls`, `readlink` and `stat` succeeded; the combined ownership check returned 1 because fuser found no owner. The explicit permission check completed successfully and reported denied access. No serial port was opened and no reset, boot-mode transition, RAM loader or persistent device operation occurred. Adapter enumeration alone does not establish connection to the expected ESP32.

## Intended first hardware command — NOT EXECUTED

```bash
python -m esptool --chip esp32 \
  --port /dev/serial/by-id/usb-1a86_USB_UART-LPT-if00-port0 \
  --baud 115200 --before default-reset --after no-reset \
  --no-stub --connect-attempts 1 flash-id
```

This requests ROM-only read identification with a single connection attempt; reset/download entry is authorized, but unavailable port permissions block execution. No flash-write, erase, eFuse burn/protection or security-configuration operation is included. Later security reads and exact full-read commands must be reviewed against actual chip identification and read permissions before use. Do not use an unrestricted eFuse dump: selected security fields suffice and key material must stay private.

## Hardware observations and required evidence

| Required observation | Result |
|---|---|
| ESP32 model and silicon revision | UNKNOWN; chip not contacted |
| Base MAC | NOT READ |
| JEDEC manufacturer/device ID | NOT READ |
| Physical flash capacity / voltage | UNKNOWN |
| Secure Boot / flash encryption | UNKNOWN on device |
| Download restrictions / secure version / eFuse state | NOT READ |
| ROM recovery and trustworthy read access | NOT DEMONSTRATED |
| Actual partition table offset and installed layout | NOT READ |
| Installed bootloader/application and version | NOT IDENTIFIED |
| NVS / PHY offsets | NOT READ from device |
| Complete backup location / bytes / SHA-256 | NONE; no backup generated |
| Independent second read and comparison | NOT PERFORMED |
| Extracted recovery artifacts / hashes | NONE |

Repository sdkconfig and historical recovery files do not establish the physical unit's configuration. No hardware acceptance checkbox was completed.

## Migration comparison and outstanding prerequisites

Planned v0.5.0 layout: nvs 0x9000/0x6000; phy_init 0xF000/0x1000; otadata 0x10000/0x2000; ota_0 0x20000/0x1E0000; ota_1 0x200000/0x1E0000; ota_keys 0x3E0000/0x20000. Compatibility with this unit remains unverified. This layout overlaps the historical factory application region; no table-only migration is safe.

After serial permissions are resolved, establish chip identity, security state and consistent capacity before reading the complete detected address space twice into unique private files under ignored tmp/m6b-device-backup/. Both successful read commands, exact sizes, matching SHA-256 and byte-for-byte comparison are mandatory for VERIFIED status. Stop on unstable communication, unexpected hardware, restricted read access, inconsistent capacity or differing reads. Extract original table/bootloader/app/NVS/PHY offline only from a verified image. Do not overwrite packaged recovery artifacts.

RESTORE_USB.md requires 3.3 V UART logic, adapter TX to RX0, RX to TX0, common ground and adequate stable board power. If automatic boot entry is not wired, hold GPIO0 LOW during reset/power-up for ROM mode. GPIO16 is oscillator enable, not PHY reset. Physical wiring, power and recovery access remain unverified. A backup alone would not prove recoverability.

Before a future separately authorized migration: verified full and extracted backups, confirmed >=4 MiB target and compatible security state, stable power and working ROM recovery, reviewed matching bootloader/table/otadata/raw app images and hashes, offline release-signing trust, unique device TLS identity, trusted issuing CA and correct AP/wired IP SAN coverage are required. No production credentials generated or accessed. Actual TLS/OTA, runtime resources, diagnostics, rollback and power-cut acceptance remain pending.

## Next action

Have a host administrator grant tanasto access through the existing dialout group (typically `sudo usermod -aG dialout tanasto`), then start a fresh login/app session and recheck access. This report did not change group membership, device permissions or invoke sudo. Avoid world-writable port permissions or running the entire development session as root. Resume the authorized read-only inspection only after access is available; no firmware installation is authorized.

No firmware source, build configuration, Git history, existing recovery artifacts or acceptance checklist changed. Stopped at the serial-permission blocker.

## Resume attempt — 28 October 2026

Serial permissions resolved: current process has dialout membership; stable adapter resolves to /dev/ttyUSB0, root:dialout 0660, read/write access confirmed. fuser reported no owner. This supersedes the earlier permission blocker.

After displaying the exact command, executed once:

```bash
python -m esptool --chip esp32 \
  --port /dev/serial/by-id/usb-1a86_USB_UART-LPT-if00-port0 \
  --baud 115200 --before default-reset --after no-reset \
  --no-stub --connect-attempts 1 flash-id
```

Sanitized output:

```text
esptool v5.6.0
Serial port /dev/serial/by-id/usb-1a86_USB_UART-LPT-if00-port0:
Connecting........
ERROR: A fatal error occurred: Failed to connect to ESP32: No serial data received.
```

Device command exit code: **2**. Private raw log: /tmp/m6b-flash-id-private.log (created under umask 077).

Serial port was opened and automatic reset/download-entry was attempted; actual ROM entry was NOT established. No chip identity, MAC, flash identification or security state was obtained. No RAM stub was requested and no flash/eFuse write, erase, provision or security change command ran. No backup or extraction exists. No additional device commands or connection retries were attempted after failure.

Current verdict: hardware inspection **BLOCKED** by no serial response; backup **BLOCKED / NOT PERFORMED**; migration readiness **NOT ESTABLISHED**. Acceptance checklist remains unchanged.

Required next step is physical verification of board power and UART wiring: adequate power, 3.3 V UART logic, common ground, adapter TX to RX0 and RX to TX0. If auto-reset wiring is unavailable, hold GPIO0 LOW during board reset/power-up for ROM download mode as documented in RESTORE_USB.md. Await user confirmation of physical readiness before another single controlled identification attempt. Do not infer correct ESP32 connection from USB adapter visibility. Device may require manual reset to resume normal operation; do not claim its current boot state is known.

## Second controlled identification attempt — 28 October 2026

User requested continuation after physical-readiness instructions. Repeated the same displayed ROM-only flash-id command once, with --before default-reset --after no-reset --no-stub --connect-attempts 1 at 115200 baud on the stable authorized adapter.

Exit code: **2**. Result: **Failed to connect to ESP32: No serial data received.** Private log: /tmp/m6b-flash-id-attempt2-private.log (umask 077). No chip identification, security reads, flash backups or persistent writes occurred. Stopped after this attempt; no further retries performed.

Inspection and backup remain **BLOCKED**. Physical UART/power/download-mode entry is not demonstrated. Before a future retry, explicitly establish manual ROM mode (GPIO0 low during board reset/power-up), verify 3.3 V UART TX/RX/common ground and stable board power, and confirm whether DTR/RTS auto-reset is actually wired. If manual ROM mode is established, review using --before no-reset to preserve it instead of repeating automatic reset. No source, configuration, recovery files or acceptance checklist changed.
