# Phase B validation gate — 2026-10-09

**Gate stopped: normal ESP-IDF GCC build prerequisite failed because installed tools are blocked by Windows application control. Hardware validation remains incomplete.**

No firmware source, partition layout, sdkconfig, DMA/ping patches, or security settings were changed during this gate. No fullclean, flash, OTA, or device monitoring was performed. Existing diagnostic artifacts were preserved.

## Build

| Item | Result |
| --- | --- |
| Normal complete GCC build | FAIL — tool execution blocked before compilation |
| ESP-IDF | Installed source version 6.1.0; CLI version query blocked |
| GCC runtime version | Unavailable; installed package path identifies esp-15.2.0_20251204 |
| New normal bootloader/application sizes | Unavailable |
| New normal OTA headroom | Unavailable |
| Source version | 0.5.0-dev |
| Existing root build application | 463,616 bytes; embedded version **0.4.2**, unsuitable for this validation |
| Prior experimental Clang application | 601,776 bytes; 0.5.0-dev; application-only verification |
| Existing OTA slot size | 1,740,800 bytes per slot |
| Prior Clang application headroom | 1,139,024 bytes; cannot substitute for a normal GCC result |

The official installed `C:/Espressif/tools/Microsoft.v6.1.PowerShell_profile.ps1 -e` exports were applied to the diagnostic process. They selected `C:/esp/v6.1/esp-idf` and `C:/Espressif/tools/python/v6.1/venv`. A read-only diagnostic outside the sandbox still failed:

- The target GCC launcher reported failure to start its child, OS error **4551**, “An Application Control policy has blocked this file.”
- The underlying generic GCC executable also reported an application-control block when invoked directly.
- Official Python failed to import `_overlapped`, reporting an application-control block, so `idf.py --version` could not complete.
- Inside the sandbox, the target GCC launcher additionally showed an Illegal System DLL Relocation error. Correct official exports did not resolve execution outside the sandbox.

These failures establish a host execution blocker independent of project compilation. They do not establish tool corruption or a firmware defect. Reading Code Integrity event logs was denied, so the exact blocking rule was not verified. No policy or DLL-search workaround was applied.

There is no new normal-build warning/error log: compilation never started. Prior experimental Clang verification remains historical evidence only. Its complete build failed because its 29,584-byte bootloader exceeded the existing 28,672-byte allowance by 912 bytes. Do not flash that build or relax its checks.

## Partition evidence

`sdkconfig`, `sdkconfig.defaults`, and `dependencies.lock` have no current Git diff. A partition layout from a **successful new normal build is unavailable**. The existing baseline partition binary has SHA-256 `41d8ef7960bf03647249e26ca1ace2b8d35f317a87f6c2ff5bc9f4bdbdbf602b`; the prior isolated Clang partition binary matched it.

| Partition | Offset | Size (bytes) |
| --- | --- | ---: |
| nvs | 0x9000 | 24,576 |
| otadata | 0xF000 | 8,192 |
| phy_init | 0x11000 | 4,096 |
| ota_0 | 0x20000 | 1,740,800 |
| ota_1 | 0x1D0000 | 1,740,800 |

This baseline evidence does not satisfy verification of the requested new normal build.

## Tests

The requested post-GCC tests were not run because the prerequisite failed. Historical Phase B results are documented separately in `LLDP_PHASE_B_0.5.0_DEV.md`; they are not a fresh validation-gate pass.

| Test | Current gate |
| --- | --- |
| `tests/verify_switch_info_parse.py` | NOT RUN |
| `tests/web_ui.test.cjs` | NOT RUN |
| `tests/verify_release.py` | NOT RUN |
| `tests/verify_autotest_rules.py` | NOT RUN |
| `tests/verify_discovery_logic.py` | NOT RUN |
| `tests/verify_fast_discovery.py` | NOT RUN |
| `tests/verify_link_loss.py` | NOT RUN |
| Physical OTA authorization/rollback regression | NOT RUN |

Earlier parser/RX assertions and bounded mutation checks were evaluated by Clang at compile time; this is not native executable or device testing. Earlier browser tests executed under Node. Earlier AutoTest/discovery/link-loss compile-time checks likewise do not demonstrate runtime hardware behavior.

## Hardware and runtime

Real LLDP reception observed: **NO — test not performed**, not evidence of reception failure. Board firmware version, multicast filter result, switch vendor/model/port, chassis ID, port ID, system name, management IP, PVID, and TTL were not observed.

Free heap, minimum heap, worker stack watermark, and all LLDP counters were not measured at any requested runtime checkpoint. No real API JSON, switch configuration comparison, serial log, screenshot, or hardware photo was obtained.

## Regression

| Area | Result |
| --- | --- |
| DHCP | NOT RUN |
| Web UI/status | NOT RUN |
| `/api/switch-info` | NOT RUN |
| AutoTest | NOT RUN |
| Fast Discover | NOT RUN |
| OLED | NOT RUN |
| Buttons | NOT RUN |
| OTA authorization/reboot/boot confirmation | NOT RUN |
| LLDP link lifecycle/port change/TTL expiry | NOT RUN |

## Remaining issues and resumption

- **Host build environment:** confirmed execution blocker. A working official GCC/Python environment is required. An operator can check the same version commands in the normal ESP-IDF desktop shell and use approved official installation repair if warranted. This report does not recommend disabling or altering security controls.
- **Code:** no new defect demonstrated by this gate; implementation unchanged.
- **Switch configuration and hardware:** untested; no fault attribution is possible.
- **Unsupported features:** Phase B remains untagged nearest-bridge LLDP only. CDP and final OLED Switch Info pages remain deferred.

After official tools execute successfully, preserve useful diagnostics before a normal `idf.py fullclean` and `idf.py build`. Verify all complete-build checks, new application descriptor/version, partition bytes, and post-build tests. Only then use the normal `build/syndetis_finder.bin` with the existing physically authorized OTA workflow and perform the requested direct-switch LLDP and regression tests. Neither currently retained application image is approved for this gate's hardware update.
