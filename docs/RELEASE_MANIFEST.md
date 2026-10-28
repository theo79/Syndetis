# Syndetis release candidate manifest — 28 October 2026

**RELEASE CANDIDATE — FINAL HARDWARE IMAGE CONFIRMATION PENDING.**

## Version and Git identity

- Firmware version: **0.5.2**, retained from the existing Ethernet OTA development line. No version bump, new feature or architectural change during cleanup.
- Verified firmware source commit: **`53a0178293ef8e4769a7abeaeb9ca71582471799`**.
- Reviewed remote base: `d8934dbe8729c68c76cca896673b9a1e8424684f` (0.5.1 OTA checkpoint); local history fast-forwarded before preserving the 0.5.2 reliability changes.
- This manifest is a documentation-only successor to the firmware source commit. The candidate's exact compiled inputs were SHA-256 compared with that commit; subsequent metadata changes do not change firmware inputs. Its complete input list is also in the ignored local candidate manifest.
- Final annotated release tag: **deliberately deferred** until exact installed-image identity and any required post-update smoke test are confirmed. No temporary or final tag was created.

## Toolchain and build

ESP-IDF **v6.1**, exact SDK commit `fff9895c82d744c7237be8847347bdd1b07c6643`; ESP32 target; Xtensa GCC **15.2.0**, `esp-15.2.0_20251204`. Dependency lock retains **LAN87xx 1.1.0**. Host verification used Python 3.14.4, Node 22.22.1, Playwright 1.62.1 and installed Google Chrome 156.0.8078.73.

A clean build in a newly created `build-final/` completed using the pinned configuration. `scripts/verify_build.py` confirms exact effective Kconfig equality, six-entry partition map, rollback enabled, secure-boot/encryption configuration unchanged, image-hash flag, OTA data size and bootloader/application fit. No hardware capacity/security-state measurement is inferred from configured values.

| Artifact | Size / identity |
|---|---|
| Raw application `Syndetis-v0.5.2.bin` | **933,440 bytes** |
| Application SHA-256 | `5e2a684cdda639eb8c34b97881540bbc0d963775d516135c4317b44438e10fb3` |
| Actual image descriptor | `Syndetis`, `0.5.2`, ESP32; descriptor magic and target checked |
| ESP image checksum / appended validation hash | Valid, confirmed by offline esptool image-info |
| Bootloader | 26,240 bytes; 2,432 bytes remaining below table |
| Partition-table SHA-256 | `4b1bbf2c902279fac6140aba1fe6d2c11b9867168020ff18e3ddd824bc952ca0` |
| Pinned configuration SHA-256 | `f97d2a2351741f49cfba5a6953bd84b0672027645e9164af87343a05ddad7238` |

Whole-file SHA-256 above is the candidate's identity, distinct from the internal ESP validation hash. Rebuilding can change timestamps/ELF metadata and whole-file bytes even with identical version/source; version equality is not binary identity.

## Configured partition capacity

Configured flash: 4 MiB. Table: 0x8000. No migration, provisioning or device write was performed.

| Partition | Offset | Bytes |
|---|---|---:|
| nvs | 0x9000 | 24,576 |
| phy_init | 0xf000 | 4,096 |
| otadata | 0x10000 | 8,192 |
| ota_0 | 0x20000 | 1,966,080 |
| ota_1 | 0x200000 | 1,966,080 |
| ota_keys (reserved legacy region, unused) | 0x3e0000 | 131,072 |

Each application slot has **1,032,640 bytes free (52.52%)** for this image. Historical factory recovery tables/apps remain separate; no recovery bytes changed.

## Verification results

| Verification | Result |
|---|---|
| Complete `scripts/run_tests.py`, `BROWSER_CHANNEL=chrome` | PASS: 12 C suites, Python/Node checks, dashboard phone/laptop browsers and all 16 update scenarios over Wi-Fi/Ethernet; no skipping |
| `scripts/run_ota_crypto_tests.py` against pinned SDK | PASS: retained legacy crypto tooling and actual discovery startup tests |
| `tests/ota_identity_test.py --idf-path ...` | PASS: legacy offline NVS-generation test using ephemeral test material, no production provisioning |
| Fresh ESP-IDF v6.1 build and `idf.py size` | PASS; no compiler warnings/errors |
| `scripts/verify_build.py build-final` | PASS |
| Offline esptool image-info / actual descriptor checks | PASS: checksum, appended hash, project, version and chip identity |
| `scripts/verify_checksums.py --manifest recovery/SHA256SUMS.txt` | PASS: all 15 payload checksums |
| Staged secret/material scan and source-input hash comparison | PASS; no generated builds, backup files, private keys/tokens or per-device identity material staged |
| `git diff --check` | PASS |

