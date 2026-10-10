# Normal GCC validation — 2026-10-09

The complete clean normal ESP-IDF GCC build and all seven requested validation scripts passed. The new application is ready for review before any separately approved hardware update. Hardware reception and runtime regression remain untested.

## Toolchain

- ESP-IDF: **v6.1.0**, confirmed by official `idf.py --version`.
- GCC: **15.2.0**, crosstool-NG **esp-15.2.0_20251204**.
- Python: **3.11.15**, `C:/Espressif/tools/python/v6.1/venv/Scripts/python.exe`.
- GCC launcher: `C:/Espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin/xtensa-esp32-elf-gcc.exe`.
- Windows blocking encountered during this resumed run: **NO**. No Windows security settings were changed by the agent.

The official installed PowerShell profile's exports initialized the build process. `IDF_TOOLCHAIN=gcc` was set explicitly. CMake identified GNU 15.2.0 for both C and C++. No Clang configuration or artifacts were used.

## Build

| Check | Result |
| --- | --- |
| Complete normal GCC build | **PASS**, process exit 0 |
| Bootloader compile/link/image/size check | PASS |
| Partition generation | PASS |
| Application compile/link/image/size check | PASS |
| Bootloader size | **26,240 bytes**, 2,432 bytes free below the existing allowance |
| Application size | **470,112 bytes** |
| OTA slot size | **1,740,800 bytes**, each slot |
| OTA headroom | **1,270,688 bytes** (about 73%) |
| Partition layout unchanged | **YES**, byte-identical to preserved baseline |
| Firmware descriptor | **0.5.0-dev**, project `syndetis_finder`, ESP-IDF v6.1 |
| Binary | `C:/Users/theoc/Documents/ChatGPT/Syndetis/build/syndetis_finder.bin` |
| SHA-256 | `1c61f468069dc2ed3be354d9ca7bcf0e99428e4480a9c2a46c4384f9022872ec` |
| Build errors | None |

The GCC application is **131,664 bytes smaller** than the prior 601,776-byte experimental Clang application. The sizes were measured independently.

Before cleaning, the entire old `build` directory was moved within the workspace to `build_evidence_before_gcc_20261009`, preserving the old 0.4.2 image, experimental artifacts, and diagnostic logs. `idf.py fullclean` then reported no build directory to clean and completed its managed-component cleanup. `idf.py build` generated a fresh root build directory using the project configuration.

Build warnings/notices:

- Git was not found in the official build process PATH, so SDK submodule checks were skipped and version detection used installed source information. The application version was correctly read from `version.txt` and independently checked in the binary. Git was available in the desktop shell and was added to the test process PATH.
- Five ESP-IDF component-validation warnings concerned private include directories shared between `esp_wifi` and `wpa_supplicant`.
- ESP-IDF Kconfig notices reported invalid boolean `default 0` values for two Bluetooth and two FATFS options, treated as `n`.
- No C/C++ compiler warnings or build errors were found in the captured build log. The notices did not change the project's sdkconfig bytes.

Full build output: `gcc_validation_build.log`.

## Partitions

| Name | Offset | Size (bytes) |
| --- | --- | ---: |
| nvs | 0x9000 | 24,576 |
| otadata | 0xF000 | 8,192 |
| phy_init | 0x11000 | 4,096 |
| ota_0 | 0x20000 | 1,740,800 |
| ota_1 | 0x1D0000 | 1,740,800 |

New and preserved baseline partition binaries have the same SHA-256: `41d8ef7960bf03647249e26ca1ace2b8d35f317a87f6c2ff5bc9f4bdbdbf602b`.

Release verification also checked partition MD5, bounds, flash size, OTA slots, erased initial OTA data, firmware descriptor, image segment bounds, checksum, appended SHA-256, and bootloader allowance.

## Tests

All scripts exited 0 after the successful build. Individual outputs are saved as `gcc_validation_<test>.log`; exit codes are in `gcc_validation_test_results.json`.

| Test | Result | Evidence scope |
| --- | --- | --- |
| verify_switch_info_parse | PASS | GCC compile-time evaluation of actual parser/classifier and mocked RX bodies, ownership/drop cases, source invariants |
| web_ui | PASS | Node execution of embedded browser logic with fixtures; discovery, AutoTest, existing OTA behavior |
| verify_release | PASS | Generated normal firmware/partition integrity and metadata checks |
| verify_autotest_rules | PASS | 39 compile-time policy assertions |
| verify_discovery_logic | PASS | GCC constexpr evaluation of production discovery logic |
| verify_fast_discovery | PASS | GCC constexpr callback fixture and configured ARP cache preprocessing |
| verify_link_loss | PASS | GCC compile-time lifecycle/DMA fixtures, source invariants, active patch routing |

Compile-time assertions and mocked callbacks are not hardware runtime validation. Node fixture results are not a live HTTP/device test.

## Source/config integrity

Before-build SHA-256 values are preserved in `gcc_validation_integrity_before.json`. The after-build comparison found **zero changed files** across all files under `main` and `patches`, plus root `CMakeLists.txt`, `sdkconfig`, `sdkconfig.defaults`, `dependencies.lock`, and `version.txt`.

- Firmware source: unchanged during this run.
- sdkconfig: unchanged.
- sdkconfig.defaults: unchanged.
- Partition table: generated bytes unchanged from baseline.
- Ethernet DMA patch: unchanged.
- Ping patch: unchanged.

Existing unrelated working-tree changes were retained. Only build/test artifacts, integrity evidence, and this report were generated.

## Hardware

- Flashed: **NO**.
- OTA performed: **NO**.
- Hardware tested: **NO**.

## Recommendation

The normal GCC `build/syndetis_finder.bin` is **ready for user review before flashing**. It satisfies this build/test gate. It does not yet establish real LLDP reception, runtime memory margins, or hardware regressions; those remain the next separately approved validation stage.
