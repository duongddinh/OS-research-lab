# Application memory isolation

Calculator, Editor, Snake, History and the Apelang REPL/compiler/runtime now
run as **ring-3 application processes**. They no longer call C kernel functions
or touch devices directly. The kernel shell, desktop, RAM filesystem, device
drivers and eBPF packet hooks remain trusted ring-0 services.

The CPU enforces the memory boundary. Each launch gets a private page-table
root (CR3), private physical backing, and a fresh user stack/heap. The same
virtual application address can therefore refer to different physical RAM:

```text
App A's virtual address -> page tables A -> private physical page A
App B's virtual address -> page tables B -> private physical page B
Kernel/other pool addresses -> supervisor-only -> CPU denies user access
```

The OS prepares the tables; the MMU checks every access. Applications run with
lower CPU privilege, so they cannot rewrite CR3, edit the tables through a
physical alias, execute HLT, or access device ports to bypass those checks.

## Build and launch boundary

`user/runtime.elf` is a separate freestanding x86-64 executable containing
the Apelang compiler/VM, application launcher, allocator, formatting/file
library and small system-call wrappers. It is linked at virtual `0x40000000`.
`tools/embed_user.py` checks the static ELF's segment bounds and permissions,
rejects writable+executable segments and dynamic images, then generates the
ignored `user/image.h`. The kernel embeds that payload and its region metadata;
there is no direct linking of user runtime functions into the kernel launch
path. The program source files remain in the shared RAM filesystem, seeded
by the kernel's `ape/seed.c`.

The kernel reserves two bounded physical backing slots in the first 32 MiB.
Only one foreground user process executes at a time. Successive normal
launches alternate slots; a reused slot is cleared before its new image and
startup state are copied. Tests retain a marker in the inactive slot and
attempt to access it from the other process to verify the boundary. This is
memory isolation, not a new concurrent/preemptive process scheduler.

## Memory permissions

| Region | User permissions |
| --- | --- |
| Linked code | Read/execute, not writable |
| Constants | Read-only, not executable |
| Data/BSS, including private 4 MiB allocator arena | Read/write, not executable |
| Startup page at `0x40800000` | Read-only, not executable |
| Diagnostic private marker at `0x407ff000` | Read/write, not executable |
| Guard page at `0x40801000` | Not present |
| 256 KiB stack at `0x40802000` | Read/write, not executable |
| Low kernel identity map, including peer backing, page tables, VGA and kernel buffers | Supervisor-only, inaccessible from ring 3 |

Private roots share the bootstrap kernel page directory as supervisor-only.
Their separate user directory uses 4 KiB leaf pages to enforce the segment
permissions. A user-accessible parent directory does not override a
supervisor-only child entry. CR0.WP and EFER.NXE are enabled. CPU support for
NX is now required, in addition to long mode/SSE2.

The kernel's own identity map remains RWX. This change protects the kernel
from direct application accesses; it does not protect it from a kernel bug.
There is no KPTI, ASLR, IOMMU isolation, copy-on-write, demand paging, dynamic
frame allocator or microarchitectural side-channel mitigation.

## Crossing into the kernel

Applications request services with `int 0x80`. This is the only DPL-3 interrupt
gate. The CPU switches to the ring-0 code selector and a kernel-owned stack
from the 64-bit TSS. All other gates are kernel-only; the TSS I/O bitmap is
outside the descriptor limit, so user port I/O is denied.

The entry stub saves general registers and FPU/XMM state, clears the direction
flag, and initializes kernel floating-point controls before calling C. User
state is restored on a normal return. New user processes start with cleared
general/XMM/x87 state; kernel FPU state is restored when a process terminates.

Services cover console output/input, RAM file operations, clipped graphics,
terminal selection, polling/waits, process identity and exit. The kernel checks
all pointer ranges against the active process's page permissions before
copying data. It rejects output buffers in read-only/code/kernel pages, address
overflow, guard crossings, overlong strings, excessive lengths and invalid
graphics coordinates. It copies through bounded kernel buffers and never
retains a pointer to application memory. Terminal titles now have kernel-owned
storage, so closing an application cannot leave a dangling user pointer.

The RAM filesystem and display are intentionally shared services. Memory
isolation does not give applications separate file namespaces or file-access
permissions. The firewall administration commands remain in the kernel shell;
no raw device or arbitrary kernel-call service is exposed to applications.

## Fault containment

The IDT handles CPU exceptions. If a fault came from ring 3, the kernel records
it, terminates that process, restores the kernel page-table root and saved
execution state, and returns to the launcher/shell. A subsequent healthy
application can run. It does not retry the forbidden instruction. A double
fault or unexpected ring-0 exception halts with a kernel diagnostic; it is not
treated as a recoverable application fault.

Maskable interrupts remain disabled. Kernel input/wait services poll devices
as before, and one foreground application owns console input. Apelang's
instruction budget still bounds ordinary language loops, but arbitrary native
user code can monopolize the CPU: timer-driven preemption/time limits are not
implemented. This is an educational isolation implementation with a bounded
service interface, not a complete multi-user security system.

## Inspect and test

In the shell:

```text
arch
isolation
isolation test
```

The self-test actually enters ring 3 and attempts kernel/peer reads and writes,
HLT, port I/O, CR3 access, code writes, execution from data, null/startup/guard
accesses, division by zero and an invalid opcode. It checks the real exception
vectors and user fault bits, validates system-call pointer rejection, tests
direction/FPU control handling, verifies an inactive process's marker remains
unchanged, then starts a healthy application after all faults.

```sh
nix develop --command make -C jordyOS test-isolation
nix develop --command make -C jordyOS test
nix develop --command make -C jordyOS test-network-user
```

The QEMU test independently inspects distinct live CR3 values and physical code
frames across launches, supervisor-only kernel aliases, RX code, read-only
startup, RW+NX stacks and the absent guard PTE. The ELF embedder is tested for
range/permission/dynamic-image failures. The full existing desktop, editor,
Apelang, Snake and eBPF/network suites exercise the real isolated runtime.
The isolation transcript defaults to `/tmp/jordyos-isolation.log`; override it
with `ISOLATION_LOG`.

References: [Intel system-programming manual](https://cdrdv2-public.intel.com/825758/253668-sdm-vol-3a.pdf),
[SysV x86-64 ABI](https://gitlab.com/x86-psABIs/x86-64-ABI).
