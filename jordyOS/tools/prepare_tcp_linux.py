"""Build a pinned diskless Alpine TCP peer; no host mounts, credentials or packages.

Usage: python3 tools/prepare_tcp_linux.py --cache /tmp/jordyos-linux-assets --output /tmp/jordyos-linux
Downloads occur only in this preparation step; the test itself is offline.
"""
import argparse
import gzip
import hashlib
import shutil
import stat
import tarfile
import urllib.request
from pathlib import Path, PurePosixPath

ASSETS = [
    ("minirootfs.tar.gz",
     "https://dl-cdn.alpinelinux.org/alpine/v3.22/releases/aarch64/alpine-minirootfs-3.22.7-aarch64.tar.gz",
     "7e9b34ff3304381909e566e2c93de450664ccc724191261423974fc121c11623"),
    ("linux-virt.apk",
     "https://dl-cdn.alpinelinux.org/alpine/v3.22/main/aarch64/linux-virt-6.12.115-r0.apk",
     "445dd29708c1df38c6643e2d6e7b4ac86ff6ab1e26e14f6c512c1e64794b51ef"),
]

def members(path):
    result = {}
    with tarfile.open(path, ignore_zeros=True) as archive:
        for item in archive:
            name = str(PurePosixPath(item.name.removeprefix("./")))
            if name.startswith("/") or ".." in PurePosixPath(name).parts:
                raise ValueError("Unsafe archive path")
            if name == "." or name.startswith("."):
                continue
            if item.isdir():
                result[name] = (stat.S_IFDIR | item.mode, b"")
            elif item.issym():
                result[name] = (stat.S_IFLNK | item.mode, item.linkname.encode())
            elif item.isfile() or item.islnk():
                result[name] = (stat.S_IFREG | item.mode, archive.extractfile(item).read())
    return result

def cpio(out, name, mode, data, inode):
    name = name.encode() + b"\0"
    fields = [inode, mode, 0, 0, 1, 0, len(data), 0, 0, 0, 0, len(name), 0]
    header = b"070701" + "".join(f"{x:08x}" for x in fields).encode()
    out.write(header + name + b"\0" * (-(len(header) + len(name)) % 4))
    out.write(data + b"\0" * (-len(data) % 4))

def prepare(cache, output):
    cache.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True, exist_ok=True)
    for name, url, digest in ASSETS:
        path = cache / name
        if not path.exists():
            print(f"Downloading {url}", flush=True)
            with urllib.request.urlopen(url, timeout=60) as response, path.open("wb") as out:
                shutil.copyfileobj(response, out)
        if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise ValueError(f"Checksum mismatch: {path}")
    files = members(cache / "minirootfs.tar.gz")
    files.update(members(cache / "linux-virt.apk"))
    (output / "vmlinuz").write_bytes(files.pop("boot/vmlinuz-virt")[1])
    scripts = {
        "init": '''#!/bin/sh
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
mkdir -p /tmp
modprobe virtio_pci
modprobe virtio_net
ip link set lo up
ip addr add 192.0.2.1/24 dev eth0
ip link set eth0 up
while :; do nc -l -p 9000 -e /echo.sh; done &
while :; do nc -l -p 9001 -e /receive.sh; done &
while :; do nc -l -p 9002 -e /send.sh; done &
echo LINUX_TCP_READY
exec sh
''',
        "echo.sh": '''#!/bin/sh
cat > /tmp/received
echo LINUX_RX_SHA256 $(sha256sum /tmp/received) > /dev/console
cat /tmp/received
''',
        "receive.sh": '''#!/bin/sh
cat > /tmp/upload
echo LINUX_UPLOAD_SHA256 $(sha256sum /tmp/upload) > /dev/console
''',
        "send.sh": '''#!/bin/sh
cat /payload.bin
''',
    }
    for name, script in scripts.items():
        files[name] = (stat.S_IFREG | 0o755, script.encode())
    files["payload.bin"] = (stat.S_IFREG | 0o644,
        bytes((i * 37 + (i >> 8)) & 255 for i in range(16384)))
    with gzip.GzipFile(filename=str(output / "initramfs.cpio.gz"), mode="wb", mtime=0) as out:
        for index, (name, (mode, data)) in enumerate(sorted(files.items(),
                key=lambda entry: (entry[0].count("/"), entry[0])), 1):
            cpio(out, name, mode, data, index)
        cpio(out, "TRAILER!!!", 0, b"", len(files) + 1)
    print(f"Prepared Linux TCP peer: {output}", flush=True)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    prepare(args.cache, args.output)
