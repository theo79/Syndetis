# GitHub preparation

This directory contains a source snapshot of Syndetis. No Git history, build
outputs, generated validation logs, or firmware binaries are included.
Source, configuration, tests, development reports, project-local SDK patches,
and the checked-in LAN87xx component (including its license) are preserved.

## Build

Use ESP-IDF 6.1.0 with the ESP32 target. Activate your installed ESP-IDF shell,
then run these commands from this directory:

```sh
idf.py build
idf.py partition-table
python tests/verify_release.py
node tests/web_ui.test.cjs
node tests/snmp_runtime_ui.test.cjs
```

`dependencies.lock` records espressif/lan87xx 1.1.0. Read `patches/README.md`
before changing SDK versions. Most Python verification scripts require a built
project and its configured compiler. Some tests also contain machine-specific
SDK/toolchain paths; inspect those paths before running on another machine.

The firmware version is taken from `version.txt` (0.7.0-dev in this snapshot).
The reports with 0.8.0 in their names do not change that version. Existing README
milestone descriptions and validation reports are historical claims; this
packaging task did not run a firmware build or a hardware test.

## Upload to GitHub

Create an empty GitHub repository named Syndetis. Open a terminal in this
`Github_Syndetis` directory and run:

```sh
git init -b main
git add .
git commit -m "Initial Syndetis source snapshot"
git remote add origin https://github.com/YOUR_USERNAME/Syndetis.git
git push -u origin main
```

Replace YOUR_USERNAME with the repository owner. This preparation did not create
a remote repository or upload files. No license for the original Syndetis code
has been chosen; upstream license notices remain in their respective files.
