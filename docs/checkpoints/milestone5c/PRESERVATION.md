# Milestone 5C evidence preservation

Source: `origin/syndetis-5c`, commit `ef1d50fc44045127b71dd721be4570fb72924bb2`, directory `Syndetis5C/`.

The checkpoint README, migration manifest and original checksum manifest in this directory are preserved byte for byte. The active project README and migration manifest remain unchanged. The historical verification records are available at `docs/VERIFICATION.md` and `docs/VERIFICATION.json` from the repository root; their build hash, browser results and `linux_execution: false` describe the original packaging session, not the subsequent Ubuntu audit.

The root `SOURCE_SHA256SUMS.txt` retains all 112 original checkpoint hashes. Only the README and migration-manifest paths are redirected to their archived copies here, so the existing verifier works without replacing the active documentation:

```bash
python3 scripts/verify_checksums.py
```

The original manifest alongside this note applies to the original standalone checkpoint layout in Git history. It is not a manifest of this archive directory alone. Unchanged firmware, configuration, tests and recovery binaries are referenced in place rather than duplicated.

The later Ubuntu recovery audit built firmware 0.4.0 with EIM/ESP-IDF v6.1: 905,664 application bytes, 142,912 bytes free in the 1,048,576-byte factory partition. Effective configuration and partition table matched the accepted checkpoint. C/Node regressions passed; Playwright browser tests were not performed in that audit. No new hardware validation is claimed.
