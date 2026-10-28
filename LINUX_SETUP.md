# Linux setup — Syndetis Milestone 6

This is the Milestone 6 development application, version **0.5.2**, for ESP32 / WT32-ETH01. The validated 0.4.0 checkpoint/recovery bytes are preserved. **Do not flash the new layout over a deployed unit before the [migration checklist](docs/milestone6/README.md) is approved.** No Windows object files, installed tools or parent source tree are needed. Internet is needed for initial SDK/tool/component/browser installation.

## 1. OS prerequisites

Ubuntu/Debian:

```bash
sudo apt-get update
sudo apt-get install git wget curl flex bison gperf python3 python3-pip python3-venv \
  cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0 \
  build-essential xz-utils
```

Fedora:

```bash
sudo dnf install git wget curl flex bison gperf python3 python3-pip cmake \
  ninja-build ccache libffi-devel openssl-devel dfu-util libusbx \
  gcc gcc-c++ make xz
```

Use Python 3.10 or newer and CMake 3.22 or newer. SDK installation below creates its Python environment and installs its declared dependencies/constraints; do not copy the Windows virtual environment or install arbitrary global replacements.

## 2. Install the exact official SDK and ESP32 tools

These commands use Espressif's supported manual installation method, so the SDK revision is explicit. EIM is also supported upstream, but must select v6.1 and satisfy the same commit check.

```bash
mkdir -p "$HOME/esp"
git clone --branch v6.1 --single-branch --recursive \
  https://github.com/espressif/esp-idf.git "$HOME/esp/esp-idf-v6.1"
cd "$HOME/esp/esp-idf-v6.1"
test "$(git rev-parse HEAD)" = fff9895c82d744c7237be8847347bdd1b07c6643
./install.sh esp32
. ./export.sh
idf.py --version
```

Expect ESP-IDF v6.1. Activate `. "$HOME/esp/esp-idf-v6.1/export.sh"` in every new build terminal. Do not select a moving SDK branch, run an SDK update, or reuse the Windows tools installation. The SDK installer provides the pinned target compiler and Python requirements.

## 3. Clone the private checkpoint branch and build

Authenticate to GitHub using your normal credential manager or SSH setup. Keep tokens out of URLs and files.

```bash
git clone --branch main https://github.com/theo79/Syndetis.git
cd Syndetis
python3 scripts/verify_checksums.py --manifest recovery/SHA256SUMS.txt
idf.py -B build-m6 build
python3 scripts/verify_build.py build-m6
```

CMake requires the exact SDK commit and ESP32 target. It initializes `build/sdkconfig` from the reviewed complete `sdkconfig.pinned`; defaults alone are not relied on to recreate omitted settings. The effective configuration must match every recorded CONFIG option. The custom `partitions.csv` selects dual OTA slots and an unused reserved legacy NVS partition. `verify_build.py` checks the complete new layout, rollback/security settings, app size and bootloader fit. Old build directories retain their own generated sdkconfig; always use a fresh build-m6 directory when moving from 0.4.0.

The component manager downloads **espressif/lan87xx 1.1.0** using `main/idf_component.yml` and `dependencies.lock`. `managed_components/` is generated locally; it is not copied from the old project. Expected output: `build-m6/Syndetis.bin`, with two 1,966,080-byte application slots. Upload the raw application .bin without a signed trailer; no identity provisioning is required. Build timestamps, paths and ELF hashes can change binary bytes; compare configurations/layout/source and record your new hash rather than expecting byte identity.

For a second genuinely fresh build without deleting the first:

```bash
idf.py -B build-fresh build
python3 scripts/verify_build.py build-fresh
```

## 4. Host and browser regressions

The C tests use the normal host compiler `cc` (override with `CC` if needed), not the ESP32 cross compiler. Node.js **20+** is required; Node 24.x is suitable. One option is the official Node 24 binary, downloaded and checksum-verified without a system install:

