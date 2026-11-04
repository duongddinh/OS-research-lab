/* SPDX-License-Identifier: GPL-2.0-only
 * JordyOS map/packet adapter for the upstream ebpfw structures and programs. */
#include "fw_bpf.h"
#include "bpf_vm.h"
#include "bpf/ebpfw_shared.h"
#include "bpf/programs.h"
#include "../util.h"

enum { POLICY, ACTIVE, STATS, EARLY4, EARLY6, ENABLED, METADATA };
static union { struct rule rule; struct header header; } policy[FW_RULES + 1];
static struct counter counters[FW_RULES + 16];
static struct lpm_key prefixes[FW_RULES];
static uint32_t indexes[FW_RULES], enabled, inner_map;
static struct packet metadata;
static BpfEnvironment environment;
static int checked;
FirewallBpfStats firewall_bpf_stats;
typedef char RuleAbiMustBe96[(sizeof(struct rule) == 96) ? 1 : -1];
unsigned fw_bpf_policy_size(void) { return sizeof ebpfw_policy_code / sizeof *ebpfw_policy_code; }
unsigned fw_bpf_xdp_size(void) { return sizeof ebpfw_xdp_code / sizeof *ebpfw_xdp_code; }
const char *fw_bpf_hash(void) { return BPF_OBJECT_SHA256; }
bool fw_bpf_ready(void) {
    if (!checked) checked = bpf_validate(ebpfw_policy_code, fw_bpf_policy_size()) &&
        bpf_validate(ebpfw_xdp_code, fw_bpf_xdp_size()) ? 1 : -1;
    return checked == 1;
}
static unsigned bits(uint32_t mask) {
    unsigned n = 0;
    while (mask) { n++; mask <<= 1; }
    return n;
}
static void prepare(Firewall *fw) {
    memset(policy, 0, sizeof policy); memset(counters, 0, sizeof counters);
    memset(&metadata, 0, sizeof metadata);
    enabled = fw->enabled; inner_map = POLICY;
    struct header *h = &policy[0].header;
    h->count = fw->count; h->ready = 1; h->schema = 2; h->generation = 1;
    h->defaults[0] = fw->defaults[0] ? 1 : 2; h->defaults[1] = fw->defaults[1] ? 1 : 2;
    unsigned entry = 1, blocked = 0;
    /* Upstream absolute blocks precede normal first-match rules. */
    for (unsigned pass = 0; pass < 2; pass++) {
        for (unsigned i = 0; i < fw->count && i < FW_RULES; i++) {
            FirewallRule *native = &fw->rules[i];
            if (native->block != (pass == 0)) continue;
            struct rule *r = &policy[entry++].rule;
            r->action = native->block ? 5 : native->allow ? 1 : 2;
            r->direction = native->block ? 0 : native->direction + 1;
            r->family = 4; r->proto = native->protocol;
            r->src_bits = bits(native->source_mask); r->dst_bits = bits(native->destination_mask);
            net_write32(r->src, native->source); net_write32(r->dst, native->destination);
            r->dport_lo = r->dport_hi = native->port; r->index = i;
            if (native->block) {
                prefixes[blocked].prefix_len = r->src_bits;
                memcpy(prefixes[blocked].address, r->src, 16);
                indexes[blocked++] = i;
            }
        }
    }
    h->blocks = blocked;
    environment = (BpfEnvironment){.maps = {
        [POLICY] = {BPF_ARRAY,4,96,FW_RULES+1,0,(uint8_t *)policy,NULL,false},
        [ACTIVE] = {BPF_MAP_ARRAY,4,4,1,0,(uint8_t *)&inner_map,NULL,false},
        [STATS] = {BPF_PERCPU_ARRAY,4,sizeof(struct counter),FW_RULES+16,0,(uint8_t *)counters,NULL,true},
        [EARLY4] = {BPF_LPM,20,4,FW_RULES,blocked,(uint8_t *)indexes,(uint8_t *)prefixes,false},
        [EARLY6] = {BPF_LPM,20,4,FW_RULES,0,(uint8_t *)indexes,(uint8_t *)prefixes,false},
        [ENABLED] = {BPF_ARRAY,4,4,1,0,(uint8_t *)&enabled,NULL,false},
        [METADATA] = {BPF_ARRAY,4,sizeof metadata,1,0,(uint8_t *)&metadata,NULL,false},
    }};
}
static bool finish(Firewall *fw, BpfResult result) {
    firewall_bpf_stats.steps += result.steps;
    if (!result.ok || (result.value != 1 && result.value != 2)) {
        firewall_bpf_stats.faults++; return false;
    }
    for (unsigned i = 0; i < fw->count && i < FW_RULES; i++) fw->rules[i].hits += (uint32_t)counters[i].packets;
    return result.value == 2;
}
bool fw_bpf_policy(Firewall *fw, const Packet *p, unsigned direction) {
    if (!fw_bpf_ready() || fw->count > FW_RULES) { firewall_bpf_stats.faults++; return false; }
    prepare(fw);
    metadata.direction = direction + 1; metadata.family = 4; metadata.proto = p->protocol;
    metadata.sport = p->source_port; metadata.dport = p->destination_port; metadata.len = p->length;
    net_write32(metadata.src, p->source); net_write32(metadata.dst, p->destination);
    firewall_bpf_stats.policy_runs++;
    return finish(fw, bpf_execute(ebpfw_policy_code, fw_bpf_policy_size(), &environment));
}
bool fw_bpf_xdp(Firewall *fw, const uint8_t *frame, size_t size) {
    if (!fw_bpf_ready() || size > NET_FRAME_MAX || fw->count > FW_RULES) { firewall_bpf_stats.faults++; return false; }
    prepare(fw);
    uint32_t ctx[6] = {BPF_DATA, BPF_DATA + (uint32_t)size, BPF_DATA, 1, 0, 0};
    environment.context = (const uint8_t *)ctx; environment.context_size = sizeof ctx;
    environment.packet = frame; environment.packet_size = (uint32_t)size;
    firewall_bpf_stats.xdp_runs++;
    bool pass = finish(fw, bpf_execute(ebpfw_xdp_code, fw_bpf_xdp_size(), &environment));
    if (!pass) { firewall_bpf_stats.xdp_drops++; fw->denied[FW_IN]++; }
    return pass;
}