Five SDK CMake private-include dependency warnings between esp_wifi and wpa_supplicant remain; no SDK modifications were made. Logs are ignored in `tmp/release-freeze/`; earlier verification logs are preserved in the external archive.

## Hardware acceptance and update recommendation

**9/9 PASS, user-reported. Test date: user-reported / date not recorded.** The nine reported categories are boot/OLED/buttons, Ethernet/reconnect, LLDP/CDP, DHCP, Manual IPv4/conflict, gateway/custom Ping, repeated operations across two ports, ordinary OTA, and non-destructive USB recovery readiness. See [acceptance record](FINAL_FIELD_ACCEPTANCE.md).

Exact installed firmware version, whole-file hash, device identifier and additional measurements were not supplied. The user report therefore cannot establish that this candidate binary was the tested image. No flash readback, reset, update or hardware inspection was initiated in this pass.

**Final update/flash recommendation: UNDETERMINED.** Match the device to the exact candidate hash or equivalent trustworthy evidence first. If exact identity is established, NO FLASH NEEDED; if an older/different image is established, FINAL FLASH REQUIRED with separate update authorization and post-update smoke testing. A different same-version 0.5.2 image is rejected by the current OTA rule; do not bypass that check. No final release acceptance or annotated tag until this gate is closed.

USB recovery readiness is user-reported PASS and all stored recovery checksums pass. This does not claim a destructive recovery or power-cut/rollback exercise.

## Known noncritical maintenance limitations

Preserved as requested, without adding features or redesigning OTA:

- Slow rejected HTTP upload bodies can delay SDK request cleanup/server shutdown; mocked HTTP tests do not establish real socket lifecycle timing.
- Dashboard plus update-server maximum socket demand exceeds the configured pool when both are fully occupied. Use one maintenance client and avoid concurrent dashboard clients.
- Immediate reopen during updater/hotspot teardown may lose the request or require waiting and retrying physically.
- Wired link/address changes invalidate a Wi-Fi-only update reservation too; wired initialization must succeed to reserve maintenance. Keep the connection stable during maintenance.
- HTTP provides no encryption or cryptographic firmware authentication. Physical authorization/local subnet filtering and ESP checksums do not authenticate firmware authorship. Use trusted images on a controlled local connection.

No confirmed unresolved defect blocking ordinary portable diagnostics was found in this pass. Electrical fault injection, measured runtime margins and destructive rollback testing are not inferred from the nine aggregate PASS results. The outstanding final-release blocker is exact hardware image confirmation and any resulting update/smoke test.

## Cleanup, artifacts and GitHub synchronization

[Cleanup record](REPOSITORY_CLEANUP.md) lists retained/archived material. Previous `tmp/` (1,780 files) and old build (1,255 files), including device backups, test keys, old candidate images and audit evidence, were moved intact into the private external archive `/home/tanasto/Documents/Syndetis-archive-20261028-final/` and hash-verified. No permanent deletion or history rewrite occurred. Source/test/configuration/recovery assets and necessary historical documentation are retained.

Active structure: `main/`, `scripts/`, `tests/`, `docs/`, `recovery/`, root CMake/configuration/lockfiles; ignored `build-final/`, `tmp/`, `releases/`, `managed_components/` and `node_modules/` are local development outputs/dependencies. No large archive was added inside the repository.

Local candidate package: `releases/v0.5.2-candidate/`, containing raw application, `manifest.json`, `verification.json`, offline `image-info.txt` and `SHA256SUMS.txt`. Its firmware copy exactly matches `build-final/Syndetis.bin`; every package checksum is verified. These generated binaries remain excluded from GitHub. Required source and supporting tools/tests are committed; this is not a GitHub Release upload.

GitHub source synchronization: **PASS** — firmware source commit `53a0178293ef8e4769a7abeaeb9ca71582471799` was pushed by SSH to the existing **PRIVATE** `git@github.com:theo79/Syndetis.git`, branch `main`, and confirmed by `git ls-remote`. This manifest is a metadata-only successor; final main/HEAD equality is checked after its separate normal push. No other branch, tag, visibility or history changes are requested. Exact final documentation commit is available from Git history rather than a self-referential hash in this file.

**Syndetis is feature-frozen for its intended portable Ethernet diagnostic role. Future changes require a demonstrated technician need or a confirmed reliability defect.**
