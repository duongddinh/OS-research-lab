"""Deterministic ISO 9660 / El Torito CD with a 1.44 MiB floppy boot image.

This intentionally supports one fixed layout, not a general ISO authoring API.
The BIOS exposes the embedded image through the loader's existing INT 13 CHS
interface. No bootloader changes, additional packages, UEFI or USB boot support.
"""
from pathlib import Path
import struct
import sys

BLOCK = 2048
CATALOG = 22
BOOT = 23
DATE = bytes([100, 1, 1, 0, 0, 0, 0])  # 2000-01-01 UTC, reproducible

def both16(value):
    return struct.pack("<H", value) + struct.pack(">H", value)

def both32(value):
    return struct.pack("<I", value) + struct.pack(">I", value)

def record(name, extent, size, directory=False):
    entry = bytearray(33 + len(name) + (len(name) % 2 == 0))
    entry[0] = len(entry)
    entry[2:10] = both32(extent)
    entry[10:18] = both32(size)
    entry[18:25] = DATE
    entry[25] = 2 if directory else 0
    entry[28:32] = both16(1)
    entry[32] = len(name)
    entry[33:33+len(name)] = name
    return entry

def descriptor(kind):
    data = bytearray(BLOCK)
    data[:7] = bytes([kind]) + b"CD001\x01"
    return data

def build(floppy):
    if len(floppy) != 1440 * 1024 or floppy[510:512] != b"\x55\xaa":
        raise ValueError("Expected a bootable 1.44 MiB os.img")
    readme = (b"JordyOS x86-64 live CD\r\n"
        b"Boot with legacy BIOS on an x86-64 CPU with SSE2 and NX.\r\n"
        b"This is a RAM-only live system; no installer or persistent disk.\r\n"
        b"See https://github.com/duongddinh/OS-research-lab for source and settings.\r\n")
    readme_block = BOOT + len(floppy) // BLOCK
    blocks = readme_block + 1
    sectors = [bytearray(BLOCK) for _ in range(blocks)]
    primary = descriptor(1)
    primary[8:40] = b"JORDYOS".ljust(32, b" ")
    primary[40:72] = b"JORDYOS".ljust(32, b" ")
    primary[80:88] = both32(blocks)
    primary[120:124] = both16(1)
    primary[124:128] = both16(1)
    primary[128:132] = both16(BLOCK)
    primary[132:140] = both32(10)
    struct.pack_into("<I", primary, 140, 19)
    struct.pack_into(">I", primary, 148, 20)
    primary[156:190] = record(b"\0", 21, BLOCK, True)
    primary[190:813] = b" " * (813-190)
    stamp = b"2000010100000000\0"
    primary[813:830] = stamp
    primary[830:847] = stamp
    primary[847:864] = b"0" * 16 + b"\0"
    primary[864:881] = stamp
    primary[881] = 1
    sectors[16] = primary
    boot_record = descriptor(0)
    boot_record[7:39] = b"EL TORITO SPECIFICATION".ljust(32, b"\0")
    struct.pack_into("<I", boot_record, 71, CATALOG)
    sectors[17] = boot_record
    sectors[18] = descriptor(255)
    sectors[19][:10] = struct.pack("<BBIHBB", 1, 0, 21, 1, 0, 0)
    sectors[20][:10] = struct.pack(">BBIHBB", 1, 0, 21, 1, 0, 0)
    entries = [record(b"\0", 21, BLOCK, True), record(b"\1", 21, BLOCK, True),
        record(b"BOOT.CAT;1", CATALOG, BLOCK), record(b"BOOT.IMG;1", BOOT, len(floppy)),
        record(b"README.TXT;1", readme_block, len(readme))]
    root = b"".join(entries)
    sectors[21][:len(root)] = root
    catalog = sectors[CATALOG]
    catalog[0] = 1  # Validation header, x86 platform ID 0
    catalog[4:28] = b"JordyOS".ljust(24, b"\0")
    catalog[30:32] = b"\x55\xaa"
    checksum = -sum(struct.unpack("<16H", catalog[:32])) & 65535
    struct.pack_into("<H", catalog, 28, checksum)
    catalog[32] = 0x88  # Bootable initial entry
    catalog[33] = 2     # 1.44 MiB floppy emulation
    struct.pack_into("<H", catalog, 38, 1)  # Initial 512-byte sector at 0000:7c00
    struct.pack_into("<I", catalog, 40, BOOT)
    for i in range(len(floppy) // BLOCK):
        sectors[BOOT+i][:] = floppy[i*BLOCK:(i+1)*BLOCK]
    sectors[readme_block][:len(readme)] = readme
    return b"".join(sectors)

if __name__ == "__main__":
    source, destination = map(Path, sys.argv[1:])
    destination.write_bytes(build(source.read_bytes()))
    print(f"BIOS bootable ISO: {destination} ({destination.stat().st_size} bytes)")
