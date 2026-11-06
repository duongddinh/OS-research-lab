# Vendored TCP LARP core

[TCP LARP](https://github.com/duongddinh/tcplarp) is pinned as a Git submodule
at `vendor/tcplarp`, just like `vendor/apelang`. The initial revision is
`9a9ea8694acea154a6324ec996db91ac497f5928`; Git's submodule entry records the
authoritative revision. Upstream files are unmodified, including the
GPL-2.0-only notices and `LICENSE`.

The kernel compiles `net/larp.c` against the actual upstream `larp_core.h`.
This header provides `larp_start`, `larp_update`, `larp_window`, and
`larp_ack_window`. It needs fixed-width integers and integer arithmetic;
the freestanding build uses its portable C path, without `__KERNEL__` or
Linux headers. The Linux module in `tcp_larp.c` and its Linux Makefile are
present for provenance but are not built into JordyOS. LARP runs as native
x86-64 C code, independently of the eBPF firewall interpreter.

## Try it

Initialize both pinned dependencies before building:

```sh
git submodule update --init --recursive
nix develop
make -C jordyOS
make -C jordyOS test-larp
make -C jordyOS run
```

Select Shell (4), then run:

```text
larp status
larp test
```

The self-test feeds synthetic measurements into the actual upstream core.
It checks the launch rate/window, feedback-driven growth, loss and ECN
reductions, the once-per-RTT congestion guard, missing/application-limited
samples, minimum-RTT revalidation, recovery windows, and saturating arithmetic.
`test-larp` runs the same scenarios on the host; the QEMU smoke test executes
them in the actual kernel and checks the shell status and error paths.
The normal `make -C jordyOS test` includes both checks.

## What this makes available

The algorithm is vendored, compiled, and executable inside JordyOS. It is
**not controlling network traffic yet**: JordyOS still has no TCP connection
implementation. Neither `larp test` nor vendoring the Linux module creates
one. The test sends no network traffic and makes no throughput claim.

A future TCP sender must establish connections, track sequence numbers and
acknowledgements, buffer data, retransmit losses, and maintain timers. It must
provide real delay/delivery/loss/ECN measurements to `larp_update`, enforce
the packet window returned by `larp_window`/`larp_ack_window`, and space
transmissions according to the model's rate in bytes per second. Linux
currently supplies those mechanisms to `tcp_larp.c`; JordyOS must supply its
own integration. The present NIC polling model also needs a reliable timing
and service strategy before meaningful pacing or performance comparisons.

## Updating the dependency

Keep dependency updates reviewable and pinned:

```sh
git -C vendor/tcplarp fetch origin
git -C vendor/tcplarp checkout <reviewed-commit>
make -C jordyOS test
git add vendor/tcplarp
```

Commit the changed submodule entry with the validation results. Builds do not
follow upstream `main` automatically. `TCPLARP_DIR` can override the source
directory for local development, analogous to `APELANG_DIR`; normal builds
use the pinned submodule.
