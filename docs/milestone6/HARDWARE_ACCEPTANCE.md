# Historical extended hardware acceptance checklist

Current ordinary acceptance is recorded as user-reported 9/9 PASS in [FINAL_FIELD_ACCEPTANCE.md](../FINAL_FIELD_ACCEPTANCE.md); this historical extended checklist is not retroactively marked complete. The user subsequently confirmed final-candidate installation and passing post-flash smoke testing; local artifact identity is verified. This does not complete unreported extended tests below.

The user confirmed successful v0.5.1 Wi-Fi browser OTA on 28 October 2026. Detailed checklist items below remain pending unless individually recorded; normal Wi-Fi success does not imply Ethernet, power-cut, rollback or runtime-margin acceptance. Record date, operator, board/flash ID, silicon revision, security state, image hashes/versions, observed result and recovery evidence for every case. No hardware PASS may be inferred from host mocks or the software build.

## Readiness and migration (requires separate device/flashing approval)

- [ ] Actual flash >=4 MiB, correct ESP32 target/revision and relevant security state safely read; no eFuse/security changes.
- [ ] Adequate stable power, accessible 3.3 V UART/boot-mode wiring; ROM loader reachable before enclosure closure.
- [ ] Full old flash and NVS/PHY/bootloader/table backups secured and hashed; packaged recovery checksums pass.
- [ ] Fresh reviewed 0.5.0 build/verification, correct six-partition table, bootloader fit and raw application hash verified; unused ota_keys untouched.
- [ ] Controlled local HTTP connection on 8080; operator understands no encryption or firmware authentication; no credentials/CA/identity required.
- [ ] Authorized initial USB migration succeeds without erasing default NVS/PHY; version and diagnostic baseline verified.
- [ ] Unprovisioned device opens updater only after physical UI authorization; no listener after boot.

## Ordinary update over both transports

- [ ] Wi-Fi-only, no router/Internet: physical session, connection instructions, upload, OLED/browser progress, controlled reboot and new version.
- [ ] Ethernet with current DHCP address: same successful flow.
- [ ] Direct Ethernet, no DHCP: deliberate Manual IPv4 on both ends, address conflict checks, correct HTTP:8080 URL, successful flow.
- [ ] Port-80 dashboard and AP-address Ethernet access remain blocked before/during/after session; TCP/8080 closes on cancel/expiry/reboot.
- [ ] Stable pre-existing Web Access state retained on cancellation; temporarily created hotspot closes; no diagnostic/address ownership race.
- [ ] No update listener after normal reboot until a new physical session; second concurrent client rejected.
- [ ] No physical session, cross-origin POST, wrong Host/interface/subnet, replayed upload/session and expired session rejected.
- [ ] Wrong-product/chip/revision, same/older version, malformed, corrupt, truncated and oversized images rejected without boot selection.
- [ ] Unplug Ethernet, disconnect Wi-Fi, abort browser, cancel on OLED/browser, stall and absolute upload expiry: inactive handle cleaned up, running firmware remains bootable, fresh retry succeeds.
- [ ] Disconnect after verification/commit: device safely boots committed image or reverts; client checks actual version rather than assuming failure.

## Power failure and rollback matrix (sacrificial/recoverable development unit)

- [ ] Cut power before erase, during sector erase, early/mid/late write, image validation, and boot-selection metadata update; restore power and verify known-good boot.
- [ ] Cut power on first pending-verification boot before health confirmation; previous image returns.
- [ ] Deliberately failing trial crashes or fails bounded UI/task/NVS checks; automatic rollback occurs; previous version still functions.
- [ ] Successful trial confirms once; repeated normal boots remain on new version.
- [ ] No Ethernet cable, no DHCP server, no Wi-Fi client and no gateway do NOT cause false rollback.
- [ ] Default NVS and reserved unused region remain compatible/readable after upgrade and rollback; no unexpected erase or schema changes.
- [ ] If neither slot boots, verified USB recovery restores operation, preserving settings where intended.

## Resources and diagnostic preservation

- [ ] Measure min free heap/largest free block and task stack high-water marks before session, HTTP connection, upload, image validation, reboot and retry; exercise two HTTP clients and four AP stations. Record margins, not just success.
- [ ] Measure OLED latency/responsiveness and LLDP/CDP capture while flash writes run; ensure watchdog safety and adequate bounded worker buffers.
- [ ] Verify speed/duplex/link, LLDP/CDP long/empty/XSS/expiry cases, DHCP acquire/timeout/cancel/cleanup, Manual IPv4 probes/conflict/release, gateway/custom four-request Ping and three-button UI after update and after rollback.
- [ ] Run the complete host/Node/Playwright suite against the exact source release; store image hashes and hardware evidence with results.

Acceptance remains **PENDING** until these tests are actually performed. Do not seal off emergency serial recovery access based on software PASS alone.

- [ ] Verify Preparing update... is visible before flash, Uploading XX%/bar is accurate, 100% requires complete reception, Verifying.../Update complete/Rebooting... and error/cancel states are visible; throttle/no OLED-flash overlap and watchdog margins on actual SH1106.

## Recorded normal-update result

- [x] User-confirmed physical Wi-Fi browser OTA of the prepared v0.5.1 firmware via http://192.168.4.1:8080/update, 28 October 2026. See M6D_WIFI_OTA_0.5.1.md for image size/hash.
- [ ] Real Ethernet OTA acceptance with strictly newer v0.5.2.
