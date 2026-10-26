# Syndetis5C migration manifest

Creation date: **2026-10-26** (Europe/Athens project date).

| Item | Value |
| --- | --- |
| Checkpoint | Syndetis5C |
| Application/About version | **0.4.0**, retained unchanged |
| Source repository | https://github.com/theo79/Syndetis — verified PRIVATE before publishing |
| Source working branch / HEAD | `master` / `8117bcbc8f41ae9a2197daa16c71dd1535cb7616`, with accepted uncommitted 5A/5B files included |
| Publishing branch | `syndetis-5c` |
| Publishing parent | `90d3d8452d1a8d2a67232526b330babd3896744f`, inspected current remote default tip; its empty source tree is left unchanged |
| SDK | Official ESP-IDF **v6.1**, commit `fff9895c82d744c7237be8847347bdd1b07c6643` |
| Component | `espressif/lan87xx` **1.1.0**, original manifest and lock retained |
| Target | ESP32 WT32-ETH01, LAN8720A address 1; original GPIO/PHY/OLED/button definitions unchanged |
| Flash / layout | 4 MiB, DIO/40 MHz; NVS 0x9000/0x6000, PHY 0xF000/0x1000, single factory app 0x10000/0x100000 |
| Accepted saved 5B app | **905,792 bytes**, SHA256 `c87668b12cee4a797818d77bac6138b82ed06fcd590d5b0e14f2ac86f50dd59b` |
| Earlier saved flashed v0.4.0 app | **891,136 bytes**, SHA256 `1bf4cee85e68e501ff0a009959ac436086b775637d2bea941d38b9c90fd4b0d1` |
| Fresh portable build / tests | See [docs/VERIFICATION.md](docs/VERIFICATION.md) and its independently generated build verification summary |
| Recovery checksums | [recovery/SHA256SUMS.txt](recovery/SHA256SUMS.txt) |
| Full checkpoint checksums | `SOURCE_SHA256SUMS.txt` (excludes itself and generated/cache files) |

The publication commit is the enclosing Git commit, available with `git rev-parse HEAD` after cloning the named branch; the delivered commit SHA is also reported in the task result. A document cannot contain its own final Git commit hash. Source HEAD and publishing-parent commit identities are recorded explicitly above. On an extracted ZIP without Git metadata, use the accompanying archive SHA256 and file manifest.

## Scope and comparison

All current firmware sources/headers, component registration, Ethernet/DHCP/Manual/ARP/Ping/UI/hotspot implementations and embedded dashboard were copied, including the untracked files absent from source HEAD. Main component inputs match the accepted working source after canonical LF normalization; `docs/FIRMWARE_SOURCE_SHA256.json` records their hashes. No obsolete A–G application code is substituted.

Packaging changes are limited to: root CMake SDK/target guards and initialization from a full portable configuration; LF attributes; portable host/browser runner/dependency lock; documentation/redaction; recovery metadata and checksum scripts. Application C/header/HTML logic, application version, required compiler options, dependency manifest/lock, defaults, board pins and partition selection are retained. The explicit `partitions.csv` is a layout record; the original SINGLE_APP SDK selection is intentionally unchanged and independently binary-verified.

Build/cache/managed-component directories, local editor state, installed Windows tools, raw field data, original Git metadata, old source trees and external preservation packages are excluded. The locked LAN87xx component is fetched by the SDK component manager; its downloaded directory is regenerated, not a hidden dependency on the old workspace. Browser tests declare Playwright 1.62.1 and use a locked npm dependency tree. Node 20+ and a normal host C compiler are external test tools.

## Hardware and artifact provenance

The user reports Milestones **5A and 5B hardware validation passed**, including the previously recorded DHCP/CDP/LLDP baseline. Packaging makes no new hardware claim and does not flash or read back the device. The saved 5B binary is a verified existing build artifact; this session does not independently establish which exact bytes are currently on hardware. Earlier saved v0.4.0 and verified v0.3.0 images are labeled separately.

A fresh portable build may have the same size but a different SHA256 because compile time, project/build paths, debug/ELF inputs and the embedded ELF digest change. Exact saved images remain available for restoration. Source/configuration/layout equivalence is verified; bit-for-bit reproducibility is not asserted without evidence.

## Migration readiness and remaining work

The checkpoint is designed to build from its own directory with the pinned official SDK, independently of the old Windows project. A Windows independent build and portable regressions are required before publication; a clean remote-clone check is performed after pushing where feasible. Actual Linux execution remains a separate environment check, not something inferred from a Windows build. Follow LINUX_SETUP.md, including SDK activation, host/compiler/browser dependencies and serial permissions.

**Before formatting**, complete [docs/EXTERNAL_BACKUP_CHECKLIST.md](docs/EXTERNAL_BACKUP_CHECKLIST.md). The original A–G bundles, full source/recovery evidence, unsanitized field records and any irreplaceable photos are not all recoverable from this branch. No off-laptop copy is claimed unless a real destination copy has been verified.

Current limitations: IPv4-literal four-request Ping only; no Continuous Monitor, hostname diagnostics, Wi-Fi OTA or Ethernet OTA. Existing ARP/native network timing limitations remain documented in VALIDATION.md. Next: **Milestone 6 — Wi-Fi and Ethernet OTA with physical authorization and an explicitly planned partition migration**. Continuous Monitor stays deferred.
