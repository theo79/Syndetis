# Final field acceptance

**Hardware acceptance: 9/9 PASS — user-reported.**

Test date: **user-reported / date not recorded**. Tester: user; name not supplied.
Board identifier, installed firmware version, binary SHA-256 and measured values: **not supplied**.

| # | User-reported test | Result |
|---|---|---|
| 1 | Boot, OLED and three-button operation | PASS |
| 2 | Ethernet link and cable disconnect/reconnect | PASS |
| 3 | LLDP/CDP discovery | PASS |
| 4 | DHCP start, stop and failure | PASS |
| 5 | Manual IPv4 and address-conflict detection | PASS |
| 6 | Gateway and custom IPv4 Ping | PASS |
| 7 | Repeated diagnostic operations across two ports | PASS |
| 8 | Ordinary OTA maintenance | PASS |
| 9 | USB recovery readiness, non-destructive | PASS |

These results record the user's completed session. No hardware operation was performed by the repository-cleanup agent. No additional timing, repeat count, switch model, port speed, memory margin, power-cut/rollback test or flash readback is inferred.

## Final image identity gate

The tested device has not been tied to the final candidate binary by an exact hash or equivalent evidence. A matching version string alone is insufficient. See [release manifest](RELEASE_MANIFEST.md) for the candidate hash and source commit.

**RELEASE CANDIDATE — FINAL HARDWARE IMAGE CONFIRMATION PENDING.**

Update recommendation: **UNDETERMINED**. Do not flash automatically. If subsequent evidence shows the exact candidate is installed, record it and select NO FLASH NEEDED. If an older/different build is established, select FINAL FLASH REQUIRED and arrange a separately authorized update plus post-update boot/OLED/buttons/link/diagnostics smoke check. The current strictly-newer-version OTA rule rejects a different image carrying the same version; do not bypass that rule or silently change partition/boot state.

Final release tag remains deferred until exact image identity and any required post-update smoke test are confirmed.
