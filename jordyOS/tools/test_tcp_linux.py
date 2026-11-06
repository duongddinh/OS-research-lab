"""Offline interoperability test: real JordyOS and Linux kernels, raw Ethernet relay.

No host networking, TAP, bridge, root, mounted host filesystems or Python packages.
Prepare the checksum-pinned Linux fixture with prepare_tcp_linux.py first.
"""
import hashlib
import heapq
import os
from pathlib import Path
import re
import select
import socket
import struct
import subprocess
import threading
import time


class Console:
    def __init__(self, command, child):
        self.p = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, pass_fds=(child.fileno(),))
        child.close()
        self.log = bytearray()

    def until(self, marker, timeout=30, contains=False):
        data = bytearray()
        end = time.monotonic() + timeout
        def found():
            return marker in data if contains else data.endswith(marker)
        while not found() and time.monotonic() < end:
            if select.select([self.p.stdout], [], [], .1)[0]:
                chunk = os.read(self.p.stdout.fileno(), 65536)
                if not chunk:
                    break
                data.extend(chunk)
                self.log.extend(chunk)
        assert found(), (marker, data.decode(errors="replace"))
        return data.decode(errors="replace").replace("\r", "")

    def write(self, data):
        self.p.stdin.write(data)
        self.p.stdin.flush()

    def command(self, text, expected, timeout=75):
        self.write(text.encode() + b"\n")
        result = self.until(b"sh> ", timeout)
        assert expected in result, result
        print(result, flush=True)
        return result

    def drain(self):
        while select.select([self.p.stdout], [], [], .1)[0]:
            data = os.read(self.p.stdout.fileno(), 65536)
            if not data:
                break
            self.log.extend(data)

    def close(self):
        self.drain()
        self.p.terminate()
        try:
            self.p.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.p.kill()
            self.p.wait()


def checksum(data):
    if len(data) & 1:
        data += b"\0"
    total = sum(struct.unpack(f"!{len(data)//2}H", data))
    while total >> 16:
        total = (total & 65535) + (total >> 16)
    return ~total & 65535


class Relay:
    def __init__(self, jordy, linux):
        self.sockets = [jordy, linux]
        self.stop = threading.Event()
        self.mode = "clean"
        self.dropped = set()
        self.error = None
        self.frames = 0
        self.trace = []
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def configure(self, mode):
        self.mode = mode
        self.dropped = set()

    def inspect(self, direction, frame):
        delay = .005 if self.mode != "clean" else 0
        if frame[12:14] != b"\x08\x00" or frame[23] != 6:
            return delay
        ip_size = struct.unpack_from("!H", frame, 16)[0]
        ip = frame[14:14+ip_size]
        assert ip[0] == 0x45 and checksum(ip[:20]) == 0
        tcp = ip[20:]
        assert checksum(ip[12:20] + struct.pack("!BBH", 0, 6, len(tcp)) + tcp) == 0
        flags = tcp[13]
        payload = len(tcp) - (tcp[12] >> 4) * 4
        self.trace.append((direction, struct.unpack_from("!II", tcp, 4), flags, payload))
        self.trace = self.trace[-200:]
        key = None
        if self.mode == "loss" and direction == 0:
            key = "syn" if flags & 2 else "upload" if payload else "fin" if flags & 1 else None
        if self.mode == "receive_loss" and direction == 1 and payload:
            key = "download"
        if key and key not in self.dropped:
            self.dropped.add(key)
            return None
        return delay

    def run(self):
        buffers = [bytearray(), bytearray()]
        queue = []
        count = 0
        try:
            while not self.stop.is_set():
                now = time.monotonic()
                while queue and queue[0][0] <= now:
                    _, _, direction, frame = heapq.heappop(queue)
                    self.sockets[1-direction].sendall(struct.pack("!I", len(frame)) + frame)
                readable = select.select(self.sockets, [], [], .001)[0]
                for wire in readable:
                    direction = self.sockets.index(wire)
                    data = wire.recv(65536)
                    if not data:
                        return
                    buf = buffers[direction]
                    buf.extend(data)
                    while len(buf) >= 4:
                        size = struct.unpack_from("!I", buf)[0]
                        assert 14 <= size <= 65536, size
                        if len(buf) < size + 4:
                            break
                        frame = bytes(buf[4:4+size])
                        del buf[:4+size]
                        self.frames += 1
                        delay = self.inspect(direction, frame)
                        if delay is not None:
                            count += 1
                            heapq.heappush(queue, (time.monotonic()+delay, count, direction, frame))
        except BaseException as error:
            self.error = error

    def close(self):
        self.stop.set()
        self.thread.join(timeout=3)
        for wire in self.sockets:
            wire.close()


def number(text, name):
    return int(re.search(rf"\b{name}=(\d+)", text).group(1))


