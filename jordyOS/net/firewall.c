#include "firewall.h"

void fw_reset(Firewall *fw) { *fw = (Firewall){.defaults = {false, true}}; }
bool fw_add(Firewall *fw, const FirewallRule *rule) {
    if (fw->count == FW_RULES || rule->direction > FW_OUT ||
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
    bool allow = true;
    if (fw->enabled) {
        allow = fw->defaults[direction];
        for (unsigned i = 0; i < fw->count; i++) {
            FirewallRule *r = &fw->rules[i];
            if (r->direction != direction || (r->protocol && r->protocol != p->protocol) ||
                (r->port && r->port != p->destination_port) ||
                (p->source & r->source_mask) != r->source ||
                (p->destination & r->destination_mask) != r->destination) continue;
            r->hits++; allow = r->allow; break;
        }
    }
    if (allow) fw->allowed[direction]++; else fw->denied[direction]++;
    return allow;
}
