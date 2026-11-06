"""Check the CD layout and boot catalog independently of the ISO writer."""
from pathlib import Path
import struct

data = Path("jordyos.iso").read_bytes()
floppy = Path("os.img").read_bytes()
assert len(data) % 2048 == 0
primary = data[16*2048:17*2048]
assert primary[:7] == b"\x01CD001\x01"
assert struct.unpack_from("<I", primary, 80)[0] * 2048 == len(data)
assert struct.unpack_from(">I", primary, 84)[0] * 2048 == len(data)
boot = data[17*2048:18*2048]
assert boot[:7] == b"\x00CD001\x01"
assert boot[7:39].rstrip(b"\0") == b"EL TORITO SPECIFICATION"
offset = struct.unpack_from("<I", boot, 71)[0] * 2048
catalog = data[offset:offset+2048]
assert catalog[:2] == b"\x01\x00" and catalog[30:32] == b"\x55\xaa"
assert sum(struct.unpack("<16H", catalog[:32])) & 65535 == 0
assert catalog[32:34] == b"\x88\x02"
assert struct.unpack_from("<H", catalog, 38)[0] == 1
offset = struct.unpack_from("<I", catalog, 40)[0] * 2048
assert data[offset:offset+len(floppy)] == floppy
assert data[18*2048:18*2048+7] == b"\xffCD001\x01"
print("PASS ISO volume, El Torito catalog/checksum, floppy geometry and exact boot-image bytes")
