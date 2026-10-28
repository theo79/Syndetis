# Final reliability report — 28 October 2026

**Software verification: PASS. Hardware acceptance: 9/9 PASS (user-reported; test date not recorded).**

**RELEASE CANDIDATE — FINAL HARDWARE IMAGE CONFIRMATION PENDING.** The user subsequently reported successful ordinary field acceptance, including OTA and non-destructive USB recovery readiness. Installed binary hash/version were not supplied; this does not establish that any specific rebuilt image was tested. See [acceptance evidence](FINAL_FIELD_ACCEPTANCE.md) and the current [release manifest](RELEASE_MANIFEST.md). No hardware operation was performed by this agent.

## Baseline and scope

- Linux host; branch `main`, HEAD `260afc42e999845c2d670c11ce81e117920ff4ad` (preserved Milestone 5C checkpoint). Working application and existing build metadata both identify **0.5.2**, unchanged by this pass. The version installed on hardware is unknown.
- The supplied tree was already dirty: 117 modified tracked files plus 23 untracked entries at the recorded baseline. Existing OTA development, permissions changes, configuration changes relative to HEAD and recovery artifacts were preserved. Comparing against the prior audit's `tmp/benchmark-audit/source-hashes.json` isolates this pass to the seven existing files listed below; the three added files are also listed. A diff against HEAD includes earlier work and is not this pass's change set.
- Toolchain: official ESP-IDF v6.1, commit `fff9895c82d744c7237be8847347bdd1b07c6643`, Xtensa GCC 15.2.0 (`esp-15.2.0_20251204`); ESP32 / configured 4 MiB, two 0x1e0000-byte OTA slots, rollback enabled. Effective configuration exactly matches the supplied `sdkconfig.pinned`.
- Reviewed the available `BENCHMARK_AUDIT.md` at the beginning of the pass and the independent audit evidence/probes/logs in `tmp/benchmark-audit/`. The Markdown audit later ceased to be present in the shared tree; no second standalone audit report was found. Findings were checked against current production sources and pinned SDK internals. References in older audit material to signed HTTPS uploads do not describe the current raw-bin HTTP:8080 updater.

## Confirmed defects corrected

| Defect and smallest correction | Focused regression evidence |
|---|---|
| Splash and ordinary redraws aborted on OLED I2C errors. Replace fatal transfer checks with a nonfatal frame attempt; log once per failure episode, retry through existing refreshes, retain button polling and release the UI gate. Retry unchanged OTA progress frames after failure; acknowledge health/notices only after successful transfers. OLED initialization failure policy remains unchanged. | New `tests/main_loop_test.c` runs the production startup/polling loop with splash and runtime transfer failures, an unchanged progress screen, and checks retry, successful-only acknowledgement and gate release. |
| Link changes during DHCP cleanup discarded cleanup failures with the stale outcome. Preserve cleanup error/code/step even when a newer link/cancel event owns the outcome; no old address is republished. | Inject reconnect during DNS restoration with DNS-restore or stack-stop failure. Error survives subsequent worker ticks, new DHCP/Manual/OTA work is blocked, explicit Stop retries and restores passive state. |
| RX drain could wait forever in the sole wired worker. Bound it to 20 one-tick waits (20 ms at the pinned 1000 Hz). On expiry disable TX, retain stack/DNS ownership, report cleanup timeout and allow explicit Stop retry; do not tear down beneath an in-flight callback. | Stalled in-flight RX test checks elapsed bound, retained ownership, closed gates, no stuck busy state, blocked Start and successful release after callback retirement. |
| Cancellation during OTA's preparing/verifying OLED notice could complete the async request while leaving the engine receiving and a writer open. Cancel the engine under its existing mutex before completing the upload. | Cancellation at each notice checks terminal state, no activation, exact writer abort when applicable, reservation release and server cleanup. |
| Failed optional OTA mutex allocation prevented all normal UI refreshes. Permit UI drawing without that mutex when allocation prevented OTA worker startup; guard the matching unlock. Existing mutual exclusion remains when allocated. | Production adapter regression exercises begin/end with a null mutex. |

## Workflow review

Source and existing actual-C tests cover passive boot/explicit IPv4 activation; 500 ms Ethernet link polling and cached negotiated speed/duplex with unavailable fallback; LLDP/CDP parsing, TTL/withdrawal and session filtering; DHCP's 30 s timeout, cancellation, release and DNS restoration; validated Manual IPv4 with three probes, two announcements and conflict withdrawal; gateway/custom four-probe Ping with wired binding, deadlines, cancellation and stale-generation suppression; button debounce, short/held OK and editor repeats; repeated operations and port changes.

Disconnect clears physical speed/duplex, neighbors and active addresses, closes wired traffic gates and invalidates Ping results. Reconnect does not automatically restart DHCP/Manual IPv4. Back intentionally retains an acquired address and says so; Stop / Release or USE DHCP explicitly releases it. No new diagnostic traffic, menus, tasks, dependencies or Wi-Fi functionality were introduced. This is software evidence, not a measurement of real link timing or physical usability.

## Deliberately deferred issues and limitations

