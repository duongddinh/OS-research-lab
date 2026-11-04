"""Real NIC/DMA and packet-path tests in an isolated QEMU socketpair.

No TAP, bridge, external interface, root, DNS, or third-party Python packages.
QEMU's stream netdev wraps each Ethernet frame with a big-endian 32-bit length.
"""
import os
import pathlib
import re
import selectors
import socket
import struct
import subprocess
import sys
import time

GUEST_MAC = bytes.fromhex("525400123456")
PEER_MAC = bytes.fromhex("525400654321")
GUEST_IP = socket.inet_aton("192.0.2.2")
PEER_IP = socket.inet_aton("192.0.2.1")


def checksum(data):
    if len(data) % 2:
        data += b"\0"
    total = sum(struct.unpack(f"!{len(data) // 2}H", data))
    while total >> 16:
        total = (total & 65535) + (total >> 16)
    return ~total & 65535


def ethernet(body, kind=0x0800, destination=GUEST_MAC):
    frame = destination + PEER_MAC + struct.pack("!H", kind) + body
    return frame.ljust(60, b"\0")


def ipv4(body, protocol=17, source=PEER_IP, flags=0, version=0x45):
    header = struct.pack("!BBHHHBBH4s4s", version, 0, 20 + len(body), 42,
                         flags, 64, protocol, 0, source, GUEST_IP)
    header = header[:10] + struct.pack("!H", checksum(header)) + header[12:]
    return ethernet(header + body)


def udp(payload=b"native firewall", source=PEER_IP, **kwargs):
    body = struct.pack("!HHHH", 40000, 7777, 8 + len(payload), 0) + payload
    pseudo = source + GUEST_IP + struct.pack("!BBH", 0, 17, len(body))
    body = body[:6] + struct.pack("!H", checksum(pseudo + body) or 65535) + body[8:]
    return ipv4(body, source=source, **kwargs)


class Lab:
    def __init__(self, user_port=None):
        self.user_port = user_port
        self.wire, child = socket.socketpair()
        self.wire.settimeout(2)
        backend = ["-netdev", f"socket,id=lab,fd={child.fileno()}"]
        if user_port is not None:
            backend = ["-netdev", "user,id=lab,net=192.0.2.0/24,host=192.0.2.1,"
                       f"dhcpstart=192.0.2.2,ipv6=off,hostfwd=udp:127.0.0.1:{user_port}-192.0.2.2:7777"]
        capture = []
        if os.environ.get("NETWORK_PCAP"):
            capture = ["-object", f"filter-dump,id=capture,netdev=lab,file={os.environ['NETWORK_PCAP']}"]
        self.p = subprocess.Popen([
            os.environ.get("QEMU", "qemu-system-i386"), "-drive",
            "if=floppy,format=raw,file=os.img", "-snapshot", "-boot", "a", "-m", "32",
            "-display", "none", "-monitor", "none", "-serial", "stdio", "-nic", "none",
            *backend, *capture, "-device",
            "rtl8139,netdev=lab,mac=52:54:00:12:34:56", "-no-reboot"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            pass_fds=(child.fileno(),))
        child.close()
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.p.stdout, selectors.EVENT_READ)
        self.transcript = bytearray()

    def boot(self):
        self.until(b"desktop> ")
        self.p.stdin.write(b"4"); self.p.stdin.flush()
        self.until(b"sh> ")

    def until(self, marker, timeout=15):
        data = bytearray()
        end = time.monotonic() + timeout
        while not data.endswith(marker) and time.monotonic() < end:
            if self.selector.select(.1):
                chunk = os.read(self.p.stdout.fileno(), 65536)
                if not chunk:
                    break
                data.extend(chunk); self.transcript.extend(chunk)
        assert data.endswith(marker), (marker, data)
        return data.decode(errors="replace").replace("\r", "")

    def command(self, text, expected):
        self.p.stdin.write(text.encode() + b"\n"); self.p.stdin.flush()
        result = self.until(b"sh> ")
        assert expected in result, result
        return result

    def send(self, frame):
        self.wire.sendall(struct.pack("!I", len(frame)) + frame)

    def read_exact(self, size):
        data = bytearray()
        while len(data) < size:
            chunk = self.wire.recv(size - len(data))
            assert chunk, "QEMU closed the network socket"
            data.extend(chunk)
        return bytes(data)

    def receive(self):
        size, = struct.unpack("!I", self.read_exact(4))
        assert 14 <= size <= 1514, size
        return self.read_exact(size)

    def quiet(self):
        self.wire.settimeout(.15)
        try:
            data = self.wire.recv(1)
            raise AssertionError(f"Unexpected reply: {data!r}")
        except socket.timeout:
            pass
        finally:
            self.wire.settimeout(2)

    def counts(self):
        stats = self.command("ebpfw stats", "in allowed=")
        return tuple(map(int, re.findall(r"(?:allowed|denied)=(\d+)", stats)))

    def denied(self, frame, direction=0):
        before = self.counts()
        self.send(frame)
        self.quiet()
        after = self.counts()
        assert after[1 if direction == 0 else 3] == before[1 if direction == 0 else 3] + 1, (before, after)

    def echo(self, frame):
        self.send(frame)
        reply = self.receive()
        assert reply[:6] == PEER_MAC and reply[6:12] == GUEST_MAC, reply
        assert reply[26:30] == GUEST_IP and reply[30:34] == frame[26:30], reply
        assert checksum(reply[14:34]) == 0, reply
        ip_size, = struct.unpack("!H", reply[16:18])
        assert len(reply) >= 14 + ip_size
        if frame[23] == 17:
            assert reply[34:38] == struct.pack("!HH", 7777, 40000), reply
            assert reply[42:14 + ip_size] == frame[42:14 + ip_size], reply
        else:
            assert reply[34] == 0 and checksum(reply[34:14 + ip_size]) == 0, reply
            assert reply[38:14 + ip_size] == frame[38:14 + ip_size], reply

    def close(self):
        self.p.terminate()
        try:
            self.p.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.p.kill(); self.p.wait()
        self.selector.close(); self.wire.close()
        default_log = "/tmp/jordyos-network-user.log" if self.user_port is not None else "/tmp/jordyos-network.log"
        log = pathlib.Path(os.environ.get("NETWORK_LOG", default_log))
        log.write_bytes(self.transcript)
        print(f"Transcript: {log}")


