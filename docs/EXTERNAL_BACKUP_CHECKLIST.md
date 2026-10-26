# Before formatting Windows — independent backup is still required

The GitHub branch preserves this portable project and selected recovery bins. It is not a backup of all local history, field evidence, toolchains or uncommitted working-tree context. No external drive/cloud backup destination was supplied in this task. No claim is made that an off-laptop copy exists or has been verified.

## Inventory

| Item | Current local location, relative to the original workspace | In the new GitHub checkpoint? | Required independent action |
| --- | --- | --- | --- |
| Complete original v0.4.0 preservation package | `../Syndetis-preservation-20261026T133551Z/` | No; only selected recovery bins are included | Copy the entire directory off this laptop; retain SHA256SUMS and verifier |
| A–G Git bundles and source archives | External package `history/` | No | Copy both bundles and ZIPs; verify bundle objects and ZIP/checksum integrity |
| Original pre-4.0 source/build/evidence snapshots | External package `workspace-snapshot/` | No | Preserve complete package, including original metadata/provenance |
| Original Git metadata and preservation refs | External package `git-administration-copy/`; live original `.git/` | No | Preserve the external package and a new current Git bundle; custom local refs are not guaranteed by a normal clone |
| Complete v0.3.0 recovery | `tmp/firmware-checkpoints/v0.3.0-8117bcb-verified/` | Three binaries and flash metadata only | Copy full original package, ELF, config, logs, manifest and provenance |
| Unverified pre-M4 artifacts | `tmp/firmware-checkpoints/pre-m4-existing-unverified/` | No | Copy with its unverified label if historical recovery evidence is wanted |
| Accepted 5A build | `build-m5a-20261026/` | No | Copy full build if its ELF/metadata/logs are required |
| Accepted 5B source and binary | Current main/tests/config; `build-m5b-20261026/` | Firmware source and selected original binaries yes | Also copy original full build/ELF/config/logs and the pre-5B snapshot for forensic provenance |
| Original unsanitized validation and field details | Original README/VALIDATION and ignored logs | No; portable docs are redacted | Copy privately; do not upload customer details without review |
| Hardware photographs/captures | No authoritative collection was identified in the inspected project; synthetic dashboard previews are not hardware photos | No | Locate originals in Downloads, Pictures, phone/cloud/other chats and explicitly back them up |
| New portable source/recovery ZIP | Created under original `tmp/` after publication; companion checksum reported in delivery | Not as a ZIP; contents are on the branch | Copy ZIP plus checksum to external storage and verify on the destination |

The private GitHub default branch/tag remain unchanged; do not assume local historical preservation refs were pushed. A clean clone of syndetis-5c recovers the checkpoint, not every item above.

## Copy and verify

Copy the external preservation package to a separate physical drive or independent storage. On that destination, run its `verify-package.py`; require all manifest entries to pass. Open several evidence files and confirm both Git bundles are present. For each bundle, use `git bundle verify` in a repository and import into a separate bare repository followed by `git fsck --full --strict` if validating its full object payload.

For the portable ZIP and other independent archives, retain companion SHA256 text files. Linux: `sha256sum -c FILE.sha256`. PowerShell: `Get-FileHash -Algorithm SHA256 -LiteralPath FILE.zip` and compare the complete digest. Extract the portable ZIP into a new directory and run `python3 scripts/verify_checksums.py` inside Syndetis5C.

For local folders without an existing manifest, produce a relative-path SHA256 manifest before copying and verify every listed file after copying. Preserve original manifests; do not rewrite them merely to hide copy errors. Record destination, date, counts and verification results. A second folder on the same laptop is not protection against formatting.

Do not format until the off-laptop copies, GitHub clone access, saved recovery binaries and any irreplaceable hardware evidence have actually been checked. This checklist does not perform deletion or formatting.
