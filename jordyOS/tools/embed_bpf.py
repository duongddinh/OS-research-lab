"""Load two BPF ELF64 sections, relocate named maps, and embed real instructions.

Only the narrow ELF surface emitted by the pinned clang build is accepted.
No executable native code, unresolved externals, CO-RE, or subprogram calls.
"""
import hashlib
import pathlib
import struct
import sys

MAPS = ['policy_template', 'active', 'stats', 'early4', 'early6', 'early_enabled', 'metadata']


def embed(source, output):
    raw = source.read_bytes()
    if len(raw) < 64 or raw[:7] != b'\x7fELF\x02\x01\x01':
        raise ValueError('Expected little-endian ELF64')
    header = struct.unpack_from('<16sHHIQQQIHHHHHH', raw)
    if header[1] != 1 or header[2] != 247 or header[11] != 64:
        raise ValueError('Expected relocatable EM_BPF object')
    offset, number, strings = header[6], header[12], header[13]
    if not number or strings >= number or offset + number * 64 > len(raw):
        raise ValueError('Invalid section table')
    sections = [struct.unpack_from('<IIQQQQIIQQ', raw, offset + i * 64) for i in range(number)]

    def contents(section):
        start, size = section[4:6]
        if start + size > len(raw):
            raise ValueError('Truncated section')
        return raw[start:start + size]

    def name(table, start):
        end = table.find(b'\0', start)
        if end < 0:
            raise ValueError('Invalid string table')
        return table[start:end].decode('ascii')

    section_strings = contents(sections[strings])
    names = [name(section_strings, s[0]) for s in sections]
    declarations = ['/* SPDX-License-Identifier: GPL-2.0-only */',
                    '/* Generated from actual clang -target bpfel bytecode. */',
                    '#ifndef JORDY_BPF_PROGRAMS_H', '#define JORDY_BPF_PROGRAMS_H',
                    '#include "../bpf_vm.h"',
                    f'#define BPF_OBJECT_SHA256 "{hashlib.sha256(raw).hexdigest()}"']
    for section_name, c_name in [('xdp', 'ebpfw_xdp_code'), ('jordyos/policy', 'ebpfw_policy_code')]:
        index = names.index(section_name)
        data = bytearray(contents(sections[index]))
        if not data or len(data) % 8 or len(data) // 8 > 4096:
            raise ValueError('Invalid BPF instruction count')
        relocations = 0
        for rel in sections:
            if rel[1] != 9 or rel[7] != index:  # SHT_REL, sh_info
                continue
            if rel[9] != 16 or rel[5] % 16:
                raise ValueError('Unsupported relocations')
            symbols = sections[rel[6]]
            if symbols[1] != 2 or symbols[9] != 24:
                raise ValueError('Invalid symbol table')
            symbol_data = contents(symbols)
            symbol_strings = contents(sections[symbols[6]])
            for at in range(0, rel[5], 16):
                where, info = struct.unpack_from('<QQ', contents(rel), at)
                symbol_index, kind = info >> 32, info & 0xffffffff
                symbol = struct.unpack_from('<IBBHQQ', symbol_data, symbol_index * 24)
                map_name = name(symbol_strings, symbol[0])
                if kind != 1 or map_name not in MAPS or names[symbol[3]] != 'maps':
                    raise ValueError(f'Unsupported relocation: {map_name}, type {kind}')
                if where % 8 or where + 16 > len(data) or data[where] != 0x18:
                    raise ValueError('Invalid map relocation instruction')
                data[where + 1] = data[where + 1] & 15 | 0x10  # BPF_PSEUDO_MAP_FD
                struct.pack_into('<i', data, where + 4, MAPS.index(map_name))
                struct.pack_into('<i', data, where + 12, 0)
                relocations += 1
        if not relocations:
            raise ValueError('Program must reference native BPF maps')
        declarations.append(f'static const BpfInsn {c_name}[] = {{')
        for at in range(0, len(data), 8):
            opcode, registers, branch, immediate = struct.unpack_from('<BBhi', data, at)
            declarations.append(f'    {{0x{opcode:02x}, 0x{registers:02x}, {branch}, {immediate}}},')
        declarations.append('};')
    declarations.append('#endif\n')
    output.write_text('\n'.join(declarations))


if __name__ == '__main__':
    embed(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]))