if sys.argv[1:] == ["--user"]:
    # Verify the documented run-net path on an available loopback-only port.
    # The raw socketpair suite below deliberately has no host forwarding.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    lab = Lab(user_port=port)
    try:
        lab.boot()
        lab.command("ebpfw allow 7777/udp", "Rule added: 1")
        lab.command("ebpfw enable", "enabled")
        lab.command("net up", "RTL8139 up")
        lab.command("net echo on", "enabled")
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
            client.settimeout(3)
            client.sendto(b"hello JordyOS", ("127.0.0.1", port))
            assert client.recvfrom(1500)[0] == b"hello JordyOS"
            lab.command("ebpfw delete 1", "Rule deleted")
            client.sendto(b"denied", ("127.0.0.1", port))
            try:
                client.recvfrom(1500)
                raise AssertionError("Denied UDP traffic got a reply")
            except socket.timeout:
                pass
            assert lab.counts()[1] >= 1
        print("PASS QEMU user network, loopback forwarding, real host UDP allow/deny")
    except Exception:
        print(lab.command("net", "Network:"))
        print(lab.command("ebpfw stats", "in allowed="))
        raise
    finally:
        lab.close()
    sys.exit(0)
assert not sys.argv[1:], "Usage: test_network.py [--user]"
lab = Lab()
try:
    lab.boot()
    lab.command("net", "Network: down")
    lab.command("ebpfw doctor", "RTL8139: down")
    lab.command("net up", "RTL8139 up")
    lab.command("ebpfw doctor", "receive/transmit hooks active")
    lab.command("ebpfw status", "disabled")
    # ARP works independently of IP filtering, so an IP deny cannot hide ARP failures.
    arp = struct.pack("!HHBBH", 1, 0x0800, 6, 4, 1) + PEER_MAC + PEER_IP + bytes(6) + GUEST_IP
    lab.send(ethernet(arp, 0x0806, b"\xff" * 6))
    reply = lab.receive()
    assert reply[:12] == PEER_MAC + GUEST_MAC and reply[20:22] == b"\0\2", reply
    assert reply[22:42] == GUEST_MAC + GUEST_IP + PEER_MAC + PEER_IP
    lab.send(udp()); lab.quiet()  # UDP listener is opt-in.
    lab.command("net echo on", "enabled")
    lab.echo(udp())
    lab.command("ebpfw enable", "enabled")
    lab.denied(udp())
    lab.command("ebpfw allow 7777/udp from 192.0.2.1/32 to 192.0.2.2", "Rule added: 1")
    lab.echo(udp())
    lab.denied(udp(source=socket.inet_aton("192.0.2.3")))
    lab.command("ebpfw deny udp", "Rule added: 2")
    lab.echo(udp())  # Earlier allow wins.
    lab.command("ebpfw delete 1", "Rule deleted")
    lab.denied(udp())
    lab.command("ebpfw delete 1", "Rule deleted")
    lab.command("ebpfw allow 7777/udp from 192.0.2.0/24", "Rule added: 1")
    lab.echo(udp(source=socket.inet_aton("192.0.2.3")))
    lab.command("ebpfw default deny outgoing", "Default updated")
    lab.denied(udp(), direction=1)
    lab.command("ebpfw allow out 40000/udp", "Rule added: 2")
    lab.echo(udp())
    lab.command("ebpfw delete 2", "Rule deleted")
    lab.command("ebpfw default allow outgoing", "Default updated")
    body = b"\x08\0\0\0\x12\x34\0\1odd ping!"
    body = body[:2] + struct.pack("!H", checksum(body)) + body[4:]
    ping = ipv4(body, 1)
    lab.denied(ping)
    lab.command("ebpfw allow icmp", "Rule added: 2")
    lab.echo(ping)
    lab.command("ebpfw deny out icmp", "Rule added: 3")
    lab.denied(ping, direction=1)
    # TCP is classified by the real RX hook but has no listener.
    lab.command("ebpfw allow tcp", "Rule added: 4")
    tcp = struct.pack("!HHIIBBHHH", 40000, 80, 1, 0, 0x50, 2, 4096, 0, 0)
    pseudo = PEER_IP + GUEST_IP + struct.pack("!BBH", 0, 6, len(tcp))
    tcp = tcp[:16] + struct.pack("!H", checksum(pseudo + tcp)) + tcp[18:]
    before = lab.counts()
    lab.send(ipv4(tcp, 6)); lab.quiet()
    after = lab.counts()
    assert after[0] == before[0] + 1 and after[2] == before[2], (before, after)
    lab.command("ebpfw delete 4", "Rule deleted")
    print("PASS real RTL8139 DMA, ARP, UDP/ICMP, first-match rules, CIDR and both hook directions")

    # Rejected commands cannot silently weaken policy.
    invalid_commands = ["allow 0/udp", "allow 65536/udp", "allow 7777", "allow ssh", "allow out",
        "allow all from 256.1.2.3", "allow all from 192.0.2.0/33", "allow udp from",
        "allow udp from any from any", "allow udp to any extra", "enable extra", "disable extra",
        "default allow routed", "delete 0", "delete -1", "delete 9999999999999999999",
        "allow udp a b c d e f g h i j"]
    for text in invalid_commands:
        lab.command("ebpfw " + text, "Invalid command; policy unchanged")
    lab.command("ebpfw status numbered", "Rules: 3/32")
    lab.denied(ping, direction=1)
    lab.command("ebpfw disable", "disabled")
    lab.echo(ping)

    # Disabled policy still rejects malformed packets before evaluating rules.
    invalid_frames = [udp(flags=0x2000), udp(flags=1), udp(version=0x46),
                      udp()[:40], ethernet(b"\x45" * 10)]
    bad = bytearray(udp()); bad[24] ^= 1; invalid_frames.append(bytes(bad))
    bad = bytearray(udp()); bad[42] ^= 1; invalid_frames.append(bytes(bad))
    bad = bytearray(udp()); bad[38:40] = b"\0\7"; invalid_frames.append(bytes(bad))
    before = int(re.search(r"invalid=(\d+)", lab.command("net", "invalid=")).group(1))
    for frame in invalid_frames:
        lab.send(frame); lab.quiet()
    after = int(re.search(r"invalid=(\d+)", lab.command("net", "invalid=")).group(1))
    assert after == before + len(invalid_frames), (before, after)
    print("PASS malformed commands and packet validation while policy disabled")

    # Repeated frames force many RX ring wraps; vary payload size through MTU.
    for i in range(180):
        lab.echo(udp(bytes([i % 256]) * (i % 1473)))
    lab.echo(udp(b"x" * 1472))
    lab.p.stdin.write(b"ape\n"); lab.p.stdin.flush(); lab.until(b"ape> ")
    lab.echo(udp())  # Polling continues while the foreground REPL waits for input.
    lab.p.stdin.write(b"exit\n"); lab.p.stdin.flush(); lab.until(b"sh> ")
    lab.command("net down", "Network down")
    lab.command("net up", "RTL8139 up")
    lab.echo(udp())
    lab.command("net echo off", "disabled")
    lab.send(udp()); lab.quiet()
    lab.command("ebpfw reset", "firewall disabled")
    for i in range(32):
        lab.command("ebpfw deny udp", f"Rule added: {i + 1}")
    lab.command("ebpfw allow udp", "Rule table full")
    lab.command("ebpfw status", "Rules: 32/32")
    lab.command("ebpfw reset", "firewall disabled")
    lab.command("ebpfw status", "Rules: 0/32")
    status = lab.command("net", "driver_errors=")
    assert "tx_dropped=0 driver_errors=0" in status, status
    print("PASS RX ring wrapping, MTU, NIC restart, listener toggle, bounded table and reset")
    print("All isolated QEMU network checks passed.")
finally:
    lab.close()