1. **Rejected OTA request body lifecycle:** `response()` sets Connection: close but returns the send result. Pinned SDK `httpd_req_delete()` purges unread bodies in a loop; a slowly supplied rejected upload can hold the HTTP task and delay shutdown. Async completion also does not itself request session closure. Changing close/response ordering requires real socket integration tests to avoid resets, reuse races or invalidating async ownership. Deferred rather than claiming mocked HTTP tests validate the SDK lifecycle.
2. **Concurrent HTTP socket capacity:** dashboard allows four client sockets and update server two; each SDK server also owns three internal sockets. Maximum demand is 12 against `CONFIG_LWIP_MAX_SOCKETS=10`. Ordinary low-concurrency operation fits, but fully occupied dashboard connections can impair maintenance access. Deferred because selecting a shared budget changes existing dashboard concurrency and needs concurrent-server testing. No Kconfig expansion was made.
3. **Immediate OTA reopen during teardown:** supervisor consumes an accepted open request while a terminal server still exists, then closes it without replaying the request. A fresh physical request after teardown can work; hotspot STOPPING may also require waiting/retrying. Deferred along with lifecycle sequencing; this does not block ordinary wired diagnostics.
4. **Existing conservative maintenance restrictions:** Ethernet link/address changes invalidate even a Wi-Fi-only reservation, and failed wired initialization prevents reservation. Existing tests explicitly cover the former policy. Preserved rather than expanding OTA independence/ownership architecture.
5. Persistent OLED initialization/bus failure still needs physical diagnosis; this pass does not add bus reinitialization. The user reports successful ordinary OLED/button and diagnostic/OTA operation. Electrical fault recovery, measured timing/traffic isolation, stack/heap margins and destructive power-cut/rollback behavior are not established by that aggregate report. No physical fault injection was performed by this agent.

The user has requested preservation of the first three issues as noncritical OTA maintenance limitations. They are not known to block ordinary field diagnostics; no redesign is included in repository cleanup. The ordinary 9/9 hardware results are user-reported. Exact final image identity remains the release gate; passing software tests alone cannot close it.

## Files changed in this pass

- Production: `main/main.c`, `main/dhcp_test.c`, `main/ota_update.c`.
- Regression coverage: `tests/main_loop_test.c` (new), `tests/dhcp_test.c`, `tests/ota_adapter_test.c`, `tests/host_stubs/esp_err.h`, `scripts/run_tests.py`.
- Deliverables: `docs/FINAL_FIELD_ACCEPTANCE.md` and this report (both new).

## Verification and capacity

All final commands completed successfully; local evidence is under ignored `tmp/final-field/`.

| Check | Result / evidence |
|---|---|
| `BROWSER_CHANNEL=chrome python3 scripts/run_tests.py` | PASS: 12 compiled C suites, Python package/identity tests, Node serializer/dashboard tests, Chrome phone/laptop dashboard cases and all 16 Wi-Fi/Ethernet update UI scenarios. No browser skipping. `tests.log`. |
| `python3 scripts/run_ota_crypto_tests.py --idf-path "$IDF_PATH"` | PASS: existing legacy signature verifier against pinned Mbed TLS/PSA and actual discovery worker startup regression. This does not assert current OTA uploads require signatures. `crypto.log`. |
| `python3 tests/ota_identity_test.py --idf-path "$IDF_PATH"` | PASS: legacy offline SDK NVS generation and permissions validation using ephemeral test material; no production credentials touched. `identity.log`. |
| Clean ESP-IDF build | PASS: fresh fullclean/configure followed by final `idf.py -B tmp/final-field/build clean build`; every application object rebuilt after final changes. `build.log`, `clean.log`. |
| `python3 scripts/verify_build.py tmp/final-field/build` | PASS: exact pinned Kconfig, partition map, rollback/security settings, image hash flag, OTA data and application/bootloader fit. `verification.log`, `build/verification.json`. |
| `idf.py -B tmp/final-field/build size` | PASS; linked payload 933,324 bytes. `size.log`. |
| Recovery checksums / protected inputs | All 15 recovery checksums PASS; supplied pinned/default config, partitions, lockfile and recovery contents match hashes captured before editing. `recovery.log`, `protected-before.sha256`. |
| `git diff --check` | PASS. |

Build has **no compiler warnings or errors**. Five CMake component-validation warnings concern private includes shared by the pinned SDK's esp_wifi/wpa_supplicant components; no SDK edits were made. Initial sandbox runs could not start Node child processes and later could not restore LAN87xx after fullclean; rerunning with permitted local process/network access resolved both environment failures. Locked LAN87xx remains 1.1.0.

| Firmware measurement | Bytes |
|---|---:|
| Application binary / raw upload | 933,440 |
| Each OTA slot | 1,966,080 |
| Remaining per slot | **1,032,640 (52.52%)** |
| Bootloader | 26,240 |
| Bootloader remaining below partition table | 2,432 |

Application SHA256: `46252b2c009d886f6a7dad89e72131b382f24f1c8f2b84ae3301affdfee95dae`.

The historical build measurements above belong to the earlier software pass; current rebuilt image identity is recorded in `docs/RELEASE_MANIFEST.md`. **Hardware acceptance: user-reported 9/9 PASS; exact final image confirmation PENDING.** No flashing, erasing, reset, provisioning, eFuse/security changes, signing-key changes, recovery changes, version bump, tag, push or release packaging was performed.
