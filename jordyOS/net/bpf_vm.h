#ifndef JORDY_BPF_VM_H
#define JORDY_BPF_VM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Little-endian eBPF instruction encoding. No native pointers enter the VM. */
typedef struct { uint8_t code, regs; int16_t offset; int32_t immediate; } BpfInsn;
typedef char BpfInstructionMustBe8[(sizeof(BpfInsn) == 8) ? 1 : -1];
#define BPF_MAX_INSNS 4096
#define BPF_FUEL 65536
#define BPF_MAPS 7
#define BPF_CTX 0x10000000u
#define BPF_DATA 0x20000000u
#define BPF_STACK 0x30000000u
#define BPF_VALUES 0x40000000u
#define BPF_HANDLES 0x80000000u
enum { BPF_ARRAY = 2, BPF_PERCPU_ARRAY = 6, BPF_LPM = 11, BPF_MAP_ARRAY = 12 };
typedef struct {
    uint32_t type, key_size, value_size, capacity, entries;
    uint8_t *values;
    const uint8_t *keys;
    bool writable;
} BpfMap;
typedef struct {
    const uint8_t *context, *packet;
    uint32_t context_size, packet_size;
    BpfMap maps[BPF_MAPS];
} BpfEnvironment;
typedef struct { bool ok; uint64_t value; uint32_t steps; } BpfResult;
bool bpf_validate(const BpfInsn *code, size_t count);
BpfResult bpf_execute(const BpfInsn *code, size_t count, BpfEnvironment *environment);
#endif
