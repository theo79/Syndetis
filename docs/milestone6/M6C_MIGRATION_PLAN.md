# M6C — simplified HTTP OTA and initial USB migration

28 October 2026. **Software and migration artifacts: PASS / READY for final USB flash review. Hardware migration/OTA acceptance: PENDING.** Stop before writes and await explicit USB flash authorization. No production credentials, flash, reset, serial communication, erase, eFuse, provisioning or Git commands performed during this change.

This plan supersedes all previous signed-HTTPS/hostname/identity migration instructions. No signing keys, CA, TLS credentials, hosts file or identity image are required. Production generation remains cancelled.

## Final implementation

The smallest product change uses a dedicated temporary HTTP updater on 8080 alongside the existing AP-only port-80 dashboard. Wi-Fi URL `http://192.168.4.1:8080/update`; Ethernet URL `http://<actual-current-DHCP-or-Manual-IP>:8080/update`. No fixed wired IP or new discovery/DNS service. Existing OLED flow and one-use code retained. Correct Host is exact actual local IPv4:8080 and mutation Origin must match HTTP authority exactly. Wrong local interface/address/subnet, hostname aliases, port, origin, code, expired/replayed/concurrent upload remain blocked. Ethernet TCP/8080 is admitted only during physical maintenance ownership; port 80, port 443 and AP-address Ethernet access remain blocked. Address/link generation changes invalidate sessions. Listener closes after cancel/expiry/completion/reboot.

Upload is the raw ESP32 app .bin without a signature trailer. Engine retains prefix/product/chip/strict-newer-version/length checks and inactive-slot bounds. Appended image hash is required; esp_ota_end + esp_image_verify validate full checksum/hash, silicon compatibility and exact image length before selection. No running app, bootloader/table/NVS/PHY/reserved region is a browser write target. Cancellation, stalled/absolute deadlines, error cleanup, controlled reboot, bounded trial confirmation and rollback are preserved.

HTTP does not authenticate or encrypt firmware/code. Anyone able to intercept/alter traffic can potentially replace firmware and recompute its unkeyed hash. A checksum is not a signature; local filtering and the physical window do not remove this risk. This tradeoff was explicitly requested. No hardware security setting is changed.

## OLED requirement

Existing UI task renders Preparing update... before flash begins, Uploading XX% plus 124-pixel bar, Verifying..., Update complete/Rebooting..., or short failure/cancellation text. Accepted raw bytes/expected total produce percent, never 100 before complete reception. The existing 250 ms refresh cadence skips unchanged progress. No drawing from network/upload callbacks or flash critical sections. Existing engine mutex serializes whole OLED/I2C frames with OTA/boot-metadata writes; preparing and verifying phases wait for successful frame acknowledgement, failing after two seconds if OLED is unavailable. Upload task yields between chunks; no new tasks/queues/menus. Responsiveness and actual flash-write/I2C timing remain physical acceptance items.

## Software evidence

Actual project `/home/tanasto/Documents/Syndetis`; no new Git operation. Pinned SDK v6.1 commit fff9895c82d744c7237be8847347bdd1b07c6643 via existing EIM activation. Isolated build `build-m6-http-20261028`. Full runner includes eleven C suites (actual production HTTP adapter now runs without crypto fixtures), two legacy Python tooling suites, two Node suites and 18 browser cases. All PASS. Legacy real-Mbed-TLS signature tooling/discovery tests also PASS; these exercise temporary fixtures only and are no longer runtime update requirements.

Tests cover chunk boundaries, raw lengths, wrong product/chip/version/missing hash flag, oversize/incomplete/cancel/expiry/stall/I/O/validation/activation failures, no boot selection on rejection, replay/concurrency, two wired IPs, Host/Origin/interface/subnet/code rejection, network port gates, lifecycle/rollback, notice acknowledgment before flash, failure before flash without OLED acknowledgment, percent0/99/100, bar0/118/120, verifying/ready/error/cancel and redraw suppression. Browser tests use both actual-style HTTP origins on 8080 across phone/laptop; traffic is intercepted, no real hardware acceptance inferred.

Build, size, size-components and verify_build.py all PASS; effective Kconfig equals sdkconfig.pinned, ESP32/4 MiB, rollback enabled, hardware Secure Boot/encryption disabled, six exact nonoverlapping partitions and image fits validated. Existing config's unused HTTPS component settings retained to avoid unnecessary Kconfig churn; updater calls only HTTP APIs. Signature implementation is no longer in application component sources. Actual built image checksum/hash validates with offline esptool; independent recomputation detects changed header/payload/checksum/hash and validates exact file length.