```bash
case "$(uname -m)" in
  x86_64) node_arch=x64 ;;
  aarch64) node_arch=arm64 ;;
  *) echo 'Install Node 20+ for your architecture from nodejs.org'; exit 1 ;;
esac
mkdir -p "$HOME/Downloads/node-syndetis" "$HOME/.local/opt"
cd "$HOME/Downloads/node-syndetis"
node_base=https://nodejs.org/dist/latest-v24.x
curl -fSLo SHASUMS256.txt "$node_base/SHASUMS256.txt"
node_archive=$(awk -v a="linux-$node_arch.tar.xz" '$2 ~ a"$" {print $2; exit}' SHASUMS256.txt)
test -n "$node_archive"
curl -fSLO "$node_base/$node_archive"
grep " $node_archive$" SHASUMS256.txt | sha256sum -c -
tar -xJf "$node_archive" -C "$HOME/.local/opt"
export PATH="$HOME/.local/opt/${node_archive%.tar.xz}/bin:$PATH"
node --version
```

Return to your cloned `Syndetis` directory, then:

```bash
npm ci --ignore-scripts
npx playwright install --with-deps chromium
python3 scripts/run_tests.py
```

The runner builds twelve actual-C suites, runs signing/provisioning validation and JSON/dashboard Node tests, then existing dashboard and new update-page Playwright scenarios. Host Python needs the cryptography package (already provided by the SDK Python environment); use that interpreter if the OS Python lacks it. It chooses the bundled browser by default; set `BROWSER_CHANNEL=chrome` only when using an installed Google Chrome. `python3 scripts/run_tests.py --skip-browser` explicitly skips browser coverage and must not be reported as a complete run. Browser data are synthetic. Outputs stay under ignored `tmp/`.

## 5. Serial device and permissions

Compare devices before/after attaching the external USB-UART adapter:

```bash
ls -l /dev/serial/by-id/ 2>/dev/null || true
ls -l /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || true
PORT=/dev/ttyUSB0  # replace with the actual adapter; a by-id path is preferable
stat -c '%G %a %n' "$PORT"
```

On Ubuntu the device group is commonly `dialout`; other distributions can use another group. Use the actual group shown by `stat`, then log out and back in:

```bash
serial_group=$(stat -c '%G' "$PORT")
sudo usermod -aG "$serial_group" "$USER"
```

Do not solve serial permissions with world-writable device permissions or a permanently root build shell.

## 6. USB flash and serial logs — future explicit device operation

No device operation was performed during checkpoint packaging. Read the board wiring and manual boot steps in [RESTORE_USB.md](RESTORE_USB.md). Close other serial programs first. In the activated SDK shell and project directory:

```bash
idf.py -B build-m6 -p "$PORT" -b 115200 flash
# Release GPIO0 and reset for normal boot on a manually controlled adapter.
idf.py -B build-m6 -p "$PORT" monitor
```

Monitor baud is 115200; exit with Ctrl+]. To restore the exact saved 5B image without rebuilding, use RESTORE_USB.md instead. Firmware version alone cannot identify which 0.4.0 image is installed.

## 7. Continue development

Firmware 0.5.2 is feature-frozen, release tag v0.5.2. The user reports 9/9 ordinary hardware acceptance PASS and confirms final-candidate installation and passing post-flash smoke testing; the local artifact hash is verified. See docs/RELEASE_MANIFEST.md and docs/FINAL_FIELD_ACCEPTANCE.md. Destructive power-cut/rollback testing is not claimed. The historical 0.4.0 single-factory layout must not be treated as OTA-capable. Continuous Monitor remains deferred. Save/verify backups listed in docs/EXTERNAL_BACKUP_CHECKLIST.md before formatting Windows.

Sources checked for these instructions: [official v6.1 manual Linux setup](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/get-started/linux-macos-setup-legacy.html), [current Linux installation options](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/get-started/linux-setup.html), [serial permissions and connection](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/get-started/establish-serial-connection.html), [official Node downloads](https://nodejs.org/en/download).

For the pinned Mbed TLS host interoperability test and offline NVS generator test, see [Milestone 6 verification](docs/milestone6/README.md). SOURCE_SHA256SUMS.txt and docs/VERIFICATION.* describe the preserved 0.4.0 checkpoint, not the changed development sources.
