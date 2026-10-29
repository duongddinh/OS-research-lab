from pathlib import Path
boot = Path("bootloader.bin").read_bytes()
kernel = Path("kernel.bin").read_bytes()
assert len(boot) == 512 and boot[510:] == b"\x55\xaa"
assert len(kernel) <= 896 * 512
image = bytearray(1440 * 1024)
image[:512] = boot
image[512:512 + len(kernel)] = kernel
Path("os.img").write_bytes(image)
print(f"Kernel: {len(kernel)} bytes; floppy: {len(image)} bytes")
