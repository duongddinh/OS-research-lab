# Native JordyOS firewall milestone

This is a first native firewall for the existing freestanding i686 JordyOS
kernel. It borrows the UFW-style command interface from
[ebpfw](https://github.com/duongddinh/ebpfw), but implements policy directly in C.
It does not execute the upstream Linux binary or Linux eBPF object. There is
no eBPF instruction interpreter, verifier, JIT, BTF, bpffs, Netfilter, or Linux
system-call compatibility layer. The Linux project's stateful behavior and
benchmark results do not apply to this implementation.

## Run the lab

From the repository root, enter the pinned environment:

```sh
nix develop
make -C jordyOS run-net
```

This attaches one RTL8139 to a QEMU user network, with guest address
`192.0.2.2/24`. UDP `127.0.0.1:17777` on the host forwards to guest port `7777`.
No host bridge, TAP interface, or privileged networking configuration is needed.
The NIC starts down; no packets are received before `net up`.
`make run` retains its existing `-nic none` behavior.

QEMU's `restrict=on` backend drops UDP replies even for host forwards, so this
interactive demo uses the standard user backend with IPv6 disabled. It is not
an egress sandbox. The kernel only responds to peers in the lab subnet and has
no gateway or connection-initiation API. The automated raw-packet test below
uses a socketpair with no host or external network connection.

Select Shell (4) and configure the firewall before starting the interface:

```text
ebpfw allow 7777/udp
ebpfw allow icmp
ebpfw enable
net up
net echo on
ebpfw doctor
ebpfw status numbered
```

On the host, this Python standard-library example should receive an echo:

```sh
python3 - <<'PY'
import socket
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
    client.settimeout(3)
    client.sendto(b"hello JordyOS", ("127.0.0.1", 17777))
    print(client.recvfrom(1500))
PY
```

Back in the guest, `ebpfw delete 1` removes the UDP allowance. Repeating the host
example now times out. `ebpfw stats` shows the incoming deny counter increasing.
`net echo off` closes the UDP listener; `net down` stops RX/TX. ICMP echo replies
are available while the interface is up and policy permits them.

## Commands and policy

```text
ebpfw allow|deny [in|out] SERVICE [from CIDR] [to CIDR]
ebpfw default allow|deny incoming|outgoing
ebpfw enable
ebpfw disable
ebpfw delete NUMBER
ebpfw status [numbered]
ebpfw stats
ebpfw doctor
ebpfw reset
net status
net up
net down
net echo on|off
```

`SERVICE` is `all`, `icmp`, `tcp`, `udp`, `PORT/tcp`, or `PORT/udp`. Ports are
1–65535. Omitted direction means incoming. Ports always match the **destination**
port, including outgoing packets. A CIDR can also be a single IPv4 address or
`any`. Examples:

```text
ebpfw allow in 7777/udp from 192.0.2.0/24 to 192.0.2.2
ebpfw deny out icmp
ebpfw default deny outgoing
ebpfw allow out udp to 192.0.2.1
```

There are at most 32 ordered rules; the first matching rule decides. A later
deny does not override an earlier allow. Otherwise the direction's default
applies: incoming deny, outgoing allow. Filtering starts disabled, so valid
packets bypass policy until explicitly enabled. `disable` preserves rules;
`reset` clears rules and policy counters, restores the defaults, and disables
filtering. Interface and listener state are separate from policy state.
Malformed commands leave policy unchanged. Unsupported Linux ebpfw commands
and application profiles are rejected rather than approximated.

Rules have per-rule hit counters. `ebpfw stats` counts incoming/outgoing policy
decisions, including bypass while disabled. An incoming allow does not mean a
service exists or a reply was sent. An outgoing allow does not guarantee NIC
transmission. `net status` separately counts Ethernet RX/TX submissions,
invalid IP packets, ignored traffic, busy TX descriptors, and driver errors.
Counters are unsigned 32-bit values and wrap. Everything is RAM-only.

## Packet path

```text
RTL8139 RX ring -> Ethernet/local IPv4 validation -> incoming rules
    -> ICMP echo or optional UDP echo -> outgoing rules -> RTL8139 TX
```

Only unicast IPv4 addressed to the guest from another host on `192.0.2.0/24`
reaches policy. Network/broadcast/self source IPs are ignored. The decoder
checks IPv4 lengths, TTL, header checksum, transport lengths, and ICMP/TCP/UDP
checksums. An absent UDP checksum is permitted for IPv4. All IPv4 fragments,
IP options, unknown transports, IPv6, and VLAN frames are unsupported; none
reach a service, even with filtering disabled. A maximum Ethernet frame is
1514 bytes excluding FCS, with a 1500-byte IP MTU.

ARP is handled separately from IP policy. The guest only replies to valid
Ethernet/IPv4 ARP requests for its own address from the lab subnet. ARP has no
authentication and this implementation has no neighbor cache. Replies reuse
the request's source Ethernet address; there is no gateway or ARP resolution
for initiating connections. UDP checksums are validated on ingress; replies
omit them as allowed by IPv4. TCP headers are validated and classified by
policy, but there is no TCP state machine, connection, or listener.

The driver discovers a legacy RTL8139 on PCI bus 0 and uses its BIOS-assigned
I/O BAR. Static DMA buffers rely on the kernel's flat identity mapping. RX
uses an 8 KiB ring with modulo copies, TX uses four fixed buffers, and each
poll processes at most eight frames. Ring errors/overflow restart the NIC and
increment the driver-error counter. PCI interrupts remain masked. The driver
has been tested against QEMU, not physical RTL8139 hardware.

Polling runs from keyboard/serial input, task yields, and guest timer waits.
A long application computation without these calls can delay reception and
overflow the NIC ring. This is cooperative research code, not a production
security boundary. Trusted apps share kernel memory and can modify policy.

## Validation

```sh
nix develop --command make -C jordyOS test
# Individual targets:
nix develop --command make -C jordyOS test-firewall
nix develop --command make -C jordyOS test-network
# Optional: test the documented host-forwarding demo on an available UDP port:
nix develop --command make -C jordyOS test-network-user
# Undefined-behavior/bounds checks for the native decoder and rule engine:
nix develop --command make -C jordyOS test-firewall \
  HOST_CFLAGS='-O1 -g -fsanitize=undefined,bounds -fno-sanitize-recover=all'
```

Host tests compile the exact decoder and rule engine. They cover valid
UDP/TCP/ICMP, checksums, truncation, fragments, options, invalid CIDRs, numeric
overflow, ordering, direction, port/subnet matching, counters, deletion, rule
capacity, reset, and deterministic malformed input.
On a compatible host, AddressSanitizer can also be enabled through
`HOST_CFLAGS`. The pinned Clang 21 AddressSanitizer runtime deadlocked during
initialization on the macOS 26.6.2 development host, before `main`; that run did
not validate the code. The normal and undefined-behavior/bounds runs passed.

The network test boots the real image with a QEMU RTL8139 connected to a Unix
socketpair. It injects Ethernet frames and checks actual ARP/UDP/ICMP responses,
both policy hooks, drop counters, invalid commands, disabled-mode validation,
maximum-size packets, repeated RX-ring wraps, and interface/listener controls.
This test has no external network connection or host forwarding port. The
existing Apelang/desktop/Snake regressions still run with no NIC. Transcripts
are `/tmp/jordyos-network.log` and `/tmp/jordyos-smoke.log`; override them with
`NETWORK_LOG` and `SMOKE_LOG`. The optional host-forwarding test defaults to
`/tmp/jordyos-network-user.log`. Set `NETWORK_PCAP` to capture the test NIC's
Ethernet frames in a local pcap file.

The next steps toward a fuller native firewall are a timer/interrupt-driven
network task, an actual TCP/IP stack, persistent configuration, and connection
tracking with explicit resource bounds. Running upstream eBPF programs would
additionally require a defined native BPF ABI, verifier/interpreter, helpers,
maps, and packet-hook adapters. A native C policy engine does not provide
those facilities.

Protocol references: [IPv4 (RFC 791)](https://www.rfc-editor.org/rfc/rfc791),
[UDP (RFC 768)](https://www.rfc-editor.org/rfc/rfc768),
[ICMP (RFC 792)](https://www.rfc-editor.org/rfc/rfc792), and
[QEMU RTL8139 register model](https://github.com/qemu/qemu/blob/v10.2.0/hw/net/rtl8139.c).
