"""Read-only verification of generated release artifacts; never connects to hardware."""
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"


def c_string(raw):
    return raw.split(b"\0", 1)[0].decode("ascii")


def verify():
    description = json.loads((BUILD / "project_description.json").read_text())
    flash = json.loads((BUILD / "flasher_args.json").read_text())
    version = (ROOT / "version.txt").read_text().strip()
    assert description["target"] == "esp32"
    assert description["project_version"] == version
    assert flash["flash_settings"]["flash_size"] == "4MB"
    table = (BUILD / "partition_table/partition-table.bin").read_bytes()
    entries = {}
    prior_end = 0x9000
    for offset in range(0, len(table), 32):
        record = table[offset:offset + 32]
        magic, = struct.unpack_from("<H", record)
        if magic == 0xEBEB:
            assert record[16:] == hashlib.md5(table[:offset]).digest(), "Partition MD5"
            break
        assert magic == 0x50AA, "Partition entry magic"
        _, kind, subtype, address, size, label, flags = struct.unpack("<HBBII16sI", record)
        name = c_string(label)
        assert address >= prior_end and address + size <= 4 * 1024 * 1024, "Partition overlap/flash overflow"
        prior_end = address + size
        entries[name] = (kind, subtype, address, size)
    else:
        raise AssertionError("Missing partition MD5 record")
    assert entries["otadata"] == (1, 0, 0xF000, 0x2000)
    assert entries["ota_0"] == (0, 0x10, 0x20000, 1700 * 1024)
    assert entries["ota_1"] == (0, 0x11, 0x1D0000, 1700 * 1024)
    assert "factory" not in entries
    assert (BUILD / "ota_data_initial.bin").read_bytes() == b"\xff" * 8192
    assert flash["app"]["offset"] == "0x20000"
    assert flash["otadata"]["offset"] == "0xf000"
    image = (BUILD / "syndetis_finder.bin").read_bytes()
    assert image[0] == 0xE9 and struct.unpack_from("<H", image, 12)[0] == 0, "ESP32 image"
    assert struct.unpack_from("<I", image, 32)[0] == 0xABCD5432, "App descriptor"
    assert c_string(image[48:80]) == version
    assert c_string(image[80:112]) == "syndetis_finder"
    assert c_string(image[144:176]).startswith("v6.1"), "ESP-IDF version"
    assert len(image) <= entries["ota_0"][3] and len(image) <= entries["ota_1"][3]
    # Independently verify every segment, ESP checksum, appended SHA256 and no trailing data.
    cursor, checksum = 24, 0xEF
    for _ in range(image[1]):
        _, length = struct.unpack_from("<II", image, cursor)
        cursor += 8
        payload = image[cursor:cursor + length]
        assert len(payload) == length
        for byte in payload:
            checksum ^= byte
        cursor += length
    checksum_at = (cursor // 16 + 1) * 16 - 1
    assert image[checksum_at] == checksum, "Image checksum"
    assert image[23] == 1, "Appended image SHA256 required"
    end = checksum_at + 1
    assert len(image) == end + 32, "Truncated image or unexpected trailing bytes"
    assert image[end:] == hashlib.sha256(image[:end]).digest(), "Appended image SHA256"
    bootloader = (BUILD / "bootloader/bootloader.bin").read_bytes()
    assert len(bootloader) <= 0x7000, "Bootloader must fit below partition table"
    print(f"PASS: ESP32 Syndetis {version}, ESP-IDF 6.1, {len(image):,} bytes, valid checksum/SHA256.")
    print("PASS: 4 MB flash, partition MD5/bounds, two 1700 KiB OTA slots, serial otadata initialization.")
    for name, (_, _, address, size) in entries.items():
        print(f"{name:10} offset=0x{address:06x} size=0x{size:06x} ({size:,} bytes)")
    print(f"Application SHA256: {hashlib.sha256(image).hexdigest()}")


if __name__ == "__main__":
    verify()
