"""Embed a fixed-address freestanding ELF64 user image and page permissions."""
import pathlib
import struct
import sys

BASE = 0x40000000
LIMIT = 8 * 1024 * 1024 - 4096  # reserve a private diagnostic marker page


def embed(source, destination):
    data = source.read_bytes()
    if len(data) < 64 or data[:7] != b'\x7fELF\x02\x01\x01':
        raise ValueError('User image must be little-endian ELF64')
    h = struct.unpack_from('<16sHHIQQQIHHHHHH', data)
    if h[1] != 2 or h[2] != 62 or h[9] != 56 or h[4] != BASE:
        raise ValueError('Expected static x86-64 user executable at USER_BASE')
    if not h[10] or h[10] > 16 or h[5] + h[10] * 56 > len(data):
        raise ValueError('Invalid user program-header table')
    payload = bytearray(); segments = []
    for i in range(h[10]):
        kind, flags, offset, virtual, physical, filesz, memsz, alignment = struct.unpack_from('<IIQQQQQQ', data, h[5] + i * 56)
        if kind in (2, 3):
            raise ValueError('Dynamic user images are not supported')
        if kind != 1:
            continue
        relative = virtual - BASE
        if relative < 0 or virtual % 4096 or relative + memsz > LIMIT or filesz > memsz or offset + filesz > len(data):
            raise ValueError('Invalid user segment range/alignment')
        if not flags & 4 or flags & ~7 or flags & 3 == 3:
            raise ValueError('User segments must be readable and cannot be writable+executable')
        for previous in segments:
            if relative < previous[0] + (previous[1] + 4095) // 4096 * 4096 and relative + memsz > previous[0]:
                raise ValueError('User segment pages overlap')
        segments.append((relative, memsz, filesz, len(payload), flags))
        payload.extend(data[offset:offset + filesz])
    if not segments or segments[0][0] != 0 or segments[0][4] != 5 or not segments[0][2]:
        raise ValueError('User entry must be in the initial RX segment')
    lines = ['/* Generated from the linked user ELF; not kernel code. */',
             '#ifndef JORDY_USER_IMAGE_H', '#define JORDY_USER_IMAGE_H',
             'typedef struct { unsigned offset, memory_size, file_size, payload_offset, flags; } UserSegment;',
             f'#define USER_ENTRY 0x{h[4]:x}ul',
             'static const unsigned char user_image_bytes[] = {']
    for i in range(0, len(payload), 16):
        lines.append('    ' + ','.join(f'0x{b:02x}' for b in payload[i:i+16]) + ',')
    lines += ['};', 'static const UserSegment user_segments[] = {']
    lines += ['    {' + ','.join(map(str, s)) + '},' for s in segments]
    lines += ['};', '#endif\n']
    destination.write_text('\n'.join(lines))
    print(f'User image: {len(payload)} embedded bytes; {len(segments)} protected segments')


if __name__ == '__main__':
    embed(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]))
