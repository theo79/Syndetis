# Checkpoint verification — historical 0.4.0

Current Milestone 6 development verification is recorded in [milestone6/RESULTS.md](milestone6/RESULTS.md). The statements below describe the preserved pre-OTA checkpoint, not the current partition/configuration.

Verified 2026-10-27. A fresh build from this standalone directory passed with official ESP-IDF v6.1, ESP32 and the locked LAN87xx dependency. Effective Kconfig matches the accepted source configuration; the generated partition table is byte-identical to the saved 5B table. All 40 firmware/component inputs match after LF normalization.

Application: **905,664 bytes**, leaving **142,912 bytes** in the 1,048,576-byte factory partition. SHA256: `a02a7694a766f08fc1e9bb3ecc9ba86db57c7f7a8419a45e4da392087a285961`. Application version remains **0.4.0**.

Eight host C suites, HTTP/JSON, Node dashboard regressions and Chrome phone/laptop browser tests passed using the declared locked Playwright dependency. Tests cover current DHCP/manual/Ping ownership, conflict, stale/cancel/link races, discovery dispatch, UI and hotspot isolation. See VALIDATION.md for coverage boundaries.

The saved accepted 5B application is 905,792 bytes (its exact checksum is in recovery/SHA256SUMS.txt). Canonical LF conversion of the embedded dashboard removes 131 CRLF bytes and reduces aligned read-only data/image size by 128 bytes. Code, IRAM and DRAM section sizes remain unchanged. Build time/path and ELF digest can also change binary hashes; byte identity with historical flashed firmware is not claimed.

Existing SDK private-include deprecation warnings and the local SDK Python environment warning did not prevent a successful build. Actual Linux execution remains unperformed; Linux instructions use the verified declared SDK/configuration and cross-platform test runner. Remote fresh-clone results are reported separately in the delivery.

The owner reports 5A/5B hardware acceptance. No flashing or device readback occurred here. Original source files and index were checked against pre-packaging hashes. Raw machine-specific build logs remain outside the published checkpoint.
