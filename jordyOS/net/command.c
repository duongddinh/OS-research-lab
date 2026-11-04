#include "network.h"
#include "firewall.h"
#include "../util.h"

void net_print_number(uint32_t n) {
    char buffer[11]; unsigned i = sizeof buffer - 1; buffer[i] = 0;
    do { buffer[--i] = '0' + n % 10; n /= 10; } while (n);
    sys_write(buffer + i);
}
void net_print_ip(uint32_t ip) {
    for (unsigned i = 0; i < 4; i++) {
        if (i) sys_write(".");
        net_print_number((ip >> (24 - i * 8)) & 255);
    }
}
static void cidr(uint32_t ip, uint32_t mask) {
    if (!mask) { sys_write("any"); return; }
    net_print_ip(ip);
    unsigned prefix = 0;
    for (; mask; mask <<= 1) prefix++;
    sys_write("/"); net_print_number(prefix);
}
static int words(char *text, char **out, unsigned maximum) {
    unsigned count = 0;
    while (*text) {
        while (*text == ' ' || *text == '\t') text++;
        if (!*text) break;
        if (count == maximum) return -1;
        out[count++] = text;
        while (*text && *text != ' ' && *text != '\t') text++;
        if (*text) *text++ = 0;
    }
    return (int)count;
}
static bool action(const char *text, bool *allow) {
    if (!strcmp(text, "allow")) { *allow = true; return true; }
    if (!strcmp(text, "deny")) { *allow = false; return true; }
    return false;
}
static const char *protocol(unsigned p) {
    return p == NET_ICMP ? "icmp" : p == NET_TCP ? "tcp" : p == NET_UDP ? "udp" : "all";
}
static bool service(char *text, FirewallRule *rule) {
    if (!strcmp(text, "all")) return true;
    if (!strcmp(text, "icmp")) { rule->protocol = NET_ICMP; return true; }
    if (!strcmp(text, "tcp")) { rule->protocol = NET_TCP; return true; }
    if (!strcmp(text, "udp")) { rule->protocol = NET_UDP; return true; }
    char *slash = text;
    while (*slash && *slash != '/') slash++;
    if (!*slash) return false;
    *slash++ = 0;
    uint32_t port;
    if (!net_number(text, 65535, &port) || !port) return false;
    if (!strcmp(slash, "tcp")) rule->protocol = NET_TCP;
    else if (!strcmp(slash, "udp")) rule->protocol = NET_UDP;
    else return false;
    rule->port = (uint16_t)port; return true;
}
static void fw_status(void) {
    Firewall *fw = &native_firewall;
    sys_write("Native JordyOS firewall: "); sys_write(fw->enabled ? "enabled\n" : "disabled\n");
    sys_write("Stateless IPv4; C backend; no Linux eBPF bytecode support.\n");
    sys_write("Defaults: incoming "); sys_write(fw->defaults[FW_IN] ? "allow" : "deny");
    sys_write(", outgoing "); sys_write(fw->defaults[FW_OUT] ? "allow\n" : "deny\n");
    sys_write("Rules: "); net_print_number(fw->count); sys_write("/32 (first match wins)\n");
    for (unsigned i = 0; i < fw->count; i++) {
        FirewallRule *r = &fw->rules[i];
        net_print_number(i + 1); sys_write(r->allow ? " allow " : " deny ");
        sys_write(r->direction == FW_IN ? "in " : "out ");
        if (r->port) { net_print_number(r->port); sys_write("/"); }
        sys_write(protocol(r->protocol)); sys_write(" from "); cidr(r->source, r->source_mask);
        sys_write(" to "); cidr(r->destination, r->destination_mask);
        sys_write(" hits="); net_print_number(r->hits); sys_write("\n");
    }
}
static void help(void) {
    sys_write("ebpfw: native stateless IPv4 firewall (no Linux BPF runtime)\n"
        "  ebpfw enable | disable | status [numbered] | stats | doctor\n"
        "  ebpfw allow|deny [in|out] SERVICE [from CIDR] [to CIDR]\n"
        "  SERVICE: all, icmp, tcp, udp, PORT/tcp, PORT/udp\n"
        "  ebpfw default allow|deny incoming|outgoing\n"
        "  ebpfw delete NUMBER | reset\n"
        "Omitted direction means in. Ports match destination, including out.\n"
        "ARP bypasses IP policy; IPv6/options/fragments are unsupported.\n"
        "Rules and counters are RAM-only. No connection tracking or forwarding.\n");
}
void fw_command(char *arguments) {
    char *w[10]; int n = words(arguments, w, 10);
    Firewall *fw = &native_firewall;
    if (!n || (n == 1 && !strcmp(w[0], "help"))) { help(); return; }
    if (n == 1 && !strcmp(w[0], "enable")) {
        fw->enabled = true; sys_write("Native firewall enabled.\n"); return;
    }
    if (n == 1 && !strcmp(w[0], "disable")) {
        fw->enabled = false; sys_write("Native firewall disabled; packet validation remains active.\n"); return;
    }
    if ((n == 1 || (n == 2 && !strcmp(w[1], "numbered"))) && !strcmp(w[0], "status")) {
        fw_status(); return;
    }
    if (n == 1 && !strcmp(w[0], "stats")) {
        for (unsigned d = 0; d < 2; d++) {
            sys_write(d == FW_IN ? "in allowed=" : "out allowed="); net_print_number(fw->allowed[d]);
            sys_write(" denied="); net_print_number(fw->denied[d]); sys_write("\n");
        }
        return;
    }
    if (n == 1 && !strcmp(w[0], "doctor")) {
        sys_write("Backend: native C, stateless IPv4. No Linux BPF facilities.\n");
        sys_write(native_network.up ? "RTL8139: up, receive/transmit hooks active.\n" :
            "RTL8139: down; attach -device rtl8139 and run net up.\n");
        sys_write("Validation: IPv4/transport lengths and checksums; fragments/options rejected.\n"
            "Services: ICMP echo; optional UDP echo at 7777. TCP classification only.\n"
            "Polling requires console activity, yields, or guest wait calls.\n");
        return;
    }
    if (n == 1 && !strcmp(w[0], "reset")) {
        fw_reset(fw); sys_write("Rules/counters reset; firewall disabled.\n"); return;
    }
    uint32_t number;
    if (n == 2 && !strcmp(w[0], "delete") && net_number(w[1], FW_RULES, &number) && fw_delete(fw, number)) {
        sys_write("Rule deleted.\n"); return;
    }
    bool allow;
    if (n == 3 && !strcmp(w[0], "default") && action(w[1], &allow)) {
        unsigned d;
        if (!strcmp(w[2], "incoming")) d = FW_IN;
        else if (!strcmp(w[2], "outgoing")) d = FW_OUT;
        else goto invalid;
        fw->defaults[d] = allow; sys_write("Default updated.\n"); return;
    }
    if (n >= 2 && action(w[0], &allow)) {
        FirewallRule rule = {.allow = allow, .direction = FW_IN};
        unsigned i = 1;
        if (!strcmp(w[i], "in")) i++;
        else if (!strcmp(w[i], "out")) { rule.direction = FW_OUT; i++; }
        if (i >= (unsigned)n || !service(w[i++], &rule)) goto invalid;
        bool from = false, to = false;
        while (i < (unsigned)n) {
            if (i + 1 >= (unsigned)n) goto invalid;
            if (!strcmp(w[i], "from") && !from) {
                from = true;
                if (strcmp(w[i + 1], "any") && !net_cidr(w[i + 1], &rule.source, &rule.source_mask)) goto invalid;
            } else if (!strcmp(w[i], "to") && !to) {
                to = true;
                if (strcmp(w[i + 1], "any") && !net_cidr(w[i + 1], &rule.destination, &rule.destination_mask)) goto invalid;
            } else goto invalid;
            i += 2;
        }
        if (!fw_add(fw, &rule)) { sys_write("Rule table full (32).\n"); return; }
        sys_write("Rule added: "); net_print_number(fw->count); sys_write("\n"); return;
    }
invalid:
    sys_write("Invalid command; policy unchanged. Type ebpfw help.\n");
}
void net_command(char *arguments) {
    char *w[3]; int n = words(arguments, w, 3);
    if (!n || (n == 1 && !strcmp(w[0], "status"))) {
        sys_write(native_network.up ? "Network: up, " : "Network: down, ");
        net_print_ip(NET_IP); sys_write("/24, RTL8139, polled\n");
        sys_write(native_network.echo ? "UDP echo 7777: on\n" : "UDP echo 7777: off\n");
        sys_write("rx="); net_print_number(native_network.received);
        sys_write(" tx="); net_print_number(native_network.transmitted);
        sys_write(" invalid="); net_print_number(native_network.invalid);
        sys_write(" ignored="); net_print_number(native_network.ignored);
        sys_write(" tx_dropped="); net_print_number(native_network.tx_dropped);
        sys_write(" driver_errors="); net_print_number(native_network.driver_errors); sys_write("\n");
    } else if (n == 1 && !strcmp(w[0], "up")) {
        sys_write(rtl_up() ? "RTL8139 up at 192.0.2.2/24.\n" : "No usable RTL8139 on PCI bus 0.\n");
    } else if (n == 1 && !strcmp(w[0], "down")) {
        rtl_down(); sys_write("Network down.\n");
    } else if (n == 2 && !strcmp(w[0], "echo") && (!strcmp(w[1], "on") || !strcmp(w[1], "off"))) {
        native_network.echo = !strcmp(w[1], "on");
        sys_write(native_network.echo ? "UDP echo 7777 enabled.\n" : "UDP echo 7777 disabled.\n");
    } else sys_write("net status | up | down | echo on|off\n");
}
