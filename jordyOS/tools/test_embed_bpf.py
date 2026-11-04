"""Reject malformed/unsupported ELF before it enters the built-in VM image."""
import pathlib
import struct
import tempfile
import unittest
from embed_bpf import embed


class BpfElf(unittest.TestCase):
    def setUp(self):
        self.raw = pathlib.Path('net/bpf/program.bpf.o').read_bytes()
        self.temporary = tempfile.TemporaryDirectory(prefix='jordyos-bpf-elf-')
        self.addCleanup(self.temporary.cleanup)
        self.source = pathlib.Path(self.temporary.name) / 'program.o'
        self.output = pathlib.Path(self.temporary.name) / 'program.h'

    def rejects(self, data):
        self.source.write_bytes(data)
        with self.assertRaises((ValueError, IndexError, struct.error)):
            embed(self.source, self.output)
        self.assertFalse(self.output.exists())

    def test_real_program_and_relocations(self):
        self.source.write_bytes(self.raw)
        embed(self.source, self.output)
        text = self.output.read_text()
        self.assertIn('ebpfw_xdp_code[]', text)
        self.assertIn('ebpfw_policy_code[]', text)
        self.assertIn('BPF_OBJECT_SHA256', text)
        self.assertIn('0x18, 0x11', text)  # relocated pseudo-map load into r1

    def test_truncated_elf(self):
        for size in [0, 16, 63, len(self.raw) - 1]:
            self.rejects(self.raw[:size])

    def test_wrong_machine_and_byte_order(self):
        data = bytearray(self.raw); data[18:20] = struct.pack('<H', 62)
        self.rejects(data)
        data = bytearray(self.raw); data[5] = 2
        self.rejects(data)

    def test_unsupported_relocation(self):
        data = bytearray(self.raw)
        header = struct.unpack_from('<16sHHIQQQIHHHHHH', data)
        found = False
        for i in range(header[12]):
            section = struct.unpack_from('<IIQQQQIIQQ', data, header[6] + i * 64)
            if section[1] == 9 and section[5]:
                info, = struct.unpack_from('<Q', data, section[4] + 8)
                struct.pack_into('<Q', data, section[4] + 8, info & ~0xffffffff | 10)
                found = True
                break
        self.assertTrue(found)
        self.rejects(data)


if __name__ == '__main__':
    unittest.main()
