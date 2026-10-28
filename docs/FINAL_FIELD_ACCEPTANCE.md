# Final field acceptance

**Hardware acceptance: 9/9 PASS — user-reported.**

Test date: **user-reported / date not recorded**. Tester: user; name not supplied.
Installed firmware: **final Syndetis v0.5.2 candidate, confirmed by the user**. Board identifier and measured values: **not supplied**.

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

## Final image confirmation and post-flash smoke test

User confirmation recorded during release finalization on **28 October 2026** (actual installation/test date not supplied):

> The final Syndetis v0.5.2 candidate was installed and passed post-flash hardware smoke testing.

The user identifies the previously prepared final candidate as the installed/tested artifact. Its local raw application was rehashed and matched the preserved build output byte for byte:

- Version/product: **0.5.2 / Syndetis**.
- Binary size: **933,440 bytes**.
- SHA-256: `5e2a684cdda639eb8c34b97881540bbc0d963775d516135c4317b44438e10fb3`.
- Verified firmware source commit: `53a0178293ef8e4769a7abeaeb9ca71582471799`.
- Final candidate installation: **PASS, user-confirmed**.
- Post-flash hardware smoke testing: **PASS, user-reported**; individual steps/measurements were not supplied.
- Independent device flash readback: **not performed by this agent**. Artifact identity is linked to hardware by the user's explicit final-candidate confirmation, not by a newly measured device hash.

**Final hardware image confirmation gate: CLOSED on user evidence plus local artifact hash verification.** Earlier 9/9 acceptance remains as reported above. No destructive recovery, power-cut testing or unreported observations are inferred.

Update recommendation: **NO FLASH NEEDED** for the confirmed installed final candidate. No further update, flash, erase, reset or provisioning was performed during release finalization. Annotated release tag: **v0.5.2**; see [release manifest](RELEASE_MANIFEST.md).
