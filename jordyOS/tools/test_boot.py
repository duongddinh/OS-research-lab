"""Check live long-mode control registers/page tables and unsupported CPUs."""
import os
import pathlib
import re
import selectors
import socket
import subprocess
import tempfile
import time
import unittest


class Boot(unittest.TestCase):
    def start(self, cpu):
        directory = tempfile.TemporaryDirectory(prefix='jordyos-boot-')
        self.addCleanup(directory.cleanup)
        monitor_path = pathlib.Path(directory.name) / 'monitor.sock'
        process = subprocess.Popen([
            os.environ.get('QEMU', 'qemu-system-x86_64'), '-cpu', cpu,
            '-drive', 'if=floppy,format=raw,file=os.img', '-boot', 'a', '-m', '32',
            '-snapshot', '-display', 'none', '-nic', 'none', '-serial', 'stdio',
            '-monitor', f'unix:{monitor_path},server=on,wait=off', '-no-reboot'],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

        def stop():
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill(); process.wait()
            process.stdin.close(); process.stdout.close()
        self.addCleanup(stop)
        selector = selectors.DefaultSelector()
        selector.register(process.stdout, selectors.EVENT_READ)
        self.addCleanup(selector.close)

        def until(marker):
            data = bytearray(); deadline = time.monotonic() + 15
            while marker not in data and time.monotonic() < deadline:
                if selector.select(.1):
                    chunk = os.read(process.stdout.fileno(), 65536)
                    if not chunk:
                        break
                    data.extend(chunk)
            self.assertIn(marker, data, data.decode(errors='replace'))
            return data.decode(errors='replace').replace('\r', '')
        return process, monitor_path, until

    def test_long_mode_and_identity_tables(self):
        process, monitor_path, until = self.start('qemu64')
        until(b'desktop> ')
        process.stdin.write(b'4'); process.stdin.flush(); until(b'sh> ')
        process.stdin.write(b'arch\n'); process.stdin.flush()
        report = until(b'sh> ')
        for expected in ['pointers=64 bits; long=64 bits', 'EFER.LMA=1', 'ABI: PASS',
                         'shared 32 MiB identity map', 'apps ring 3 with private CR3']:
            self.assertIn(expected, report)
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as monitor:
            monitor.settimeout(3); monitor.connect(str(monitor_path))

            def prompt():
                data = bytearray()
                while not data.endswith(b'(qemu) '):
                    chunk = monitor.recv(65536)
                    self.assertTrue(chunk, 'Monitor closed before prompt')
                    data.extend(chunk)
                return re.sub(r'\x1b\[[0-9;]*[A-Za-z]', '', data.decode(errors='replace'))

            prompt()  # Drain the whole greeting before sending the first command.
            def command(text):
                monitor.sendall(text.encode() + b'\n')
                return prompt()

            registers = command('info registers')
            self.assertRegex(registers, r'CS\s*=0018.*CS64')
            for name, mask in [('CR0', 0x80010001), ('CR4', 0x620), ('EFER', 0xd00)]:
                match = re.search(name + r'=([0-9a-fA-F]+)', registers)
                self.assertIsNotNone(match, registers)
                self.assertEqual(int(match[1], 16) & mask, mask, registers)
            self.assertEqual(int(re.search(r'CR3=([0-9a-fA-F]+)', registers)[1], 16), 0x80000)

            def words(address, count):
                result = command(f'xp /{count}gx {address:#x}')
                values = re.findall(r'0x([0-9a-fA-F]{16})', result)
                self.assertEqual(len(values), count, result)
                return [int(value, 16) for value in values]

            # Hardware may have set Accessed/Dirty bits; preserve all other flags.
            self.assertEqual(words(0x80000, 2), [0x81023, 0])
            self.assertEqual(words(0x81000, 2), [0x82023, 0])
            leaves = words(0x82000, 18)
            for i in range(16):
                self.assertEqual(leaves[i] & ~0x60, i * 0x200000 | 0x83)
                self.assertFalse(leaves[i] & 4, 'Page must be supervisor-only')
            self.assertEqual(leaves[16:], [0, 0])

    def test_unsupported_cpus_halt_cleanly(self):
        for cpu in ['pentium3', 'qemu64,-lm', 'qemu64,-sse2', 'qemu64,-nx']:
            with self.subTest(cpu=cpu):
                process, _, until = self.start(cpu)
                output = until(b'JordyOS requires an x86-64 CPU with SSE2 and NX.')
                self.assertNotIn('JordyOS x86-64 / long mode', output)
                self.assertIsNone(process.poll(), 'Guest should halt, not reset/triple fault')


if __name__ == '__main__':
    unittest.main(verbosity=2)
