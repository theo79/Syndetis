> Historical signed-HTTPS design/evidence, superseded by the user-approved raw-bin HTTP updater. See README.md and M6C_MIGRATION_PLAN.md for the current implementation and validation.

# Milestone 6 software verification — 27 October 2026

Status: **software PASS; hardware acceptance PENDING**. No serial device was visible, no hardware facts were inferred from sdkconfig, and no flashing, erasing, identity provisioning, eFuse or security-state change was performed. The existing device is not claimed to be running 0.5.0.

## Build and layout

EIM selected official ESP-IDF v6.1 at commit `fff9895c82d744c7237be8847347bdd1b07c6643`. GCC: esp-15.2.0_20251204. Build directory: ignored `tmp/m6-build`. No compiler optimization change was made; -Og remains. Build completed, followed by idf.py size / size-components / size-files and scripts/verify_build.py.

| Measurement | Result |
|---|---:|
| Application version | 0.5.0 |
| Application binary | **998,032 bytes** |
| Increment over 905,664-byte 0.4.0 baseline | **92,368 bytes** |
| Each OTA slot | **1,966,080 bytes** |
| Free per slot | **968,048 bytes (49.24%)** |
| Signed browser-upload package | **998,160 bytes** (128-byte trailer excluded from flash) |
| Bootloader binary | **26,240 bytes** |
| Bootloader margin below table | **2,432 bytes** |
| Flash code | 719,106 bytes |
| Flash data | 170,176 bytes |
| IRAM static use / remaining | 90,339 / 40,733 bytes |
| DRAM static use / remaining in reported linker region | 86,847 / 93,889 bytes |
| DRAM BSS / initialized data | 68,600 / 18,247 bytes |
| idf.py size image payload before binary headers/padding | 997,908 bytes |

App SHA256 for this build: `a57fcb5e822e21d37bde3fab9a5e045da999579aa962591efdd3e5e93441d5e5`. Rebuild timestamps/ELF hashes may change the binary hash. A release must be separately signed and versioned; signing this 0.5.0 build does not make it admissible over a device already running 0.5.0 (same-version uploads are rejected).

verify_build.py confirmed exact effective Kconfig equality to the reviewed pinned configuration; ESP32/4 MiB settings; rollback enabled; hardware Secure Boot/encryption disabled; exact six-entry partition layout with types/subtypes/flags, no overlap and app alignment; identity excluded from generic flash arguments; otadata initialization size; app and bootloader fit. NVS/PHY addresses are preserved. Recovery manifest still passes **15 SHA256 checksums**. No recovery bytes or historical source checksum manifests were edited.

## Automated tests

- Complete `python3 scripts/run_tests.py`: **PASS**, no browser skipping. Ten C suites (including two additions), two Python suites, two Node suites, existing two dashboard browser viewport scenarios and **16 new update-page scenarios**. Playwright remains lockfile version 1.62.1 and Chromium revision 1234. Node 22.22.1 / npm 9.2.0 were used.
- Existing diagnostics: core LLDP/CDP, UI/buttons, JSON, DHCP, Manual IPv4/conflict, Ping, HTTP actions and socket isolation regressions still pass. UI test includes the new Settings/update navigation, instructions/progress rendering bounds and physical cancellation.
- OTA engine: actual C state machine tested with many chunk boundaries, newer version/product/chip validation, unauthorized/replay/oversize/incomplete data, stall/absolute deadlines, cancellation and injected begin/write/signature/image/activation failures. Failed paths do not select an image.
- Wired access: actual owner/dispatcher/transmit code tested for default TCP blocking, temporary local TCP/443 only, cross-subnet/AP-address/port/fragment/length rejection, diagnostic exclusion, lease-generation/link invalidation, expiration and extension. Original RX buffers retain single-owner free semantics.
- Browser: phone/laptop and hotspot/wired HTTPS origins × success/rejection/disconnect/cancel; matching raw binary and authorization headers, no external resources, plain-text error handling and no horizontal overflow. Phone screenshot visually inspected. Browser traffic is intercepted and does not validate a physical TLS handshake or flash.
- `scripts/run_ota_crypto_tests.py --idf-path ...`: **PASS**. The **production** ota_signature.c runs against host-built pinned SDK Mbed TLS/PSA. Real Python P-256 signatures are accepted; wrong keys, a private-key input, altered signed metadata/signatures, canonical padding and altered image digests are rejected. Two host-only compatibility forwarding headers bridge Espressif's legacy bignum/ecp includes; no SDK sources are modified and no hardware acceleration is exercised.
- The same SDK host validation also compiles the **production** ota_update.c HTTP/async callbacks with only SDK I/O mocked, retaining real crypto. Tests cover both interfaces, concurrent/replayed upload, Host/Origin/type/code/interface/subnet rejection, disconnect/stall/cancel, Ethernet/Wi-Fi loss, all writer/finalize/select failures, missing identity, session setup/cleanup, confirmation and rollback/restart API sequencing. This validates adapter software flow, not flash atomicity or bootloader execution on silicon.
- `python tests/ota_identity_test.py --idf-path ...` under EIM: **PASS**. Actual SDK NVS generator produced a 0x20000-byte identity image in a temporary test directory; permissions and NUL-terminated PEM blobs verified. Invalid key match, lifetime, SAN, EKU, CA/leaf role, curve and signing-private-key input are rejected. Test keys are ephemeral and not used as production trust.
- The production discovery worker startup handshake is also host tested: allocation/task-creation failures stay unready; a scheduled worker becomes ready without a link or received advertisement. This avoids confirming a trial merely because xTaskCreate returned successfully.
- `git diff --check`: **PASS**. Baseline HEAD/branch remain unchanged; all implementation/test/doc edits are uncommitted. No commit, push, tag, release or visibility operation was performed.

## Resources and facts still needing hardware confirmation

There is no measurement of runtime minimum free heap, largest free block, task stack high-water mark on ESP32, TLS handshake memory, upload throughput, UI latency under flash writes, real image boot health or actual power-cut rollback. Do not infer these from static free-memory figures.

Configured allocations: 2,048-byte receive buffer on an 8,192-byte upload stack; 6,144-byte control stack; 8,192-byte HTTPS stack; <=2 HTTPS sockets; 288-byte prefix +128-byte signature trailer; fixed identity PEM arrays 4,096/2,048/1,024 bytes. No second firmware image is stored in RAM/filesystem. Heap admission requires >=32,768 free bytes and >=16,384 largest free block. Runtime minima/high-water mark are recorded in the internal status snapshot; Device Info retains heap/minimum heap display. These thresholds need real stress validation.

Actual unit flash capacity/manufacturer, silicon revision and relevant security state remain **UNKNOWN**, despite the configured 4 MiB and model datasheet. A flash-driver >=4 MiB gate is implemented, but initial hardware verification and recovery readiness remain prerequisites to migration. The dedicated identity and browser CA trust must be provisioned correctly before update access can operate.

Follow [HARDWARE_ACCEPTANCE.md](HARDWARE_ACCEPTANCE.md) after separate authorization. Overall milestone acceptance and enclosure-ready routine network updating remain **PENDING**, not hardware PASS.

## Local evidence (generated, ignored)

`tmp/m6-build.log`, `tmp/m6-build/verification.json`, `tmp/m6-final-size.log`, `tmp/m6-regressions.log`, `tmp/m6-crypto-test.log`, `tmp/m6-identity-test.log`, and `tmp/browser-screenshots/update-*.png`. Test identity material remains only in ignored temporary locations; the real identity-generation test directory was automatically removed.
