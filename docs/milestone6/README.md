# Milestone 6 — local browser firmware updates (v0.5.1)

Hardware checkpoint: user-confirmed successful v0.5.1 Wi-Fi browser OTA on 28 October 2026. Ethernet OTA physical acceptance is pending; v0.5.2 is next.

Current architecture, 28 October 2026: physical OLED authorization, temporary HTTP port 8080 and raw ESP32 application .bin uploads over Wi-Fi or Ethernet. No firmware-signing requirement, CA, certificate, passphrase, hostname mapping or device identity provisioning. Earlier signed-HTTPS plans/results are historical and do not describe this build. Production credential generation was cancelled; no production credentials were created.

HTTP neither encrypts traffic nor authenticates firmware. A local attacker able to observe/modify the session can replace firmware and recompute its unkeyed checksum/hash. The physical window and network filtering limit access but do not prevent that attack. Use a controlled local connection and firmware obtained from a trusted source. This is the explicit user-approved tradeoff; no Secure Boot, flash encryption or eFuse changes.

## Ordinary technician workflow after approved initial USB migration

1. For wired updating, acquire DHCP or deliberately select Manual IPv4 using existing diagnostics; use a same-subnet laptop. No fixed IP or customer DNS change. Stop competing active diagnostics.
2. Select Settings → Firmware Update using the three-button OLED UI. A three-minute window opens without a code; hotspot starts if needed. No updater is listening after normal boot until this physical action.
3. Connect to hotspot and open `http://192.168.4.1:8080/update`, or use `http://<current-wired-IP>:8080/update`. The ordinary dashboard remains separate on hotspot port 80 and blocked from Ethernet. No router forwarding, public service, certificates or hosts-file setup.
4. Choose the raw `Syndetis.bin` compiled for a strictly newer version. Do not use a bootloader/table, merged image, old signed-trailer package, same/older version or wrong target/product. The updater streams only to an inactive OTA app partition and validates ESP32 image format, revision, checksum/hash and exact length before boot selection. Hashes detect corruption, not malicious authorship.
5. OLED shows Preparing update..., Uploading XX% with a bar, Verifying..., Update complete and Rebooting.... Failed/cancelled operations show a short reason; browser retains full response text. Upload has a ten-minute absolute deadline and ten-second stall deadline. concurrent/replayed upload is rejected.
6. Hold OK/use Cancel before commit to abort. Once ready to reboot, boot selection is committed. After reboot verify actual Device Info/dashboard version and diagnostics; do not assume a disconnected browser means failure. Update listener and authorization close on completion, cancellation, expiry/reboot.

No one-time code or replacement authentication exists in v0.5.1; physical menu activation is required. Existing Wi-Fi access settings are unchanged. For the first update from running v0.5.0, use its existing code. See [M6D release and first Wi-Fi test procedure](M6D_WIFI_OTA_0.5.1.md).

## OLED/flash synchronization and rollback

All drawing stays in the existing main UI task. Its full render/I2C frame shares the engine mutex with OTA begin/write/finalize/activation and boot-metadata operations. The main task tries this gate without blocking; buttons still poll. Upload task publishes Preparing/Verifying notices and waits up to two seconds for the existing successful-frame callback to acknowledge them, otherwise fails before proceeding. It never draws and yields between chunks. No new task/queue/menu was introduced.

Percentage is floor(received*100/total), clamped to 0..100, with zero-total ->0. Received counts actual accepted raw image bytes; no signed trailer exists. The bar reaches 100 only once all expected bytes are accepted. UI refresh is bounded by the existing 250 ms interval and suppresses unchanged progress state/percentage/message; input-triggered redraw remains responsive. Runtime latency/stack/heap margins require bench acceptance.

Trial health remains bounded: after at least three seconds and eight good UI frames, NVS/task/discovery readiness confirms; failure by thirty seconds triggers rollback. Cable/DHCP/client/gateway availability is not a success prerequisite. Previous OTA slot is retained. Initial USB migration has no previous valid OTA slot and cannot roll back to the old factory layout.

## Layout and validation

NVS 0x9000/0x6000; PHY 0xF000/0x1000; otadata 0x10000/0x2000; OTA0 0x20000/0x1E0000; OTA1 0x200000/0x1E0000. Table at 0x8000, bootloader at 0x1000. Former ota_keys at 0x3E0000/0x20000 is reserved/unused and left untouched. No identity image is written or read. Keeping its entry avoids unrelated partition churn.

The runtime links no ota_signature.c and calls no TLS/identity/signature functions. Existing SDK configuration and unused HTTPS component settings were retained to avoid unrelated Kconfig changes; they do not start an HTTPS listener. Legacy signing/provisioning tools/tests remain as historical tooling only; they are not inputs to firmware or routine use.

See [current migration plan and exact artifact hashes](M6C_MIGRATION_PLAN.md) and [pending hardware acceptance](HARDWARE_ACCEPTANCE.md). Existing recovery artifacts remain unchanged. No hardware was accessed during this software change.
