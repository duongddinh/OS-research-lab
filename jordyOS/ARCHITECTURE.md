# JordyOS x86-64 architecture

The C kernel, Apelang runtime, shell, desktop and eBPF interpreter compile
to native **x86-64** code. The application runtime is a separate user ELF;
Calculator/Editor/REPL/Snake/History execute in ring 3 with private page tables.
The shell, desktop, drivers and eBPF hooks stay in ring 0. The BIOS loader still begins in 16-bit real mode;
the brief 32-bit stage establishes long mode before any C code runs. The build
produces an ELF64 x86-64 executable, extracts its raw load image, and places
that image after the existing BIOS boot sector in `os.img`.

## Boot stages

| Stage | Work |
| --- | --- |
| `bootloader.asm`, 16-bit | Select VGA mode/font, read the raw kernel at physical `0x10000`, jump to its entry |
| `kernel_entry.asm`, 16-bit | Enable A20, load GDT, enter protected mode |
| `kernel_entry.asm`, 32-bit | Check CPUID/MSR/PAE/SSE2/long-mode support, clear/build page tables, enable PAE/SSE state, set CR3 and EFER.LME, enable paging |
| `kernel_entry.asm`, 64-bit | Load the 64-bit code selector, align RSP, initialize x87/MXCSR, clear BSS, call `kmain` |
| `kmain` and tasks, 64-bit | Validate the live mode, initialize services, start the cooperative shell/desktop |

The GDT retains a 32-bit bootstrap code selector `0x08` and data selector
`0x10`; selector `0x18` is the 64-bit code descriptor (L=1, D=0). Paging and
EFER.LME activate IA-32e mode, then the far jump loads the 64-bit descriptor.
The startup checks MSR/PAE/FXSR/SSE/SSE2, NX and long-mode capability before using
those facilities. Unsupported CPUs print a serial error and halt. There is
no 32-bit fallback kernel or requirement for UEFI/GRUB/a host Linux kernel.

## Paging and physical layout

Long mode requires paging. The bootstrap installs a single shared, supervisor
identity map: virtual address equals physical address for the first **32 MiB**.
PML4[0] points to one PDPT entry, which points to a page directory containing
sixteen 2 MiB leaf mappings. All remaining entries are zero.

| Address | Purpose |
| --- | --- |
| `0x500` | BIOS-supplied 32-bit physical pointer to the ROM font; widened before C pointer use |
| `0x7c00` | BIOS boot sector and initial real-mode stack |
| `0x10000` through below `0x80000` | Raw kernel load region, at most 448 KiB |
| `0x80000` | PML4 page; CR3 points here |
| `0x81000` | Page-directory-pointer table |
| `0x82000` | Page directory containing the sixteen large-page mappings |
| below `0x90000` | Temporary bootstrap stack, growing downward |
| `0xa0000` | VGA framebuffer |
| `0x100000` onward | Zeroed BSS, task stacks, RAM files, 4 MiB runtime arena, and NIC buffers |
| `0x02000000` | End of the bootstrap identity map |

The linker rejects a load image reaching the page tables or BSS extending
beyond the identity map. The runtime uses static RAM and the arena, not a
physical-frame allocator. Memory above 32 MiB is not discovered or mapped;
giving QEMU more RAM does not make it available to applications. This is a
64-bit execution/ABI conversion, not an implementation of high-memory support.

Every bootstrap leaf is supervisor-only, present, writable and executable.
CR0.WP and EFER.NXE are enabled. The kernel shell uses the bootstrap root at
`0x80000`. Each application uses a private root that retains those low kernel
mappings as supervisor-only and adds its own 4 KiB user pages starting at
`0x40000000`. User code is RX, constants/startup are read-only and NX, and
heap/stack are RW and NX. A guard page precedes the user stack. A null access
from ring 3 is denied by the supervisor-only low map. User faults return to
the shell through the TSS/IDT entry path; kernel faults halt. Details and
limits are in [ISOLATION.md](ISOLATION.md).

## Compiler and task ABI

Clang targets `x86_64-none-elf` with the baseline x86-64 ISA, LP64 data model,
small code model and no red zone. NASM emits ELF64 assembly objects and LLD
links them with the `elf_x86_64` emulation. The kernel uses the SysV AMD64
calling convention: integer/pointer arguments begin in RDI/RSI, floating-point
arguments/results use XMM registers, and RSP is aligned to 16 bytes before a
call. `long`, pointers and `size_t` are 64 bits; fixed-width packet/BPF fields
retain their defined widths.

`ctx_switch` saves RBP, RBX and R12-R15 on the old stack, stores its 64-bit RSP,
loads the next stack, restores those registers and returns. New task frames
contain six saved registers, the trampoline entry and a dummy return slot so
the trampoline starts with RSP modulo 16 equal to 8. Stacks are explicitly
16-byte aligned. The shell has a 16 KiB stack; trusted launchers have 64 KiB,
and each isolated application has a guarded 256 KiB user stack.

This is cooperative switching at a normal function-call boundary. XMM data
registers are caller-saved under this ABI; C callers preserve live values
around the trusted kernel switch. Ring-3 transitions additionally save/restore
FPU/XMM state and prevent user MXCSR/DF settings from affecting kernel work.
Interrupt-driven preemption is still unimplemented.

The arena's allocation headers are aligned/sized to a multiple of 16 bytes,
so returned heap pointers retain the ABI alignment after splitting blocks.
The `arch` shell command checks a double-argument function call, mixed
integer/string/floating-point varargs, an integer exceeding 32-bit range, and
allocation alignment. It reads the actual control registers and code selector
to report long-mode readiness.

## Networking and eBPF

The same RTL8139 driver, IPv4 packet path and compiled eBPF programs run in the
64-bit kernel. Legacy RTL8139 DMA addresses remain 32 bits; the driver checks
that its statically allocated buffers fit before programming the registers.
Identity mapping makes their low virtual addresses usable as physical DMA
addresses. This does not introduce an IOMMU or isolated DMA address space.

The eBPF interpreter still runs real bytecode. Its virtual registers are
64-bit values, now manipulated by a 64-bit C kernel. Map/control-plane layouts
and the built-in programs' bytecode remain unchanged. The networking lab
remains limited to ARP, local IPv4, ICMP echo and the optional UDP echo service;
this architecture change does not add TCP connections, DNS or internet access.

## Validation

```sh
nix develop --command make -C jordyOS test
nix develop --command make -C jordyOS test-network-user
```

The default suite checks live CS/CR0/CR3/CR4/EFER state and physical page-table
entries through QEMU's monitor, the guest ABI probe, and clean rejection of
Pentium III / long-mode-disabled / SSE2-disabled / NX-disabled CPU models. It also exercises
all existing desktop, Apelang, editor, Snake, packet, eBPF and XDP behavior.
The image builder checks ELF64, EM_X86_64, the entry address, boot signature,
and load-size bound. `make test-boot` runs only the architectural checks.
`make test-isolation` checks native user faults, private CR3/physical frames,
user page permissions, syscall validation, and recovery to healthy applications.

References: [Intel system-programming manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html),
[System V x86-64 psABI](https://gitlab.com/x86-psABIs/x86-64-ABI).
