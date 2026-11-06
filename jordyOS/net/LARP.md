# Vendored TCP LARP core

[TCP LARP](https://github.com/duongddinh/tcplarp) is pinned as a Git submodule
at `vendor/tcplarp`, just like `vendor/apelang`. The initial revision is
`9a9ea8694acea154a6324ec996db91ac497f5928`; Git's submodule entry records the
authoritative revision. Upstream files are unmodified, including the
GPL-2.0-only notices and `LICENSE`.

The kernel compiles `net/larp.c` and `net/tcp.c` against the actual upstream `larp_core.h`.
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

## Real TCP transfers

LARP is the native TCP sender's default and sole congestion controller.
`net/tcp.c` implements a bounded active-open stream: handshake, sequence/ACK
tracking, peer-window flow control, MSS negotiation, retransmission with
exponential backoff, three-duplicate-ACK fast retransmit, and FIN close.
Out-of-order receive data is discarded and acknowledged cumulatively;
overlapping retransmissions are trimmed. The kernel adapter supplies ARP,
IPv4/checksums, the eBPF ingress/egress hooks, and the RTL8139 driver.

On the host, start a loopback-only echo server in the pinned Nix environment:

```sh
python3 jordyOS/tools/tcp_echo.py
# In another terminal:
make -C jordyOS run-net
```

Select Shell (4), then:

```text
ebpfw allow in tcp
ebpfw allow out tcp
ebpfw enable
net up
tcp demo 192.0.2.1 9000
tcp exchange 192.0.2.1 9000 demo.ape received.ape
ape received.ape
tcp status
```

QEMU's user-network host address `192.0.2.1` reaches the loopback echo server.
`tcp demo` sends a deterministic 16 KiB binary payload, including zero bytes,
half-closes its send side, receives the echo, checks every byte, and waits for
peer EOF. `tcp send IP PORT FILE`, `tcp recv IP PORT FILE`, and
`tcp exchange IP PORT SOURCE DESTINATION` operate on the shared RAM filesystem.
The peer must close its send side to delimit the response. Escape or Ctrl-C
cancels an active transfer. Destination files are written only after successful
TCP completion; errors do not save partial data.

The adapter feeds measured delivery intervals, RTT, loss and application-limited
state into the upstream controller. Ambiguous retransmission ACKs do not yield
RTT/delivery samples (Karn's rule). Delivery intervals use the longer of sender
and acknowledgement intervals, rather than raw ACK spacing. `larp_window` and
`larp_ack_window` limit outstanding packets, with an implementation cap of 16
segments. Each data transmission, including retries, schedules the next allowed
transmission from its actual send time at LARP's rate; late polls do not bank
credit for a catch-up burst. TCP status reports real ACKed bytes, receive bytes,
LARP feedback/learning, retransmissions, and observed pacing gaps.

## Tests and limits

```sh
make -C jordyOS test-tcp
python3 jordyOS/tools/prepare_tcp_linux.py \
  --cache /tmp/jordyos-linux-assets --output /tmp/jordyos-linux
TCP_LINUX_DIR=/tmp/jordyos-linux make -C jordyOS test-tcp-linux
```

Fixture preparation verifies SHA256-pinned Alpine 3.22.7 aarch64 rootfs and
Linux 6.12.115 assets. The interoperability test uses two diskless QEMU guests
connected only through a local Ethernet relay. It requires
`qemu-system-x86_64` and `qemu-system-aarch64`; no host interfaces, bridges,
root privileges, mounted host directories, credentials, or Internet access
are supplied to either guest. The Linux peer runs BusyBox TCP servers.
The relay checks IP/TCP checksums and can delay or drop real frames.
The suite verifies 16 KiB binary echo, RAM-file upload/download/exchange,
SHA256 checks in Linux, execution of transferred Apelang source, loss of SYN,
upload data and FIN with 5 ms per-direction relay delay, Linux retransmission
after download loss, peer refusal, firewall enforcement, and cancellation.
`test-tcp` is included in the normal test suite; Linux interoperability is a
separate target because its larger fixture must be prepared explicitly.

This is a research TCP client, not a full RFC-conformant general socket stack
or a throughput benchmark. Limits:

- One foreground transfer; 16 KiB each for upload and response, 536-byte maximum
  outgoing MSS, 16 outstanding segments and a 4 KiB advertised receive window
  to fit the 8 KiB NIC ring. One byte of EOF allowance remains at the file-size
  limit; additional payload is rejected. No TCP listener or user-process sockets.
- Static IPv4 on the local /24 only; no gateway, DNS, DHCP, IPv6 or fragmentation.
- No SACK, window scaling, timestamps, ECN negotiation, urgent-data API or
  out-of-order receive queue. ECN is not advertised; arbitrary ECE bits cannot
  be treated as negotiated ECN evidence.
- PIT-calibrated TSC timing assumes a stable QEMU CPU clock. Networking is
  serviced by the blocking transfer loop and existing polling points, without
  interrupt-driven/preemptive networking. The PIT channel is restored to the
  speaker gate state before returning; later guest waits can reuse channel 2.
- Initial RTO is one second, measured RTO has a one-second minimum and
  60-second maximum, retries back off exponentially, and transfers have a
  120-second deadline. A retransmitted handshake starts data with a three-second
  RTO until an unambiguous RTT sample arrives. Zero-window probes are bounded
  by that deadline.
- Eight 120-second TIME-WAIT records answer repeated FINs. If all are occupied,
  wait for one to expire. Ephemeral ports are never reused during a boot;
  reboot after exhausting ports 49152 through 65535. ISNs use the TSC, not a
  cryptographic random generator; this is intended for the isolated lab.

Linux LARP's published performance results do not apply to this adapter.

Protocol references: [TCP (RFC 9293)](https://www.rfc-editor.org/rfc/rfc9293),
[retransmission timers (RFC 6298)](https://www.rfc-editor.org/rfc/rfc6298),
and [duplicate-ACK recovery (RFC 5681)](https://www.rfc-editor.org/rfc/rfc5681).
The research LARP controller replaces standard Reno congestion control; this
bounded implementation does not claim full TCP standards conformance.

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
