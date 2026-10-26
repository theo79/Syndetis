# USB restore — saved firmware images

No restore or flash was performed during packaging. Use 3.3 V UART logic, adapter TX to RX0, RX to TX0 and common ground. Supply adequate board power. Hold GPIO0 LOW while resetting/powering the board for the ROM loader. After programming, release GPIO0 and reset for normal operation. GPIO16 is the oscillator enable, not PHY reset; do not change the GPIO mapping.

## Choose the intended image

| Directory | Provenance |
| --- | --- |
| `recovery/milestone5b` | Original saved 5B build, 905,792 bytes, application 0.4.0. Owner reports 5A/5B hardware validation passed; no per-device hash/readback was independently supplied during packaging. |
| `recovery/v0.4.0-saved` | Earlier DHCP-only saved flashed 0.4.0 artifacts; successful historical flash evidence retained in the external preservation package. Not the 5B firmware. |
| `recovery/v0.3.0` | Verified reproduction from tagged commit 8117bcbc8f41ae9a2197daa16c71dd1535cb7616. Not asserted byte-identical to a historical device image. |

`recovery/SHA256SUMS.txt` records every packaged recovery file. The portable fresh build is a separate reproduction with its own hash; it does not replace or relabel these original saved bytes.

## Verify before use

From the checkpoint root, Python 3.10+:

```text
python scripts/verify_checksums.py --manifest recovery/SHA256SUMS.txt
```

Use `python3` on Linux if that is the interpreter name. Require every checksum to pass.

## Exact flash layout (all supplied sets)

| Binary | Flash offset |
| --- | --- |
| bootloader.bin | 0x1000 |
| partition-table.bin | 0x8000 |
| Syndetis.bin | 0x10000 |

ESP32; DIO; 40 MHz flash; 4 MB physical flash. Partitions: NVS 0x9000/0x6000, PHY init 0xF000/0x1000, one factory application 0x10000/0x100000. No OTA slots. Do not substitute the old A–G partition table.

These files are firmware recovery, not a complete flash/NVS/PHY-data dump. They cannot restore previously lost device-specific storage. No full-chip erase is required by this procedure.

## Linux — saved 5B image, no rebuild

Activate the pinned SDK environment, choose the actual serial device and enter the desired recovery directory:

```bash
cd recovery/milestone5b
PORT=/dev/ttyUSB0  # actual adapter, preferably /dev/serial/by-id/...
python -m esptool --chip esp32 --port "$PORT" --baud 115200 \
  --before default-reset --after hard-reset write-flash @flash_args
```

## Windows — saved 5B image, no rebuild

In the activated ESP-IDF PowerShell, from the checkpoint root:

```powershell
Set-Location recovery/milestone5b
$SyndetisPort = 'COM_PORT' # replace with the detected adapter port
python -m esptool --chip esp32 --port $SyndetisPort --baud 115200 --before default-reset --after hard-reset write-flash "@flash_args"
```

Require esptool's written-data verification to succeed. Release GPIO0 and reset, then verify boot, link/discovery, boot-off Ethernet IPv4/Web Access and expected Manual/Ping workflows. Version 0.4.0 alone does not prove image identity. The generic commands deliberately require a selected port and do not flash automatically from a test/build script.
