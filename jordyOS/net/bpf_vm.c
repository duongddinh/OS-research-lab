#include "bpf_vm.h"

/* Structural instruction validation plus checked runtime memory/fuel. This is
   deliberately not Linux's pointer/dataflow verifier. Only helper 1 is exposed. */
static bool target(const BpfInsn *code, size_t count, size_t pc, int32_t offset) {
    int64_t next = (int64_t)pc + 1 + offset;
    return next >= 0 && (uint64_t)next < count && code[next].code != 0;
}
bool bpf_validate(const BpfInsn *code, size_t count) {
    if (!count || count > BPF_MAX_INSNS) return false;
    bool exit = false;
    for (size_t pc = 0; pc < count; pc++) {
        BpfInsn i = code[pc]; unsigned d = i.regs & 15, s = i.regs >> 4;
        unsigned cls = i.code & 7, op = i.code & 0xf0;
        if (d > 10 || s > 10) return false;
        if (i.code == 0x18) {
            if (d == 10 || i.offset || s > 1 || pc + 1 >= count) return false;
            BpfInsn hi = code[++pc];
            if (hi.code || hi.regs || hi.offset) return false;
            if (s && (i.immediate < 0 || i.immediate >= BPF_MAPS || hi.immediate)) return false;
        } else if (cls == 4 || cls == 7) {
            if (d == 10 || i.offset) return false;
            if (op == 0xd0) {
                if (cls != 4 || s || (i.immediate != 16 && i.immediate != 32 && i.immediate != 64)) return false;
            } else {
                if (op > 0xc0 || (op == 0x80 && (i.code & 8))) return false;
                if ((i.code & 8) ? i.immediate != 0 : s != 0) return false;
                if (op == 0x80 && i.immediate) return false;
            }
        } else if (cls == 1 || cls == 2 || cls == 3) {
            if ((i.code & 0xe0) != 0x60) return false; /* MEM only, no atomics/legacy loads */
            if (cls == 1 && (d == 10 || i.immediate)) return false;
            if (cls == 2 && s) return false;
            if (cls == 3 && i.immediate) return false;
        } else if (cls == 5 || cls == 6) {
            if (op == 0x80) {
                if (i.code != 0x85 || i.regs || i.offset || i.immediate != 1) return false;
            } else if (op == 0x90) {
                if (i.code != 0x95 || i.regs || i.offset || i.immediate) return false;
                exit = true;
            } else {
                if (op > 0xd0 || op == 0xe0 || op == 0xf0) return false;
                if (op == 0) {
                    if (i.code != 0x05 || i.regs || i.immediate) return false;
                } else if ((i.code & 8) ? i.immediate != 0 : s != 0) return false;
                if (!target(code, count, pc, i.offset)) return false;
            }
        } else return false;
    }
    return exit;
}
static uint64_t read_le(const uint8_t *p, unsigned size) {
    uint64_t n = 0;
    for (unsigned i = 0; i < size; i++) n |= (uint64_t)p[i] << (i * 8);
    return n;
}
static uint8_t *memory(BpfEnvironment *e, uint8_t *stack, uint64_t address,
                       unsigned size, bool write) {
    if (address >= BPF_STACK && address - BPF_STACK <= 512 && size <= 512 - (address - BPF_STACK))
        return stack + (size_t)(address - BPF_STACK);
    if (!write && address >= BPF_CTX && address - BPF_CTX <= e->context_size &&
        size <= e->context_size - (address - BPF_CTX))
        return (uint8_t *)e->context + (size_t)(address - BPF_CTX);
    if (!write && address >= BPF_DATA && address - BPF_DATA <= e->packet_size &&
        size <= e->packet_size - (address - BPF_DATA))
        return (uint8_t *)e->packet + (size_t)(address - BPF_DATA);
    if (address < BPF_VALUES || address >= BPF_VALUES + BPF_MAPS * 0x100000u) return NULL;
    unsigned id = (unsigned)(address - BPF_VALUES) >> 20;
    unsigned offset = (unsigned)(address - BPF_VALUES) & 0xfffff;
    unsigned index = offset >> 8, within = offset & 255;
    BpfMap *m = &e->maps[id];
    if (!m->values || m->value_size > 256 || index >= m->capacity ||
        within > m->value_size || size > m->value_size - within || (write && !m->writable)) return NULL;
    return m->values + index * m->value_size + within;
}
static bool lookup(BpfEnvironment *e, uint8_t *stack, uint64_t handle, uint64_t key_address, uint64_t *result) {
    if (handle < BPF_HANDLES || handle >= BPF_HANDLES + BPF_MAPS) return false;
    unsigned id = (unsigned)(handle - BPF_HANDLES);
    BpfMap *m = &e->maps[id];
    if (!m->values || !m->value_size || m->value_size > 256 || m->capacity > 4096 ||
        (m->key_size != 4 && m->key_size != 20)) return false;
    uint8_t *key = memory(e, stack, key_address, m->key_size, false);
    if (!key) return false;
    uint32_t index = (uint32_t)read_le(key, 4);
    *result = 0;
    if (m->type == BPF_LPM) {
        if (m->key_size != 20 || !m->keys || index > 128 || m->entries > m->capacity) return false;
        int best = -1; unsigned best_bits = 0;
        for (unsigned i = 0; i < m->entries; i++) {
            const uint8_t *prefix = m->keys + i * 20;
            unsigned bits = (unsigned)read_le(prefix, 4);
            if (bits > index || bits > 128 || (best >= 0 && bits <= best_bits)) continue;
            bool match = true;
            for (unsigned b = 0; b < bits; b++)
                if ((key[4 + b / 8] ^ prefix[4 + b / 8]) & (0x80u >> (b % 8))) { match = false; break; }
            if (match) { best = (int)i; best_bits = bits; }
        }
        if (best < 0) return true;
        index = (unsigned)best;
    } else if (m->type != BPF_ARRAY && m->type != BPF_PERCPU_ARRAY && m->type != BPF_MAP_ARRAY) return false;
    if (index >= m->capacity) return true;
    if (m->type == BPF_MAP_ARRAY) {
        if (m->value_size != 4) return false;
        unsigned inner = (unsigned)read_le(m->values + index * 4, 4);
        if (inner >= BPF_MAPS) return false;
        *result = BPF_HANDLES + inner;
    } else *result = BPF_VALUES + id * 0x100000u + index * 256;
    return true;
}
static uint64_t divide(uint64_t n, uint64_t d, bool remainder) {
    /* Avoid compiler-rt's 64-bit division dependency in the i686 kernel. */
    if (!d) return remainder ? n : 0;
    uint64_t q = 0, r = 0;
    for (unsigned i = 64; i; i--) {
        bool high = (r >> 63) != 0;
        r = (r << 1) | ((n >> (i - 1)) & 1);
        if (high || r >= d) { r -= d; q |= (uint64_t)1 << (i - 1); }
    }
    return remainder ? r : q;
}
BpfResult bpf_execute(const BpfInsn *code, size_t count, BpfEnvironment *e) {
    uint64_t r[11] = {0}; uint8_t stack[512] = {0};
    BpfResult result = {0}; size_t pc = 0;
    if (!count || count > BPF_MAX_INSNS) return result;
    r[1] = BPF_CTX; r[10] = BPF_STACK + sizeof stack;
    while (pc < count && result.steps < BPF_FUEL) {
        BpfInsn i = code[pc++]; result.steps++;
        unsigned d = i.regs & 15, s = i.regs >> 4, cls = i.code & 7, op = i.code & 0xf0;
        if (d > 10 || s > 10) return result;
        if (i.code == 0x18) {
            if (pc >= count || d == 10 || s > 1) return result;
            r[d] = (uint32_t)i.immediate | (uint64_t)(uint32_t)code[pc++].immediate << 32;
            if (s) { if (r[d] >= BPF_MAPS) return result; r[d] += BPF_HANDLES; }
        } else if (cls == 4 || cls == 7) {
            if (d == 10) return result;
            bool wide = cls == 7;
            uint64_t a = wide ? r[d] : (uint32_t)r[d];
            uint64_t b = (i.code & 8) ? r[s] : (uint64_t)(int64_t)i.immediate;
            if (!wide) b = (uint32_t)b;
            unsigned shift = (unsigned)b & (wide ? 63 : 31);
            switch (op) {
                case 0x00: a += b; break; case 0x10: a -= b; break;
                case 0x20: a *= b; break; case 0x30: a = divide(a, b, false); break;
                case 0x40: a |= b; break; case 0x50: a &= b; break;
                case 0x60: a <<= shift; break; case 0x70: a >>= shift; break;
                case 0x80: a = 0 - a; break; case 0x90: a = divide(a, b, true); break;
                case 0xa0: a ^= b; break; case 0xb0: a = b; break;
                case 0xc0: a = wide ? (uint64_t)((int64_t)a >> shift) : (uint32_t)((int32_t)a >> shift); break;
                case 0xd0: {
                    if (wide || (i.immediate != 16 && i.immediate != 32 && i.immediate != 64)) return result;
                    unsigned bits = (unsigned)i.immediate;
                    a = r[d];
                    if (bits < 64) a &= ((uint64_t)1 << bits) - 1;
                    if (i.code & 8) {
                        uint64_t swapped = 0;
                        for (unsigned byte = 0; byte < bits / 8; byte++) { swapped = swapped << 8 | (a & 255); a >>= 8; }
                        a = swapped;
                    }
                    r[d] = a; continue;
                }
                default: return result;
            }
            r[d] = wide ? a : (uint32_t)a;
        } else if (cls == 1 || cls == 2 || cls == 3) {
            if ((i.code & 0xe0) != 0x60 || (cls == 1 && d == 10)) return result;
            unsigned size = (i.code & 0x18) == 0 ? 4 : (i.code & 0x18) == 8 ? 2 : (i.code & 0x18) == 16 ? 1 : 8;
            uint64_t address = r[cls == 1 ? s : d] + (uint64_t)(int64_t)i.offset;
            uint8_t *p = memory(e, stack, address, size, cls != 1);
            if (!p) return result;
            if (cls == 1) r[d] = read_le(p, size);
            else {
                uint64_t value = cls == 3 ? r[s] : (uint64_t)(int64_t)i.immediate;
                for (unsigned byte = 0; byte < size; byte++) p[byte] = value >> (byte * 8);
            }
        } else if (cls == 5 || cls == 6) {
            if (i.code == 0x95) { result.ok = true; result.value = r[0]; return result; }
            if (i.code == 0x85) {
                if (i.immediate != 1 || !lookup(e, stack, r[1], r[2], &r[0])) return result;
                for (unsigned reg = 1; reg <= 5; reg++) r[reg] = 0;
                continue;
            }
            uint64_t a = r[d], b = (i.code & 8) ? r[s] : (uint64_t)(int64_t)i.immediate;
            int64_t sa, sb;
            if (cls == 6) { a = (uint32_t)a; b = (uint32_t)b; sa = (int32_t)a; sb = (int32_t)b; }
            else { sa = (int64_t)a; sb = (int64_t)b; }
            bool jump;
            switch (op) {
                case 0x00: jump = true; break; case 0x10: jump = a == b; break;
                case 0x20: jump = a > b; break; case 0x30: jump = a >= b; break;
                case 0x40: jump = (a & b) != 0; break; case 0x50: jump = a != b; break;
                case 0x60: jump = sa > sb; break; case 0x70: jump = sa >= sb; break;
                case 0xa0: jump = a < b; break; case 0xb0: jump = a <= b; break;
                case 0xc0: jump = sa < sb; break; case 0xd0: jump = sa <= sb; break;
                default: return result;
            }
            if (jump) {
                int64_t next = (int64_t)pc + i.offset;
                if (next < 0 || (uint64_t)next >= count || !code[next].code) return result;
                pc = (size_t)next;
            }
        } else return result;
    }
    return result; /* Missing exit, bad access, or fuel exhaustion fails closed. */
}