## Device and backup evidence

Newer user-supplied device observations: ESP32-D0WD rev1.1, 4 MiB; Secure Boot and flash encryption disabled, ROM reads accessible, download encrypt/decrypt/cache restrictions false. No new device operation was run. Reconfirm same physical unit/security state before executing a later authorized write.

Both unchanged private full readbacks remain 4194304 bytes under `tmp/m6b-device-backup/esp32-full-read1.bin` and read2, identical by byte comparison and SHA-256 `9efa40afd12a4ffead78d31649a9bbb093a6337ad80a023ae7877bc2ec6d010c`. Rehashed during this preparation. They contain credentials/configuration; do not publish. Encrypted external copy is recommended and not independently confirmed.

Offline extraction from verified backup remains at `tmp/m6c-preparation-20261028/recovery-extracted/manifest.json`: old bootloader/table/factory app/NVS/PHY. Installed old app metadata is Syndetis 0.4.0, factory at 0x10000, not OTA-capable. Packaged recovery images differ from actual device readback; use full verified device backup for exact restoration. Historical recovery files were not modified.

## Exact layout and artifacts

Keep NVS 0x9000/0x6000 and PHY 0xF000/0x1000 unchanged. Write matching bootloader at 0x1000, table at 0x8000, initial otadata at 0x10000, raw v0.5.0 at 0x20000 (OTA0). OTA1 at 0x200000/0x1E0000 remains inactive/unwritten; verified old backup shows erased bytes there. ota_keys 0x3E0000/0x20000 is now unused/reserved and must remain untouched. No full erase or identity write.

Initial otadata is 8192 bytes 0xFF. With no factory entry, pinned bootloader selects OTA0. Old factory app is overwritten by otadata/new app; table-only migration is unsafe. The first USB image has no previous valid OTA slot and cannot rely on automatic rollback to0.4.0. Preserve accessible ROM recovery. The separate first signed-HTTPS artifact directories are obsolete for this migration and were not overwritten.

Staging directory: `/home/tanasto/Documents/Syndetis/tmp/m6-http-migration-20261028/`. Immutable files, `manifest.json` and `SHA256SUMS.txt` prepared from the validated build. Firmware version and binary size follow:

```json
{
  "target": "esp32",
  "application_version": "0.5.0",
  "application_bytes": 934032,
  "application_partition_bytes": 1966080,
  "free_bytes": 1032048,
  "upload_bytes": 934032,
  "credential_provisioning_required": false,
  "update_transport": "HTTP:8080",
  "bootloader_bytes": 26240,
  "bootloader_free_bytes": 2432,
  "sha256": "17d9d78e57f5b58fba3face8160d6d03e030f840a03fe024ee13837505269c58",
  "effective_config_identical": true,
  "partition_layout_verified": true,
  "rollback_enabled": true,
  "hardware_validated": false
}
```

| File | Offset | Bytes | SHA-256 |
|---|---:|---:|---|
| bootloader.bin | 0x1000 | 26240 | `4d737be2ee0f644f3599fa478ea58017b481fb0b04cd14abc43b452953aae326` |
| partition-table.bin | 0x8000 | 3072 | `4b1bbf2c902279fac6140aba1fe6d2c11b9867168020ff18e3ddd824bc952ca0` |
| ota_data_initial.bin | 0x10000 | 8192 | `7d2c7ac4888bfd75cd5f56e8d61f69595121183afc81556c876732fd3782c62f` |
| Syndetis-v0.5.0.bin | 0x20000 | 934032 | `17d9d78e57f5b58fba3face8160d6d03e030f840a03fe024ee13837505269c58` |

Compared with last signed-HTTPS v0.5.0 (998112 bytes), app shrinks to 934032 (-64080). Static DRAM 78895 (-7952), IRAM 90339 (unchanged), flash code 663722 (-55420), flash data 161560 (-8664). Bootloader 26240 unchanged, margin 2432; free app slot 1032048. Runtime stack/heap/timing are not measured by static size reports.

## Exact USB sequence — NOT EXECUTED; final authorization required

