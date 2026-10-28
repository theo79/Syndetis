# v0.5.2 — historical Ethernet OTA test-build procedure

Current candidate and acceptance identity status: [release manifest](../RELEASE_MANIFEST.md). Historical tmp/build paths below were moved to the external archive recorded in [cleanup](../REPOSITORY_CLEANUP.md); do not treat this earlier image as the current rebuilt candidate.

Version-only release: `CMakeLists.txt` sets `PROJECT_VER` to `0.5.2`.
Ethernet OTA and physical authorization already exist in v0.5.1.
The operator reports v0.5.1 Wi-Fi OTA successfully deployed and tested;
v0.5.2 Ethernet hardware acceptance remains pending.

## Build the raw application

From a Bash terminal on this workstation:

```bash
cd /home/tanasto/Documents/Syndetis
source /home/tanasto/.espressif/tools/activate_idf_v6.1.sh
idf.py -B build-m6e-052-20261028 build
python3 scripts/verify_build.py build-m6e-052-20261028
```

The EIM activation script selects the installed v6.1 Python/toolchain.
CMake enforces SDK commit `fff9895c82d744c7237be8847347bdd1b07c6643`.
A fresh build directory initializes its configuration from `sdkconfig.pinned`.
Keep the product name `Syndetis`, partition layout and configuration unchanged.
The raw OTA input is `build-m6e-052-20261028/Syndetis.bin`.
The staged copy is `tmp/releases/v0.5.2/Syndetis-v0.5.2.bin`, accompanied by
`manifest.json` and `SHA256SUMS.txt`. Verify it with:

```bash
cd /home/tanasto/Documents/Syndetis/tmp/releases/v0.5.2
sha256sum -c SHA256SUMS.txt
```

Upload only the raw application, without a signature trailer. Bootloader,
partition-table, otadata and merged flash images are not OTA inputs.
Keep `tmp/releases/v0.5.1/` and `tmp/m6b-device-backup/` for recovery/reference.

## Physical Ethernet test

1. Confirm the device runs v0.5.1. Connect Ethernet and obtain its current
   DHCP address or deliberately configure Manual IPv4. Put the laptop on the
   same subnet and stop active diagnostics before requesting update mode.
2. Use UP/DOWN and OK to select **Settings → Firmware Update** on the OLED.
   Physical selection opens the temporary three-minute authorization window;
   v0.5.1 requires no one-time code. Keep stable power throughout the update.
3. Open `http://<device-ip>:8080/update` using the actual wired IPv4 address.
   Select `Syndetis-v0.5.2.bin` and upload during that physical session.
4. Observe Preparing, upload progress, Verifying, completion and reboot.
   After reboot confirm **Device Info reports 0.5.2**, allow at least 45 seconds
   of normal operation, and check Ethernet diagnostics and OLED/buttons.
   A normal restart should still report 0.5.2.
5. Confirm TCP/8080 closes outside the authorized session and the ordinary
   port-80 dashboard remains blocked on Ethernet. Re-uploading 0.5.2 must be
   rejected by the strictly-newer-version rule.

Record actual hardware observations separately from build verification.
