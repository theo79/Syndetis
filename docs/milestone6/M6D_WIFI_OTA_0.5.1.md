# M6D — first real Wi-Fi OTA release v0.5.1

Prepared 28 October 2026. **Software/release preparation: PASS. v0.5.1 physical Wi-Fi browser OTA: user-confirmed SUCCESS on 28 October 2026. Detailed trial-state/retained-slot readback and destructive rollback tests remain PENDING.** User reports v0.5.0 successfully booted after USB migration; no serial, USB, network upload or device reset was performed during this task.

## Release artifact

- Version: **0.5.1**.
- Binary: `/home/tanasto/Documents/Syndetis/tmp/releases/v0.5.1/Syndetis-v0.5.1.bin`.
- Size: **933,264 bytes**.
- OTA slot: **1,966,080 bytes**, free **1,032,816 bytes (52.53%)**.
- SHA-256: `4363cff3d3de487a2040b2f4a21777fe5254a1482be437fbb463367e22f1b044`.
- Checksum/manifest: `/home/tanasto/Documents/Syndetis/tmp/releases/v0.5.1/SHA256SUMS.txt` and `/home/tanasto/Documents/Syndetis/tmp/releases/v0.5.1/manifest.json`.

Raw ESP32 application only: no signing trailer, keys, certificate, identity image, bootloader, table, otadata or NVS in the staged release. ESP-IDF target/product/version, exact image length, checksum and appended SHA-256 verified offline. The v0.5.0 raw-image engine from the unchanged pre-edit source accepted the complete new application in an offline compatibility test using its old code workflow. New and old partition-table bytes are equal; Kconfig/layout unchanged. Generated SDK build by-products are not release inputs and must not be USB-flashed. No idf.py flash or esptool device command is part of this procedure.

## One-time code removal — COMPLETE in v0.5.1

No random code generation, fields/attempt counters, code validator, custom code header, browser entry, OLED code text or code-specific errors remain in the active update workflow. Physical Settings → Firmware Update remains the only way to open an update session; page/status/upload/cancel still validate actual socket interface/subnet, exact Host and mutation Origin. Three-minute session start window, ten-minute upload absolute deadline, ten-second stall deadline, one active writer and same-peer status/cancel while receiving remain. Browser replay/concurrency and same/older version reject. No replacement authentication added.

HTTP is neither encrypted nor firmware-authenticated. During the physical window, a permitted local peer can initiate an update; there is no additional secret. This is the requested workflow. Existing Wi-Fi network access settings remain unchanged.

OLED progress and synchronization retained: successful frame acknowledgments for Preparing update... and Verifying... before corresponding flash work; accepted-byte percentage/bar (not 100 before complete reception); Update complete/Rebooting..., short errors; existing UI task and shared flash/OLED mutex, redraw throttling, no new tasks/queues/menus. Inactive-slot validation, boot selection, bounded trial health and rollback unchanged.

## Verification — PASS

Pinned ESP-IDF v6.1 commit fff9895c82d744c7237be8847347bdd1b07c6643; fresh build `/home/tanasto/Documents/Syndetis/build-m6d-051-20261028`. Complete runner including eleven C suites, two Python legacy-tooling suites, two Node suites, two dashboard Chromium viewports and sixteen Wi-Fi/Ethernet update Chromium scenarios passes. Physical request-session API is exercised without any code, including duplicate request rejection. Browser tests assert no code input and no code header. Tests retain unauthorized/expired/interface/subnet/Host/Origin/replay/concurrent/length/product/chip/version/stall/cancel/disconnect/final-validation/activation failure, OLED notice/progress/error/completion and trial-confirmation/rollback cases. No hardware PASS follows from mocks.

Build, verify_build.py, size and size-components: PASS. App decreased by 768 bytes versus 934032-byte v0.5.0; static DRAM 78855 (-40), IRAM 90339 (unchanged), flash code 663458 (-264), flash data 161064 (-496). Runtime timing/memory remain physical test items.

Evidence: `/tmp/m6d-final-tests.log`, `/tmp/m6d-build.log`, `/tmp/m6d-verify.log`, `/tmp/m6d-size.log`, `/tmp/m6d-components.log`. Source comparisons confirm no partition/Kconfig/progress/synchronization changes beyond removal of the code display/fields. No Git commit/push performed.

