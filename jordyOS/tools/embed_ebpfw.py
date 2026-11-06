"""Derive the supported native adapter's headers from the pinned ebpfw submodule.

Upstream's single stateful.c also contains Linux-only hooks/maps/kfuncs. Select
the original declarations/helpers/XDP body, without modifying the submodule.
Reject source changes until both the pin and adapter have been reviewed.
"""
import hashlib
import json
from pathlib import Path
import sys


def declaration(source, anchor, structure=False):
    if source.count(anchor) != 1:
        raise ValueError(f"Expected one upstream declaration: {anchor}")
    start = source.index(anchor)
    brace = source.index("{", start)
    depth = 0
    for end in range(brace, len(source)):
        depth += (source[end] == "{") - (source[end] == "}")
        if not depth:
            end += 1
            if structure:
                if source[end:end+1] != ";":
                    raise ValueError(f"Missing struct terminator: {anchor}")
                end += 1
            return source[start:end] + "\n"
    raise ValueError(f"Unterminated upstream declaration: {anchor}")


def generate(vendor, manifest):
    pin = json.loads(manifest.read_text())
    raw = (vendor / pin["source"]).read_bytes()
    if hashlib.sha256(raw).hexdigest() != pin["sha256"]:
        raise ValueError("ebpfw source differs from reviewed hash; review the pin and native adapter")
    source = raw.decode()
    banner = ("/* SPDX-License-Identifier: GPL-2.0-only\n"
        f" * Generated from vendor/ebpfw at {pin['revision']}; do not edit.\n"
        " * Source selection and xdp.frags -> xdp adaptation: tools/embed_ebpfw.py.\n"
        " * See UPSTREAM.md for scope and vendor/ebpfw/LICENSE for licensing. */\n")
    shared = banner + "#ifndef JORDY_EBPFW_SHARED_H\n#define JORDY_EBPFW_SHARED_H\n"
    for line in ["typedef unsigned char u8;", "typedef unsigned short u16;",
                 "typedef unsigned int u32;", "typedef unsigned long long u64;",
                 "#define INLINE static __inline __attribute__((always_inline))"]:
        if source.count(line) != 1:
            raise ValueError(f"Missing upstream type/macro: {line}")
        shared += line + "\n"
    for name in ["rule", "header", "packet", "counter", "lpm_key"]:
        shared += declaration(source, f"struct {name} {{", structure=True)
    for name in ["u16 be16", "u32 be32", "int prefix", "int name_equal", "int matches"]:
        shared += declaration(source, f"INLINE {name}(")
    shared += "#endif\n"
    early = declaration(source, 'SEC("xdp.frags") int ebpfw_early(')
    early = early.replace('SEC("xdp.frags")', 'SEC("xdp")', 1)
    count = declaration(source, "INLINE void count(")
    return {"ebpfw_shared.h": shared, "ebpfw_early.h": banner + early,
            "ebpfw_count.h": banner + count}


if __name__ == "__main__":
    vendor, manifest, output = map(Path, sys.argv[1:])
    for name, text in generate(vendor, manifest).items():
        (output / name).write_text(text)
    print("Generated ebpfw declarations, matcher, counter and XDP body from pinned submodule")
