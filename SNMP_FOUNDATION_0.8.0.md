# Syndetis 0.8.0 — Read-Only SNMP Foundation

2026-10-12. Backend foundation implemented and software validated. Hardware acceptance and a user-triggered runtime caller remain pending. No commit, flashing, OTA, remote writes, or live subnet queries were performed. Firmware version remains 0.7.0-dev until runtime integration/acceptance; this document scopes the 0.8.0 milestone.

## Architecture assessment

ESP-IDF 6.1, ESP32, GCC 15.2. Root CMake substitutes project-local EMAC RX and ping patches. Ethernet initializes through esp_netif/esp_eth in app_main. Discovery runs in its own 4096-byte task, paced ARP plus four concurrent ICMP probes, with separate bounded UDP PTR hostname enrichment. The host table holds up to 254 discovery_device_t records: IPv4, MAC, vendor, hostname, ARP/ICMP state and measurement flags. Protected discovery_snapshot/discovery_device copies serve consumers. The HTTP server streams a fixed snapshot through /api/discovery; other read-only APIs expose switch/PROFINET information. Existing HTTP requests execute in one server task.

No SNMP library is enabled in sdkconfig or the component registration. lwIP's SNMP/MIB2 facilities concern an agent, not the required remote GET client. Existing socket/select, nonblocking fcntl, inet_pton/inet_ntop and connected UDP facilities suffice. No dependency download or lwIP configuration change is needed. The codec is small and isolated under main, following the project's separation of protocol parsing from networking.

Configured socket limit is 10; ping, PTR, HTTP and SNMP compete for this pool. Socket exhaustion returns failure and does not change discovery. One SNMP query must be scheduled at a time by the eventual worker, preferably outside discovery enrichment. Never call the synchronous API from HTTP handlers, lwIP callbacks or discovery's worker. Its maximum receive wait is 1500 ms plus scheduling/API overhead. Nonblocking IO and an absolute deadline prevent malformed traffic from extending the wait. This is not a hard real-time guarantee under CPU starvation.

Last existing size report: static DRAM 141817/180736 bytes, 38919 bytes remaining; this is not runtime free heap. Baseline app is 0x79940 (497,984) bytes with 0x12f6c0 bytes (71%) free in the 0x1a9000 application partition. Bootloader has 0x980 free and is unaffected.

## Added and modified files

| File | Purpose |
| --- | --- |
| main/syndetis_snmp.h | Public result, isolated defaults and version/selected-host APIs |
| main/syndetis_snmp.c | UDP transport, deadlines, retries, diagnostics, cleanup and selected discovery-record hook |
| main/snmp_codec.h | Private portable codec contract |
| main/snmp_codec.c | GET-only BER encoder and strict response parser |
| tests/verify_snmp.py | Production-body codec and mocked transport fixtures using target GCC constexpr evaluation |
| main/CMakeLists.txt | One added source-registration line for the two new C files |
| SNMP_FOUNDATION_0.8.0.md | Design, evidence and remaining acceptance work |

Build/test logs, snmp_test_results.json and snmp_git_diff_stat.txt are generated evidence. All other source modifications visible against Git predate this task. No existing discovery record layout, timing, ARP behavior, Ethernet pins/initialization, UI, sdkconfig, patches or version source was deliberately changed.

## Design and API

syndetis_snmp_query_basic uses v2c. syndetis_snmp_query_version explicitly selects v1 or v2c; there is no automatic version fallback. NULL community selects public; explicit communities are 1–64 bytes. This value is isolated in the header for later configuration. It is never logged. Caller owns the result; failures clear it and valid becomes true only after all six bindings validate.

syndetis_snmp_query_discovered selects one current host by index, copies its identity before network IO and calls the basic API. It does not extend or alter the discovery table. Its result belongs to the caller. A future UI worker must retain the selected IPv4/MAC identity and check it before attaching results because table indices can change on rediscovery. No caller, scanner, task, HTTP route or UI control is automatically installed.

Only numeric IPv4 addresses are accepted; zero/0.x, loopback and multicast/reserved ranges are rejected. Selected-host invocation uses an existing valid discovery record. The raw-IP API has no subnet-mask context and callers must not pass directed subnet broadcasts.

One connected UDP socket restricts replies to the requested peer and port 161. Both send and receive are nonblocking; select waits against an absolute monotonic deadline. Each attempt has 750 ms receive timeout, at most one retry (two GET sends). IO failures stop cleanly. Silent unsupported agents, unreachable peers without immediate errors and wrong-community silence time out. Protocol errors/malformed responses are discarded within the same deadline. Successful replies validate exact version, community, request ID, RESPONSE PDU, zero error status/index, unique known OIDs and expected ASN.1 types. No generic PDU writer or SET/GETNEXT/WALK API exists.

