import pathlib
import struct
import tempfile
import unittest
from embed_user import embed, BASE, LIMIT


class UserImage(unittest.TestCase):
    def setUp(self):
        self.data=pathlib.Path('user/runtime.elf').read_bytes()
        self.temp=tempfile.TemporaryDirectory(prefix='jordyos-user-elf-')
        self.addCleanup(self.temp.cleanup)
        self.source=pathlib.Path(self.temp.name)/'app.elf'
        self.destination=pathlib.Path(self.temp.name)/'app.h'
        self.phoff=struct.unpack_from('<Q',self.data,32)[0]

    def rejects(self,data):
        self.source.write_bytes(data)
        with self.assertRaises((ValueError,struct.error)):
            embed(self.source,self.destination)
        self.assertFalse(self.destination.exists())

    def test_real_image(self):
        self.source.write_bytes(self.data); embed(self.source,self.destination)
        text=self.destination.read_text()
        self.assertIn('USER_ENTRY 0x40000000ul',text)
        self.assertIn('user_segments[]',text)

    def test_rejects_writable_executable_code(self):
        data=bytearray(self.data); struct.pack_into('<I',data,self.phoff+4,7)
        self.rejects(data)

    def test_rejects_out_of_range_and_truncated_segments(self):
        data=bytearray(self.data); struct.pack_into('<Q',data,self.phoff+16,BASE+LIMIT)
        self.rejects(data)
        data=bytearray(self.data); struct.pack_into('<Q',data,self.phoff+32,len(data)*2)
        self.rejects(data)
        self.rejects(self.data[:63])

    def test_rejects_native_kernel_and_dynamic_image(self):
        self.rejects(pathlib.Path('kernel.elf').read_bytes())
        data=bytearray(self.data); struct.pack_into('<I',data,self.phoff,2)
        self.rejects(data)


if __name__=='__main__':
    unittest.main()
