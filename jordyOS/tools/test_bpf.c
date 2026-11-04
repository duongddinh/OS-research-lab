#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "net/bpf_vm.h"
#include "net/fw_bpf.h"
#define I(c,d,s,o,v) {c, (d) | (s) << 4, o, v}
#define EXIT I(0x95,0,0,0,0)
#define WIDE(d,lo,hi) I(0x18,d,0,0,(int32_t)(lo)), I(0,0,0,0,(int32_t)(hi))
#define MAP(d,n) I(0x18,d,1,0,n), I(0,0,0,0,0)
#define N(a) (sizeof(a)/sizeof(*(a)))
static BpfResult run(const BpfInsn *p, size_t n, BpfEnvironment *e) {
    assert(bpf_validate(p, n));
    return bpf_execute(p, n, e);
}
static void arithmetic(void) {
    BpfEnvironment e = {0}; BpfResult r;
    BpfInsn carry[] = {WIDE(0,0xffffffff,0xffffffff),I(0x07,0,0,0,1),EXIT};
    r = run(carry,N(carry),&e); assert(r.ok && r.value == 0);
    BpfInsn alu32[] = {I(0xb4,0,0,0,-1),I(0x07,0,0,0,1),EXIT};
    r = run(alu32,N(alu32),&e); assert(r.ok && r.value == 0x100000000ULL);
    BpfInsn shift[] = {I(0xb7,0,0,0,1),I(0x67,0,0,0,64),EXIT};
    r = run(shift,N(shift),&e); assert(r.ok && r.value == 1);
    BpfInsn divide[] = {WIDE(0,0x76543210,0xfedcba98),I(0xb7,1,0,0,13),I(0x3f,0,1,0,0),EXIT};
    r = run(divide,N(divide),&e); assert(r.ok && r.value == 0xfedcba9876543210ULL/13);
    divide[3].code = 0x9f;
    r = run(divide,N(divide),&e); assert(r.ok && r.value == 0xfedcba9876543210ULL%13);
    divide[2].immediate = 0;
    r = run(divide,N(divide),&e); assert(r.ok && r.value == 0xfedcba9876543210ULL);
    divide[3].code = 0x3f;
    r = run(divide,N(divide),&e); assert(r.ok && r.value == 0);
    BpfInsn endian[] = {WIDE(0,0x05060708,0x01020304),I(0xdc,0,0,0,64),EXIT};
    r = run(endian,N(endian),&e); assert(r.ok && r.value == 0x0807060504030201ULL);
    endian[2].immediate = 16;
    r = run(endian,N(endian),&e); assert(r.ok && r.value == 0x0807);
    BpfInsn signed_jump[] = {I(0xb4,0,0,0,(int32_t)0x80000000u),I(0xc6,0,0,1,0),I(0xb7,0,0,0,99),EXIT};
    r = run(signed_jump,N(signed_jump),&e); assert(r.ok && r.value == 0x80000000u);
    puts("PASS eBPF 64/32-bit arithmetic, division/modulo, shifts, endian and signed branches");
}
static void validation_and_memory(void) {
    BpfEnvironment e = {0}; BpfResult r;
    BpfInsn stack[] = {I(0x62,10,0,-7,0x12345678),I(0x61,0,10,-7,0),EXIT};
    r = run(stack,N(stack),&e); assert(r.ok && r.value == 0x12345678);
    stack[1].offset = 0;
    assert(!run(stack,N(stack),&e).ok);
    BpfInsn zero_stack[] = {I(0x71,0,10,-1,0),EXIT};
    r = run(zero_stack,N(zero_stack),&e); assert(r.ok && !r.value);
    uint8_t data[4] = {1,2,3,4}; e.packet = data; e.packet_size = sizeof data;
    BpfInsn packet[] = {I(0xb7,1,0,0,BPF_DATA),I(0x61,0,1,0,0),EXIT};
    r = run(packet,N(packet),&e); assert(r.ok && r.value == 0x04030201);
    packet[1].offset = 1; assert(!run(packet,N(packet),&e).ok);
    packet[1] = (BpfInsn)I(0x62,1,0,0,7); assert(!run(packet,N(packet),&e).ok);
    assert(data[0] == 1); /* packet region is read-only */
    packet[0].immediate = 0x1234; assert(!run(packet,N(packet),&e).ok);
    BpfInsn loop[] = {I(0x05,0,0,-1,0),EXIT};
    r = run(loop,N(loop),&e); assert(!r.ok && r.steps == BPF_FUEL);
    BpfInsn invalid[] = {I(0xb7,10,0,0,0),EXIT}; assert(!bpf_validate(invalid,N(invalid)));
    invalid[0] = (BpfInsn)I(0x85,0,0,0,2); assert(!bpf_validate(invalid,N(invalid)));
    invalid[0] = (BpfInsn)I(0x85,0,1,0,1); assert(!bpf_validate(invalid,N(invalid)));
    invalid[0] = (BpfInsn)I(0x05,0,0,9,0); assert(!bpf_validate(invalid,N(invalid)));
    invalid[0] = (BpfInsn)I(0xc3,1,0,0,0); assert(!bpf_validate(invalid,N(invalid)));
    invalid[0] = (BpfInsn)I(0xb7,11,0,0,0); assert(!bpf_validate(invalid,N(invalid)));
    BpfInsn wide[] = {I(0x05,0,0,1,0),WIDE(0,1,0),EXIT}; assert(!bpf_validate(wide,N(wide)));
    wide[0].offset = 2; assert(bpf_validate(wide,N(wide)));
    wide[2].regs = 1; assert(!bpf_validate(wide,N(wide)));
    assert(!bpf_validate(wide,0) && !bpf_validate(wide,BPF_MAX_INSNS+1));
    puts("PASS eBPF instruction validation, immutable FP, virtual memory bounds, read-only packets, fuel limit");
}
static void maps(void) {
    BpfEnvironment e = {0}; BpfResult r;
    uint32_t values[] = {42,99};
    e.maps[0] = (BpfMap){BPF_ARRAY,4,4,2,0,(uint8_t *)values,NULL,false};
    BpfInsn program[] = {I(0x62,10,0,-4,1),I(0xbf,2,10,0,0),I(0x07,2,0,0,-4),MAP(1,0),
        I(0x85,0,0,0,1),I(0x61,0,0,0,0),EXIT};
    r = run(program,N(program),&e); assert(r.ok && r.value == 99);
    program[6].offset = 1; assert(!run(program,N(program),&e).ok); /* cross-value access */
    program[6] = (BpfInsn)I(0x62,0,0,0,9); assert(!run(program,N(program),&e).ok);
    assert(values[1] == 99);
    e.maps[0].writable = true;
    r = run(program,N(program),&e); assert(r.ok && values[1] == 9);
    program[6] = (BpfInsn)I(0x61,0,1,0,0); assert(!run(program,N(program),&e).ok); /* r1 clobbered by helper */
    program[6] = (BpfInsn)I(0x61,0,0,0,0); program[0].immediate = 2;
    assert(!run(program,N(program),&e).ok); /* lookup miss -> null, dereference traps */
    program[3].immediate = BPF_MAPS; assert(!bpf_validate(program,N(program)));
    /* Longest-prefix matching is a native map primitive, not a policy helper. */
    uint8_t keys[40] = {24,0,0,0,192,0,2};
    keys[20] = 32; keys[24] = 192; keys[26] = 2; keys[27] = 1;
    e.maps[0] = (BpfMap){BPF_LPM,20,4,2,2,(uint8_t *)values,keys,false};
    BpfInsn lpm[] = {I(0x62,10,0,-24,32),I(0x62,10,0,-20,0x010200c0),
        I(0x7a,10,0,-16,0),I(0x7a,10,0,-8,0),I(0xbf,2,10,0,0),I(0x07,2,0,0,-24),
        MAP(1,0),I(0x85,0,0,0,1),I(0x61,0,0,0,0),EXIT};
    r = run(lpm,N(lpm),&e); assert(r.ok && r.value == 9);
    lpm[1].immediate = 0x030200c0;
    r = run(lpm,N(lpm),&e); assert(r.ok && r.value == 42);
    uint32_t inner = 1;
    e.maps[0] = (BpfMap){BPF_MAP_ARRAY,4,4,1,0,(uint8_t *)&inner,NULL,false};
    e.maps[1] = (BpfMap){BPF_ARRAY,4,4,2,0,(uint8_t *)values,NULL,false};
    BpfInsn nested[] = {I(0x62,10,0,-4,0),I(0xbf,6,10,0,0),I(0x07,6,0,0,-4),
        I(0xbf,2,6,0,0),MAP(1,0),I(0x85,0,0,0,1),I(0xbf,1,0,0,0),
        I(0xbf,2,6,0,0),I(0x85,0,0,0,1),I(0x61,0,0,0,0),EXIT};
    r = run(nested,N(nested),&e); assert(r.ok && r.value == 42);
    puts("PASS BPF array/map-of-maps/LPM lookup, null handling, helper ABI, per-value bounds and write permissions");
}
static void actual_ebpfw(void) {
    assert(fw_bpf_ready() && fw_bpf_policy_size() > 100 && fw_bpf_xdp_size() > 100);
    Firewall fw; fw_reset(&fw); fw.enabled = true;
    FirewallRule allow = {.allow = true, .direction = FW_IN};
    FirewallRule block = {.block = true, .source = 0xc0000200, .source_mask = 0xffffff00};
    assert(fw_add(&fw,&allow) && fw_add(&fw,&block));
    Packet p = {.source = 0xc0000201, .destination = NET_IP, .protocol = NET_UDP};
    assert(!fw_accept(&fw,&p,FW_IN)); /* Absolute block overrides earlier allow in actual BPF. */
    uint8_t frame[80] = {0};
    net_write16(frame+12,0x0800); frame[14] = 0x45;
    net_write32(frame+26,p.source);
    assert(!fw_bpf_xdp(&fw,frame,60));
    assert(fw.rules[1].hits == 2 && firewall_bpf_stats.xdp_drops == 1);
    /* Upstream XDP handles two VLAN tags even though the host stack does not. */
    net_write16(frame+12,0x8100); net_write16(frame+16,0x88a8); net_write16(frame+20,0x0800);
    frame[22] = 0x45; net_write32(frame+34,p.source);
    assert(!fw_bpf_xdp(&fw,frame,60));
    fw.enabled = false; assert(fw_bpf_xdp(&fw,frame,60));
    for (unsigned size = 0; size < 34; size++) assert(fw_bpf_xdp(&fw,frame,size));
    fw_reset(&fw); fw.enabled = true;
    for (unsigned i = 0; i < FW_RULES; i++) {
        FirewallRule rule = {.allow = true, .direction = FW_IN, .protocol = NET_UDP,
            .source = p.source, .source_mask = 0xffffffffu,
            .destination = i == FW_RULES - 1 ? p.destination : p.destination + i + 1,
            .destination_mask = 0xffffffffu};
        assert(fw_add(&fw,&rule));
    }
    assert(fw_accept(&fw,&p,FW_IN) && fw.rules[FW_RULES-1].hits == 1);
    assert(firewall_bpf_stats.faults == 0);
    puts("PASS actual compiled upstream ebpfw XDP, VLAN parsing, absolute source blocks and eBPF policy matching");
}
void test_bpf(void) { arithmetic(); validation_and_memory(); maps(); actual_ebpfw(); }