Definite BER lengths only, at most two length octets; all nested reads stay within their parent slice. Unknown, duplicate, missing and extra bindings are rejected. TimeTicks are bounded uint32; ObjectID arcs and formatted output are bounded. DisplayString values are bounded and truncated with termination; control/non-ASCII bytes become '?'. ObjectID overflow/overlength fails the entire result. All string output must still be escaped or inserted with textContent by a future UI.

## Supported objects

| Object | OID | Result |
| --- | --- | --- |
| sysDescr.0 | 1.3.6.1.2.1.1.1.0 | sys_descr[128] |
| sysObjectID.0 | 1.3.6.1.2.1.1.2.0 | sys_object_id[128], dotted decimal |
| sysUpTime.0 | 1.3.6.1.2.1.1.3.0 | sys_uptime_ticks, hundredths of a second |
| sysContact.0 | 1.3.6.1.2.1.1.4.0 | sys_contact[64] |
| sysName.0 | 1.3.6.1.2.1.1.5.0 | sys_name[64] |
| sysLocation.0 | 1.3.6.1.2.1.1.6.0 | sys_location[64] |

All six are requested in one GET. Partial/exception responses are not accepted as valid identity.

## Memory and flash

No large automatic packet arrays and no persistent SNMP table/task. Scratch allocates exactly 1729 bytes per call (256 request + 1473 receive); response cap is 1472, with an extra byte to detect oversize/truncated datagrams. The caller result is approximately 456 bytes including ABI padding. Socket and lwIP packet overhead are additional and depend on traffic. Small local codec arrays are at most 8 bytes; selected-host wrapper also copies one discovery_device_t and a 16-byte IPv4 buffer. Allocation failure returns ESP_ERR_NO_MEM. Every allocated scratch/socket is released on completion/failure.

Target object size: codec text/data 2136 bytes, transport text/data 1368 bytes, combined 3504; both have zero data/BSS according to target size tool. Once called, allow roughly 4–6 KB flash including referenced SDK functions and alignment, and a separately budgeted worker stack. Current binary remains 497984 bytes: unused functions are discarded by the linker because runtime integration is intentionally deferred. This is not evidence of zero flash cost when activated.

## Validation

Baseline normal idf.py build: PASS, snmp_baseline_build.log. Installed Python/toolchain execution required approved sandbox escalation after Windows application-control restrictions; no toolchain repair was performed. After-change normal build: PASS, snmp_build.log. Final normal build: snmp_build_final.log. New source compiled without compiler warnings/errors; SDK reconfiguration emitted existing dependency/Kconfig notices.

All 15 verify_*.py scripts and the Node Web UI suite passed; exit codes are in snmp_test_results.json. Includes discovery logic, fast/enhanced discovery, hostname, ICMP degradation, link-loss handling, autotest, switch parsers/APIs/UI, PROFINET and release checks. snmp_verify_snmp.log records the expanded SNMP suite.

SNMP fixtures evaluate production codec bodies using target GCC static assertions, with constexpr-compatible byte-copy/format adapters and equivalent failure exits. They cover valid v1/v2c, all six fields, multi-octet ObjectID, every truncated prefix, wrong request ID/community/version/PDU, malformed lengths, duplicate OIDs, invalid ticks, repeated decode, packet cap, short output buffer and six GET-only bindings. Production transport control flow is evaluated with mocked sockets/time, scoped storage replacing heap and equivalent cleanup counters: successful reply, silent non-agent, unreachable connect, wrong-community silence, malformed flood, OOM, socket failure, absent selected host and 50 repeated selected-host queries. Tests prove mock ownership/deadline behavior, not real lwIP interoperability or allocator stability.

Not performed: physical responding agent, physical non-agent/unreachable/wrong-community behavior, on-device repeated-query heap/stack measurements, real Ethernet initialization/discovery/UI operation under SNMP traffic. Existing software regressions passed but do not replace these hardware tests.

## Limitations and next step

The 0.8.0 user-visible milestone is not yet complete: the backend is callable but has no UI/runtime trigger. Adding UI safely requires a dedicated serialized worker, selected-identity retention, polling status/result endpoint and minimal details-panel action. This is deliberately deferred to preserve the current HTTP task's responsiveness and the discovery architecture. Existing /api/discovery plus discovery_device is the integration point. Do not place this synchronous call directly in discovery_json.

Next: implement that narrowly scoped worker/UI integration, then verify v1/v2c agents and all negative cases on hardware while measuring free/minimum heap and worker stack watermark before/after repeated queries. Retain the existing discovery regressions. Then mark firmware 0.8.0-dev. No interface walking, port statistics, SNMPv3, background scanning or remote writes are included.

## Final Git statistics

Full git diff --stat is saved in snmp_git_diff_stat.txt. At capture: 193 tracked files changed, 19015 insertions(+), 25400 deletions(-). This includes substantial pre-existing changes and generated tracked build artifacts; Git omits the newly added untracked SNMP files. This task's existing-source change is one source-registration line in main/CMakeLists.txt. The added files are listed above; nothing was staged or committed.

