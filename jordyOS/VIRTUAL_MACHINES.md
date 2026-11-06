# Booting the JordyOS ISO

JordyOS is an x86-64 operating system with a legacy BIOS bootstrap, VGA
graphics and PS/2 keyboard input. It can be packaged as a bootable live CD:

```sh
git submodule update --init --recursive
nix develop
make -C jordyOS iso
make -C jordyOS run-iso
```

The file is `jordyOS/jordyos.iso`. It boots the same kernel and applications
as `os.img`; `tools/iso.py` wraps that exact image in an ISO 9660 filesystem
with an El Torito 1.44 MiB floppy-emulation boot entry. The BIOS provides the
loader's existing disk interface while reading from the optical drive. Only
the pinned Python toolchain is needed; no extra ISO authoring package or
machine-local installation is required. Output is deterministic for the same
input image. The binary is a build artifact and is not committed to Git.

This is a live RAM-only system. No hard disk or installation is needed, and
RAM files disappear on reboot. The ISO is intended to be attached as a
virtual CD/DVD; it is not a hybrid USB image or a UEFI boot image.

## Host and hypervisor compatibility

| Hypervisor / host | Status |
| --- | --- |
| QEMU x86 emulation, including an Apple Silicon host | CD boot and application smoke suite verified |
| VirtualBox on Intel/AMD x86-64 | Expected with the BIOS settings below; not yet verified |
| VMware on Intel/AMD x86-64 | Expected with legacy BIOS and compatible VGA/input; not yet verified |
| VirtualBox or VMware Fusion on Apple Silicon | Cannot run this x86-64 guest |

VirtualBox requires Arm guests on an Arm host; see its
[Arm host limitations](https://docs.oracle.com/en/virtualization/virtualbox/7.2/user/installation.html).
VMware documents the same architecture restriction for
[Fusion on Apple Silicon](https://knowledge.broadcom.com/external/article/315602).
An ISO changes the boot medium, not the CPU architecture. Use QEMU's x86
emulation on Apple Silicon.

## VirtualBox on an Intel/AMD machine

Create a VM with these settings; the UI labels may vary by release:

- Guest OS: Other, 64-bit; skip unattended installation.
- CPU: one x86-64 CPU with SSE2 and NX available; enable PAE/NX.
- RAM: 64 MiB or more (the kernel currently maps/uses the first 32 MiB).
- Firmware: BIOS; disable EFI. Use the PIIX3 chipset.
- Display: legacy VGA compatibility, such as VBoxVGA, 16 MiB video memory,
  and 3D acceleration disabled.
- Storage: attach `jordyos.iso` as a CD/DVD, preferably on an IDE controller;
  put Optical first in the boot order. A virtual hard disk is optional.
- Input: retain PS/2 keyboard emulation.
- Network: disable adapters for the desktop/application check.

Start the VM. The desktop should appear. Select Shell (4) and try `arch`,
`isolation test`, `larp test`, `ape demo.ape`, and `desktop`.

For VMware on an Intel/AMD host, use an Other 64-bit guest, legacy BIOS
firmware, one CPU, at least 64 MiB RAM, legacy VGA/PS2 compatibility and the
ISO connected to a CD/DVD at power-on. This configuration has not yet been
tested; no Guest Additions or VMware Tools integration is supplied.

## Networking and verification

The existing driver supports RTL8139 only. VirtualBox's
[available network hardware](https://docs.oracle.com/en/virtualization/virtualbox/7.2/user/networkingdetails.html)
does not include RTL8139. Its default PCNet, Intel and virtio devices need
additional JordyOS drivers. Booting the ISO does not add those drivers.
Continue using QEMU's RTL8139 lab for TCP/LARP and eBPF network tests.

```sh
make -C jordyOS test-iso
```

This checks the ISO descriptors, boot catalog checksum and embedded image,
then boots with a CD/DVD and no floppy attached. The full existing shell/app
smoke suite runs from that CD boot, including CPU/ABI checks, LARP status and
self-test, compiler/bytecode/REPL, Calculator, Editor, Snake and desktop
launchers. The transcript is `/tmp/jordyos-iso-smoke.log`. This verifies QEMU
CD boot; it does not certify VirtualBox or VMware compatibility.

Format references: [El Torito specification](https://www.ardent-tool.com/storage/specscdrom.pdf)
and [ECMA-119 / ISO 9660](https://ecma-international.org/publications-and-standards/standards/ecma-119/).
