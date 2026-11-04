# Upstream ebpfw source

Source: https://github.com/duongddinh/ebpfw/blob/d1421287c5d57cb796e4a2e7d2c2767cec865908/bpf/stateful.c

Pinned revision: `d1421287c5d57cb796e4a2e7d2c2767cec865908`

Original source SHA-256: `8c42d349f2ef99680403853c35180ffa70c38588e2e1b5de0da858d25cc78053`

`ebpfw_shared.h` retains the upstream rule/header/packet/counter/LPM-key
structures and its be16, be32, prefix, name_equal, and matches functions.
`ebpfw_early.h` retains the actual upstream ebpfw_early XDP function, changing
only its section from xdp.frags to xdp because this driver supplies one
contiguous frame. Both files are GPL-2.0-only; LICENSE contains that license.
The original file is available at the immutable source link above.

JordyOS modifications (November 4, 2026): this directory's program.bpf.c
supplies bounded legacy map definitions (32 rules, one CPU), the original count
helper, and a new stateless policy entry point using upstream matches. Native
code serializes the shell policy into the same 96-byte rule layout. Only
allow/deny/absolute IPv4 source blocks are exposed. This is a partial native
adaptation, not execution of the full upstream Linux Netfilter program.

Linux dynptr/kfunc hooks, BTF relocations, connection tracking, IPv6 policy,
rate limiting, reject replies, spin locks, events, and the Go administration
program have not been ported. No upstream performance claims apply.