def main():
    fixture = Path(os.environ["TCP_LINUX_DIR"])
    qemu = os.environ.get("QEMU", "qemu-system-x86_64")
    linux_qemu = os.environ.get("TCP_LINUX_QEMU", "qemu-system-aarch64")
    jw, jc = socket.socketpair()
    lw, lc = socket.socketpair()
    relay = Relay(jw, lw)
    guests = []
    try:
        linux = Console([linux_qemu, "-machine", "virt", "-accel", "tcg", "-cpu", "cortex-a72",
            "-m", "512", "-display", "none", "-monitor", "none", "-serial", "stdio", "-no-reboot",
            "-kernel", str(fixture / "vmlinuz"), "-initrd", str(fixture / "initramfs.cpio.gz"),
            "-append", "console=ttyAMA0 quiet panic=-1", "-nic", "none",
            "-netdev", f"socket,id=lab,fd={lc.fileno()}",
            "-device", "virtio-net-pci,netdev=lab,mac=52:54:00:65:43:21"], lc)
        guests.append((linux, "linux"))
        linux.until(b"LINUX_TCP_READY\r\n", contains=True)
        jordy = Console([qemu, "-cpu", "qemu64", "-m", "32", "-display", "none",
            "-monitor", "none", "-serial", "stdio", "-no-reboot", "-snapshot", "-boot", "a",
            "-drive", "if=floppy,format=raw,file=os.img", "-nic", "none",
            "-netdev", f"socket,id=lab,fd={jc.fileno()}",
            "-device", "rtl8139,netdev=lab,mac=52:54:00:12:34:56"], jc)
        guests.append((jordy, "jordyos"))
        jordy.until(b"desktop> ")
        jordy.write(b"4")
        jordy.until(b"sh> ")
        for command, expected in [("net up", "RTL8139 up"), ("ebpfw allow in tcp", "Rule added"),
                ("ebpfw allow out tcp", "Rule added"), ("ebpfw enable", "enabled")]:
            jordy.command(command, expected)
        result = jordy.command("tcp demo 192.0.2.1 9000", "TCP DEMO PASS")
        assert number(result, "tx_acked") == number(result, "rx") == 16384
        assert number(result, "feedbacks") > 0 and number(result, "learned") == 1
        assert number(result, "paced_packets") >= 31 and number(result, "min_gap_us") > 0
        print("PASS actual Linux TCP interoperability and native LARP feedback/pacing", flush=True)

        relay.configure("loss")
        result = jordy.command("tcp demo 192.0.2.1 9000", "TCP DEMO PASS")
        assert relay.dropped == {"syn", "upload", "fin"}, relay.dropped
        assert number(result, "retransmits") >= 3 and number(result, "timeouts") >= 2
        assert number(result, "fast_retransmits") >= 1
        print("PASS delayed path with lost SYN, upload segment and FIN", flush=True)

        relay.configure("receive_loss")
        jordy.command("tcp demo 192.0.2.1 9000", "TCP DEMO PASS")
        assert relay.dropped == {"download"}, relay.dropped
        print("PASS Linux retransmission after lost download segment", flush=True)
        relay.configure("clean")

        jordy.command("tcp exchange 192.0.2.1 9000 demo.ape received.ape", "TCP TRANSFER PASS")
        jordy.command("ape received.ape", "DEMO PASS")
        jordy.command("tcp send 192.0.2.1 9001 demo.ape", "TCP TRANSFER PASS")
        jordy.command("tcp recv 192.0.2.1 9002 received.bin", "TCP TRANSFER PASS")
        jordy.command("tcp send 192.0.2.1 9001 received.bin", "TCP TRANSFER PASS")
        linux.drain()
        binary_hash = hashlib.sha256(bytes((i*37+(i>>8)) & 255 for i in range(16384))).hexdigest()
        file_hash = hashlib.sha256(Path("programs/demo.ape").read_bytes()).hexdigest()
        log = linux.log.decode(errors="replace")
        assert f"LINUX_RX_SHA256 {binary_hash}" in log, log
        assert f"LINUX_UPLOAD_SHA256 {binary_hash}" in log, log
        assert f"LINUX_UPLOAD_SHA256 {file_hash}" in log, log
        print("PASS RAM-file upload/download/exchange and Linux SHA256 checks", flush=True)

        jordy.command("tcp demo 192.0.2.1 9999", "TCP failed: connection refused")
        jordy.command("ebpfw default deny outgoing", "Default updated")
        jordy.command("ebpfw delete 2", "Rule deleted")
        jordy.write(b"tcp demo 192.0.2.1 9000\n")
        jordy.until(b"TCP connecting; default controller=LARP.\r\n")
        time.sleep(.1)
        jordy.write(b"\x03")
        result = jordy.until(b"sh> ")
        assert "TCP failed: cancelled" in result, result
        stats = jordy.command("ebpfw stats", "faults=0")
        assert re.search(r"out allowed=\d+ denied=[1-9]\d*", stats), stats
        assert relay.error is None, relay.error
        print(f"PASS refusal, firewall enforcement and cancellation; {relay.frames} Ethernet frames", flush=True)
    finally:
        if relay.error:
            print(f"Relay error: {relay.error!r}", flush=True)
        if os.environ.get("TCP_TRACE"):
            print(f"Last TCP frames: {relay.trace}", flush=True)
        relay.close()
        for guest, name in guests:
            guest.close()
            path = Path(os.environ.get("TCP_LOG_DIR", "/tmp")) / f"jordyos-tcp-{name}.log"
            path.write_bytes(guest.log)
            print(f"Transcript: {path}", flush=True)
        jc.close()
        lc.close()


if __name__ == "__main__":
    main()
