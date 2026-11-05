from pathlib import Path
import struct
boot = Path("bootloader.bin").read_bytes()
kernel = Path("kernel.bin").read_bytes()
elf = Path("kernel.elf").read_bytes()
if len(elf) < 64 or elf[:7] != b"\x7fELF\x02\x01\x01":
    raise ValueError("Kernel must be a little-endian ELF64 executable")
header = struct.unpack_from("<16sHHIQQQIHHHHHH", elf)
if header[1] != 2 or header[2] != 62 or header[4] != 0x10000:
    raise ValueError("Kernel must target x86-64 with entry point 0x10000")
assert len(boot) == 512 and boot[510:] == b"\x55\xaa"
assert len(kernel) <= 896 * 512
image = bytearray(1440 * 1024)
image[:512] = boot
image[512:512 + len(kernel)] = kernel
Path("os.img").write_bytes(image)
print(f"Kernel: {len(kernel)} bytes; floppy: {len(image)} bytes")
