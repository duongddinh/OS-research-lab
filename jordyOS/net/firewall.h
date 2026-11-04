#ifndef JORDY_FIREWALL_H
#define JORDY_FIREWALL_H
#include "packet.h"
#define FW_RULES 32
enum { FW_IN, FW_OUT };
typedef struct {
    uint32_t source, source_mask, destination, destination_mask, hits;
    uint16_t port; /* destination port; zero means any */
    uint8_t protocol, direction;
    bool allow;
} FirewallRule;
typedef struct {
    FirewallRule rules[FW_RULES];
    unsigned count;
    bool enabled, defaults[2];
    uint32_t allowed[2], denied[2];
} Firewall;
void fw_reset(Firewall *fw);
bool fw_add(Firewall *fw, const FirewallRule *rule);
bool fw_delete(Firewall *fw, unsigned number);
bool fw_accept(Firewall *fw, const Packet *packet, unsigned direction);
void fw_command(char *arguments);
extern Firewall native_firewall;
#endif