Confirm known unit, stable power and 3.3 V UART TX→RX0/RX→TX0/common ground. Manually hold GPIO0 LOW during reset/power-up for ROM, then release GPIO0 without resetting out of ROM. Confirm port permissions/no competing process. These future commands perform persistent writes and must not run before explicit final authorization:

```bash
set -e
cd /home/tanasto/Documents/Syndetis
. /home/tanasto/.espressif/tools/activate_idf_v6.1.sh
M6_HTTP_PORT=/dev/serial/by-id/usb-1a86_USB_UART-LPT-if00-port0
M6_HTTP_IMAGES=/home/tanasto/Documents/Syndetis/tmp/m6-http-migration-20261028
(cd "$M6_HTTP_IMAGES" && sha256sum -c SHA256SUMS.txt)
python -m esptool --chip esp32 --port "$M6_HTTP_PORT" --baud 115200 \
  --before no-reset --after no-reset --connect-attempts 1 \
  write-flash --flash-mode dio --flash-size 4MB --flash-freq 40m \
  0x1000 "$M6_HTTP_IMAGES/bootloader.bin" \
  0x8000 "$M6_HTTP_IMAGES/partition-table.bin" \
  0x10000 "$M6_HTTP_IMAGES/ota_data_initial.bin" \
  0x20000 "$M6_HTTP_IMAGES/Syndetis-v0.5.0.bin"
```

No generic idf.py flash, force/security overrides, erase-flash or identity command. Four pairs exactly; sector ranges exclude NVS/PHY/reserved identity region. Stop on any error, preserve ROM access, require esptool verification and reviewed readback before normal reset. After writes have been separately authorized and verified: release GPIO0, reset normally, confirm Device Info/dashboard 0.5.0, diagnostics and no 8080 listener before physical session. Establish the first real session on phone/laptop and verify OLED preparation/progress/verification/reboot.

## Physical OTA acceptance after separately approved migration

Use a strictly newer raw application (0.5.1 for Wi-Fi then 0.5.2 for Ethernet), produced by a future reviewed version-only release build. No such releases were built here; same-version 0.5.0 must reject. Connect hotspot Wi-Fi or same-subnet Ethernet using existing DHCP/Manual IPv4, open physical session, enter temporary OLED code, upload raw application bin, observe progress/error/reboot, verify actual new version/slot and trial VALID confirmation, and repeat diagnostics/isolation tests. HTTP port 8080 closes outside the window; ordinary dashboard port 80 remains Ethernet-blocked throughout.

Test wrong product/chip/version, malformed/corrupt/truncated/oversize images, wrong code/host/origin, replay, interruption/disconnect/cancel/stall/expiry and missing OLED before flash; retain original active slot on rejection. Valid newly constructed unsigned images are intentionally accepted if their other checks pass. No signature acceptance claim remains. Trial failure and power cuts need separate bench authorization once a confirmed previous OTA slot exists.

## Recovery — separate write authorization required

Before recovery, recheck original 4 MiB backup hash, stable power/known disabled security state and manual ROM entry. Future exact full-baseline restoration command (never run here):

```bash
python -m esptool --chip esp32 \
  --port /dev/serial/by-id/usb-1a86_USB_UART-LPT-if00-port0 --baud 115200 \
  --before no-reset --after no-reset --connect-attempts 1 \
  write-flash --flash-mode keep --flash-size keep --flash-freq keep \
  0x0 /home/tanasto/Documents/Syndetis/tmp/m6b-device-backup/esp32-full-read1.bin
```

This restores old factory layout and all old device settings, overwriting later changes. Verify full readback against original SHA-256 before releasing GPIO0/resetting and confirming 0.4.0 diagnostics. Do not mix old/new app offsets or tables. Never erase the chip/eFuses as a troubleshooting step. Keep emergency serial accessible after enclosure assembly.

## Remaining boundary and evidence

No credential-generation/provisioning/hostname blocker remains. Final steps: review four-image hashes and hardware/backup readiness, grant explicit USB migration write authorization, then perform real hardware acceptance. Software PASS does not establish power-cut safety, flash timing, runtime memory or functional hardware OTA PASS.

Logs: `/tmp/m6-http-final-tests.log`, `/tmp/m6-http-legacy-crypto-tests.log`, `/tmp/m6-http-final-build.log`, `/tmp/m6-http-final-verify.log`, `/tmp/m6-http-final-size.log`, `/tmp/m6-http-final-components.log`. Production credential request was cancelled; no secrets directory/image was created or used. Historical documentation retained as historical evidence.
