#ifndef JORDY_FW_BPF_H
#define JORDY_FW_BPF_H
#include "firewall.h"
typedef struct { uint32_t policy_runs, xdp_runs, xdp_drops, faults, steps; } FirewallBpfStats;
extern FirewallBpfStats firewall_bpf_stats;
bool fw_bpf_ready(void);
bool fw_bpf_policy(Firewall *fw, const Packet *packet, unsigned direction);
bool fw_bpf_xdp(Firewall *fw, const uint8_t *frame, size_t size);
unsigned fw_bpf_policy_size(void);
unsigned fw_bpf_xdp_size(void);
const char *fw_bpf_hash(void);
#endif
