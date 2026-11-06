# Upstream ebpfw source

The full upstream repository is pinned as the `vendor/ebpfw` Git submodule,
using the same dependency model as Apelang and TCP LARP. Initialize it with
`git submodule update --init --recursive`.

Source: https://github.com/duongddinh/ebpfw/blob/d1421287c5d57cb796e4a2e7d2c2767cec865908/bpf/stateful.c

Pinned revision: `d1421287c5d57cb796e4a2e7d2c2767cec865908`

Original source SHA-256: `8c42d349f2ef99680403853c35180ffa70c38588e2e1b5de0da858d25cc78053`

The build runs `tools/embed_ebpfw.py` against the actual vendored
`bpf/stateful.c`. `upstream.json` records the reviewed source hash and revision;
an unexpected source change fails the build until the adapter is reviewed.
The generated headers are ignored build outputs, not hand-maintained copies.

`ebpfw_shared.h` retains the upstream rule/header/packet/counter/LPM-key
structures and its be16, be32, prefix, name_equal, and matches functions.
`ebpfw_early.h` retains the actual upstream ebpfw_early XDP function, changing
only its section from xdp.frags to xdp because this driver supplies one
contiguous frame. `ebpfw_count.h` retains the original counter helper. The
generated code is GPL-2.0-only; the submodule and this directory retain the
license. The original file is available in the submodule and at the immutable
source link above. At this pin, the generated BPF object remains byte-identical
to the prior adaptation (SHA256
`c8d6d6ef066acb92670fe775a62d473bdf0c5165f3879e5566575cb2555cee0e`).

To update, check out a reviewed upstream commit in `vendor/ebpfw`, review the
declarations/helpers/XDP body and native ABI, update `upstream.json` and this
document, then run the firewall, network and full regression tests. Commit
the changed gitlink and metadata together. `EBPFW_DIR` can override the source
directory for development; normal builds use the pinned submodule. The hash
guard applies to either source location.

JordyOS modifications (November 4, 2026): this directory's program.bpf.c
supplies bounded legacy map definitions (32 rules, one CPU), the original count
helper, and a new stateless policy entry point using upstream matches. Native
code serializes the shell policy into the same 96-byte rule layout. Only
allow/deny/absolute IPv4 source blocks are exposed. This is a partial native
adaptation, not execution of the full upstream Linux Netfilter program.

Linux dynptr/kfunc hooks, BTF relocations, connection tracking, IPv6 policy,
rate limiting, reject replies, spin locks, events, and the Go administration
program have not been ported. No upstream performance claims apply.
