# Repository cleanup — 28 October 2026

No firmware behavior or version changed during cleanup. Current source remains **0.5.2** with the prior reliability corrections. No source, recovery asset, credential or private backup was deleted.

## Removed from the active project by archival

A private external archive at `/home/tanasto/Documents/Syndetis-archive-20261028-final/` holds:

| Original path | Disposition |
|---|---|
| `build-m6e-052-20261028/` | Previous generated build; 1,255 files moved and SHA-256 verified. |
| Earlier `tmp/` | 1,780 files moved and SHA-256 verified, including benchmark scratch/logs, old host/crypto builds, screenshots and previous final-field evidence. |
| Earlier `tmp/releases/` | Preserved inside that archive, including prior 0.5.1/0.5.2 raw images and manifests. |
| Earlier `tmp/m6b-device-backup/` and test-key material | Preserved privately inside archived tmp; excluded from Git. No device backup/key contents printed or republished. |
| Working tree before reconciliation | Complete tracked/untracked candidate-file snapshot in `working-tree-before-reconciliation.tar.gz`; also retained in a named local Git stash. |

The archive root is mode 0700. Per-directory SHA-256 inventories are beside the archived directories. Nothing was permanently deleted; no broad clean command was used. Historical records mentioning former tmp/build paths remain historical evidence, with this relocation record as their locator.

## Retained and excluded

- `main/`, CMake, partitions, pinned configuration and dependency locks: required firmware inputs.
- `scripts/`, `tests/`: actual regression/build checks, including historical signature/identity tools still exercised by tests; no unused-source deletion was guessed.
- `docs/`, historical source/checkpoint manifests, `recovery/`, `RESTORE_USB.md`: provenance and verified non-destructive recovery readiness. All 16 recovery files are byte-identical to the original Git checkpoint; 15 payload checksums pass.
- `node_modules/` and `managed_components/`: installed/generated dependencies retained for reproducible local development, ignored by Git.
- New `build-final/`, `tmp/` and local `releases/`: current generated build, test evidence and candidate binary; ignored rather than published to GitHub. Historical source checksum lists are not relabeled as current firmware manifests.
- Local environments, caches, logs, private configuration, keys/certificates, provisioning/device identities and backup folders: ignored. Candidate content was scanned for private-key blocks and token/access-key patterns. The existing tracked prototype hotspot default is product configuration already present in history; no per-device credentials were added.

Uniform executable bits inherited by tracked data/source/recovery files were restored to their original Git modes without changing contents. No recovery binary bytes changed.

## Git reconciliation

Fetched private SSH `origin` and reviewed incoming commit `d8934dbe8729c68c76cca896673b9a1e8424684f` (0.5.1 OTA checkpoint). Local `main` was fast-forwarded from `260afc42e999845c2d670c11ce81e117920ff4ad`; no history was rewritten. Remote ignore protections and hardware-history updates were retained. Reviewed local 0.5.2 version, reliability source/tests and new field documents were restored from the protected snapshot. Current README/acceptance/manifest supersede historical milestone status statements.

## Release closure and retained limitations

The earlier image-identity/tag deferral was closed by the user's subsequent confirmation that the final v0.5.2 candidate was installed and passed post-flash smoke testing, together with local artifact hash verification. Annotated release tag: v0.5.2. No further hardware operation was performed by this agent. Noncritical OTA maintenance limitations remain documented; no new development milestone or features were introduced. Current generated outputs can be regenerated, but private historical backups and old firmware images must remain in the external archive.