## Exact first Wi-Fi upload procedure — performed by the user

1. Keep stable device power and accessible recovery wiring. On the **running v0.5.0**, select **Settings → Firmware Update** using its existing three-button menu. Note the hotspot connection details and **the v0.5.0 one-time code**. The code-free behavior is not active yet.
2. Connect laptop/phone to the Syndetis hotspot. If the client warns there is no Internet, stay connected and disable any automatic switch away from this Wi-Fi for the test. No router/Internet/hosts file/certificate setup is needed.
3. Open **http://192.168.4.1:8080/update**. The page is served by v0.5.0 and is expected to show its code field. Refresh if necessary; do not use a stale page from another device/session.
4. On the laptop select **`Syndetis-v0.5.1.bin`** from `/home/tanasto/Documents/Syndetis/tmp/releases/v0.5.1`. For a phone, transfer this exact raw binary to the phone first and verify its checksum through a trusted file-transfer/checksum workflow. Do not select any bootloader/table/merged image, previous signed package or old version. Laptop checksum command: `sha256sum /home/tanasto/Documents/Syndetis/tmp/releases/v0.5.1/Syndetis-v0.5.1.bin`; require the SHA-256 above.
5. Enter the code displayed by the **running v0.5.0**, then select **Upload and update** before its session expires. If it expires, reopen physically and use the new v0.5.0 code. Removing the code in the candidate firmware cannot remove it from the updater that is currently handling this first upload.
6. Watch OLED **Preparing update... → Uploading XX%/bar → Verifying... → Update complete / Rebooting...**. Record percentages, interruptions/errors, browser result and elapsed time. Keep power connected. Do not reset during write/verification/trial. Browser disconnection after a verified commit can accompany a successful reboot; inspect the actual device version.
7. After reboot check **Device Info shows 0.5.1**. The hotspot/update listener may be off normally; that is expected. To check code-free behavior, physically open **Settings → Firmware Update** on the now-running v0.5.1, reconnect and reload `/update`: OLED says update mode active and the browser has **no code field**. Do not upload 0.5.1 again; same-version must reject. Cancel this inspection session normally and confirm the temporary service closes.
8. Let the device run normally for at least 45 seconds and check there is no rollback/reboot/error. Trial software requires >=3 seconds, >=8 good UI frames, NVS/task/discovery readiness and has a 30-second failure bound. If comfortable, after that interval perform a normal user-controlled restart and verify 0.5.1 again; this provides behavioral evidence that the trial was accepted. Do not deliberately interrupt the trial to prove rollback during this first test.
9. The initial v0.5.0 USB plan places it in OTA0; this OTA is expected to write inactive OTA1 and retain OTA0. Code and host tests enforce this, but Device Info alone does not prove otadata VALID state or byte-for-byte retention of the previous slot. No current UI field exposes those facts. Record retained-slot/boot-state verification as pending unless supported by actual hardware evidence; do not claim a successful physical rollback test from a normal boot. Any additional readback/serial inspection must be separately scoped; none is run here.
10. Confirm normal Ethernet link/discovery and existing DHCP/Manual IPv4/Ping behavior after reboot. Report actual version, OLED/browser observations, reboot outcome and any errors before preparing a strictly newer Ethernet test release (e.g. 0.5.2). No 0.5.2 image was built in this task.

## Boundaries / remaining blockers

The user confirms the prepared v0.5.1 image successfully updated the physical ESP32 through http://192.168.4.1:8080/update. This establishes the first real Wi-Fi OTA success; it does not establish Ethernet OTA, measured resource margins, independent otadata/retained-slot readback or destructive rollback acceptance. Next firmware milestone is a strictly newer v0.5.2 Ethernet OTA test. Do not perform USB writes, partition/otadata/NVS changes, eFuse/security operations, credential provisioning or automatic network upload. Stop after artifact/procedure preparation.

## Hardware checkpoint update — 28 October 2026

The user confirmed successful physical Wi-Fi browser OTA with the prepared v0.5.1 artifact above. This observation is attributed to the user; the earlier artifact-preparation session did not initiate upload. Ethernet OTA physical acceptance is still pending. Keep detailed trial/rollback and memory/power-cut findings separate from this successful normal update.
