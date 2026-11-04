# eBPF execution in JordyOS

`program.bpf.c` includes actual upstream ebpfw source from `ebpfw_early.h` and
the original `matches`/prefix helpers from `ebpfw_shared.h`. See
[UPSTREAM.md](UPSTREAM.md) for exact provenance and modifications. The early
XDP function is retained apart from the section name. A new stateless policy
entry point uses the upstream matcher and 96-byte rule layout. Both entry
points are compiled with `clang -target bpfel -mcpu=v1 -O2` to real eBPF
instructions in an ELF64 EM_BPF object.

The interpreter and native adapters are `../bpf_vm.c` and `../fw_bpf.c`.
There is no native C rule-matching fallback: policy decisions run in the VM.
C retains the shell parser, IP/transport validation, policy-to-map serializer,
primitive map lookups, and accounting for the returned verdict. Source-prefix
matching for the upstream early program is an LPM-map primitive.

## Build and inspect

```sh
nix develop
make -C jordyOS
llvm-objdump -d jordyOS/net/bpf/program.bpf.o
llvm-readelf -r jordyOS/net/bpf/program.bpf.o
```

`tools/embed_bpf.py` extracts the `xdp` and `jordyos/policy` sections and
resolves R_BPF_64_64 map-symbol relocations to seven known map IDs. It rejects
other machine types, malformed sizes, and unsupported relocations. There are
no BTF or CO-RE relocations, subprogram calls, downloaded object files, or
runtime native-code execution. It generates the ignored `programs.h`, which
embeds the relocated 8-byte instructions and the input ELF's SHA-256.
`make clean` removes both generated files; the pinned Nix environment supplies
the BPF compiler and LLVM tools. The shell's `ebpfw bpf` command displays the
instruction counts, validation status, and object hash.

## Execution ABI and limits

The interpreter implements little-endian 8-byte eBPF instructions with eleven
64-bit registers, immutable frame pointer r10, and a fresh 512-byte stack.
Supported operations include ALU32/64, wide immediates, unsigned division and
modulo, signed/unsigned branches, JMP32, byte-order conversion, and MEM loads
and stores of 1/2/4/8 bytes. Supported programs have at most 4096 instruction
slots. BPF-to-BPF calls, kfuncs, atomics, legacy packet loads, signed division,
and newer ISA extensions are rejected. The scope is a documented subset, not
a claim of all current BPF conformance groups.

Structural validation checks opcodes, register numbers, wide-instruction
continuations, branch targets, map IDs, helper IDs, and writes to r10. It does
not prove dataflow or termination as Linux's verifier does. The only exposed
helper is `bpf_map_lookup_elem` (ID 1), including its r1–r5 clobber convention.
Loops are bounded dynamically by a 65,536-dispatch limit. A fault, unsupported
verdict, or exhausted budget drops the packet; there is no C fallback.

Registers contain virtual addresses, never native kernel pointers. Every load
and store is translated and checked against context, packet, stack, or one
map value. Packet/context/policy values are read-only; stack and stats values
are writable. An access cannot cross an individual map value's boundary.
Stack/register contents start at zero. This provides runtime confinement for
the built-in programs; trusted kernel applications still share the enclosing
address space, so the OS itself remains outside a security sandbox.

The XDP context uses the six 32-bit `xdp_md` fields and standard PASS=2/DROP=1
verdicts. This is a JordyOS driver-side XDP-compatible hook, not Linux's XDP
attachment infrastructure or NIC offload. It executes after the DMA ring copy
and before Ethernet/IP processing. It is interpreted, so no Linux/XDP speed
claims or zero-copy claims are made. Ordinary incoming/outgoing rules execute
in a separate BPF program after the C packet decoder supplies validated IPv4
metadata. The outgoing hook is a JordyOS policy hook, not XDP egress.

## Native maps

| ID | Symbol | Native behavior |
| --- | --- | --- |
| 0 | `policy_template` | Array of 33 96-byte header/rule entries, read-only |
| 1 | `active` | One-element array-of-maps referencing policy map 0 |
| 2 | `stats` | 48 counter values, writable; per-CPU array on one CPU |
| 3 | `early4` | At most 32 IPv4 longest-prefix entries |
| 4 | `early6` | Empty IPv6 longest-prefix map; no IPv6 shell policy yet |
| 5 | `early_enabled` | Read-only enable flag |
| 6 | `metadata` | Read-only validated packet metadata for policy evaluation |

The C adapter serializes the current RAM policy before each evaluation. It
places absolute blocks ahead of ordinary rules, retains original rule indexes
for counters, and copies per-evaluation BPF hit counters back into shell
state. This is intentionally simple, single-CPU code; it has no kernel map
syscalls, userspace map FDs, dynamic program loader, concurrent updates, or
persistent pinned links. Shell commands only configure the built-in programs.

Reference: [BPF instruction set specification](https://docs.kernel.org/bpf/standardization/instruction-set.html).
