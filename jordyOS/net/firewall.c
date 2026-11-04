#include "firewall.h"
#include "fw_bpf.h"

void fw_reset(Firewall *fw) { *fw = (Firewall){.defaults = {false, true}}; }
bool fw_add(Firewall *fw, const FirewallRule *rule) {
    if (fw->count >= FW_RULES || rule->direction > FW_OUT ||
        (rule->protocol != 0 && rule->protocol != NET_ICMP &&
         rule->protocol != NET_TCP && rule->protocol != NET_UDP) ||
        (rule->port && rule->protocol != NET_TCP && rule->protocol != NET_UDP)) return false;
    fw->rules[fw->count] = *rule;
    fw->rules[fw->count++].hits = 0;
    return true;
}
bool fw_delete(Firewall *fw, unsigned number) {
    if (!number || number > fw->count) return false;
    for (unsigned i = number; i < fw->count; i++) fw->rules[i - 1] = fw->rules[i];
    fw->count--; return true;
}
bool fw_accept(Firewall *fw, const Packet *p, unsigned direction) {
    if (direction > FW_OUT) return false;
    bool allow = fw_bpf_policy(fw, p, direction);
    if (allow) fw->allowed[direction]++; else fw->denied[direction]++;
    return allow;
}
