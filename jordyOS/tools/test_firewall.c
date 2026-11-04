/* Host tests exercise the exact freestanding decoder and policy engine. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "net/packet.h"
#include "net/firewall.h"

static uint8_t ip[1500];
static void ip_checksum(void) {
    net_write16(ip + 10, 0); net_write16(ip + 10, net_checksum(ip, 20, 0));
}
static void transport_checksum(void) {
    unsigned length = net_read16(ip + 2) - 20;
    unsigned offset = ip[9] == NET_TCP ? 16 : 6;
    net_write16(ip + 20 + offset, 0);
    uint32_t pseudo = net_read16(ip + 12) + net_read16(ip + 14) +
        net_read16(ip + 16) + net_read16(ip + 18) + ip[9] + length;
    uint16_t sum = net_checksum(ip + 20, length, pseudo);
    net_write16(ip + 20 + offset, sum ? sum : 65535);
}
static void packet(unsigned protocol, unsigned payload) {
    memset(ip, 0, sizeof ip);
    ip[0] = 0x45; ip[8] = 64; ip[9] = protocol;
    net_write32(ip + 12, 0xc0000201); net_write32(ip + 16, NET_IP);
    unsigned body = protocol == NET_TCP ? 20 : 8;
    net_write16(ip + 2, 20 + body + payload);
    if (protocol == NET_ICMP) {
        ip[20] = 8; net_write16(ip + 22, net_checksum(ip + 20, body + payload, 0));
    } else {
        net_write16(ip + 20, 40000); net_write16(ip + 22, 7777);
        if (protocol == NET_TCP) ip[32] = 0x50;
        else net_write16(ip + 24, body + payload);
        transport_checksum();
    }
    ip_checksum();
}
static void decoding(void) {
    Packet p;
    packet(NET_UDP, 3);
    assert(net_decode(ip, 31, &p));
    assert(p.source == 0xc0000201 && p.destination == NET_IP && p.source_port == 40000 &&
           p.destination_port == 7777 && p.protocol == NET_UDP && p.length == 31);
    for (unsigned n = 0; n < 31; n++) assert(!net_decode(ip, n, &p));
    assert(net_decode(ip, 46, &p)); /* Ethernet padding is outside IP. */
    assert(!net_decode(ip, 1501, &p));
    ip[30] ^= 1; assert(!net_decode(ip, 31, &p));
    net_write16(ip + 26, 0); assert(net_decode(ip, 31, &p)); /* Optional IPv4 UDP checksum. */
    net_write16(ip + 24, 10); assert(!net_decode(ip, 31, &p));
    packet(NET_UDP, 0); net_write16(ip + 2, 19); ip_checksum(); assert(!net_decode(ip, 28, &p));
    packet(NET_UDP, 0); ip[0] = 0x46; ip_checksum(); assert(!net_decode(ip, 28, &p));
    packet(NET_UDP, 0); ip[8] = 0; ip_checksum(); assert(!net_decode(ip, 28, &p));
    const uint16_t bad_flags[] = {0x8000, 0x2000, 1, 0x3fff};
    for (unsigned i = 0; i < sizeof bad_flags / sizeof *bad_flags; i++) {
        packet(NET_UDP, 0); net_write16(ip + 6, bad_flags[i]); ip_checksum();
        assert(!net_decode(ip, 28, &p));
    }
    packet(NET_UDP, 0); net_write16(ip + 6, 0x4000); ip_checksum(); assert(net_decode(ip, 28, &p));
    ip[1] ^= 1; assert(!net_decode(ip, 28, &p));
    packet(NET_TCP, 1); assert(net_decode(ip, 41, &p));
    ip[32] = 0x40; transport_checksum(); assert(!net_decode(ip, 41, &p));
    ip[32] = 0xf0; transport_checksum(); assert(!net_decode(ip, 41, &p));
    packet(NET_TCP, 1); ip[40] ^= 1; assert(!net_decode(ip, 41, &p));
    packet(NET_ICMP, 1); assert(net_decode(ip, 29, &p));
    ip[28] ^= 1; assert(!net_decode(ip, 29, &p));
    packet(99, 0); assert(!net_decode(ip, 28, &p));
    packet(NET_UDP, 1472); assert(net_decode(ip, 1500, &p));
    /* Reproducible malformed input across every possible Ethernet payload size.
       Run this under ASan/UBSan as well as on the normal host compiler. */
    uint32_t state = 42;
    for (unsigned n = 0; n < sizeof ip; n++) {
        for (unsigned i = 0; i < n; i++) { state = state * 1664525u + 1013904223u; ip[i] = state >> 24; }
        net_decode(ip, n, &p);
    }
    puts("PASS IPv4/UDP/TCP/ICMP decoding, checksums, lengths, fragments, malformed input");
}
static void parsing(void) {
    uint32_t address, mask, n;
    assert(net_number("4294967295", 0xffffffffu, &n) && n == 0xffffffffu);
    const char *bad_numbers[] = {"", "-1", "+1", "1x", "4294967296", "99999999999999999999"};
    for (unsigned i = 0; i < sizeof bad_numbers / sizeof *bad_numbers; i++)
        assert(!net_number(bad_numbers[i], 0xffffffffu, &n));
    assert(net_cidr("192.0.2.99/24", &address, &mask) && address == 0xc0000200 && mask == NET_MASK);
    assert(net_cidr("192.0.2.1", &address, &mask) && address == 0xc0000201 && mask == 0xffffffffu);
    assert(net_cidr("0.0.0.0/0", &address, &mask) && !address && !mask);
    const char *bad_cidrs[] = {"", "1", "1.2.3", "256.0.0.1", "1.2.3.4.5", "1.2.3.4/33",
        "1.2.3.4/", "1.2.3.4/-1", "1.2.3.4/0x", "1.2.3.4x", "1..2.3", "0000.2.3.4"};
    for (unsigned i = 0; i < sizeof bad_cidrs / sizeof *bad_cidrs; i++)
        assert(!net_cidr(bad_cidrs[i], &address, &mask));
    puts("PASS bounded numeric and CIDR parsing");
}
static void policy(void) {
    Firewall fw; fw_reset(&fw);
    Packet p = {.source = 0xc0000201, .destination = NET_IP, .destination_port = 7777, .protocol = NET_UDP};
    assert(!fw.enabled && !fw.count && !fw.defaults[FW_IN] && fw.defaults[FW_OUT]);
    assert(fw_accept(&fw, &p, FW_IN) && fw.allowed[FW_IN] == 1);
    fw.enabled = true;
    assert(!fw_accept(&fw, &p, FW_IN) && fw.denied[FW_IN] == 1);
    assert(fw_accept(&fw, &p, FW_OUT));
    FirewallRule rule = {.allow = true, .direction = FW_IN, .protocol = NET_UDP, .port = 7777,
        .source = 0xc0000201, .source_mask = 0xffffffffu, .destination = NET_IP, .destination_mask = 0xffffffffu};
    assert(fw_add(&fw, &rule));
    assert(fw_accept(&fw, &p, FW_IN) && fw.rules[0].hits == 1);
    p.source++; assert(!fw_accept(&fw, &p, FW_IN)); p.source--;
    p.destination++; assert(!fw_accept(&fw, &p, FW_IN)); p.destination--;
    p.destination_port++; assert(!fw_accept(&fw, &p, FW_IN)); p.destination_port--;
    p.protocol = NET_TCP; assert(!fw_accept(&fw, &p, FW_IN)); p.protocol = NET_UDP;
    rule = (FirewallRule){.direction = FW_IN}; assert(fw_add(&fw, &rule));
    assert(fw_accept(&fw, &p, FW_IN)); /* Earlier allow wins over later deny. */
    assert(!fw_delete(&fw, 0) && !fw_delete(&fw, 3));
    assert(fw_delete(&fw, 1) && !fw_accept(&fw, &p, FW_IN) && fw.rules[0].hits == 1);
    assert(fw_delete(&fw, 1) && !fw.count);
    rule = (FirewallRule){.direction = FW_OUT, .protocol = NET_UDP, .port = 7777};
    assert(fw_add(&fw, &rule) && !fw_accept(&fw, &p, FW_OUT));
    assert(!fw_accept(&fw, &p, 2));
    while (fw.count < FW_RULES) assert(fw_add(&fw, &rule));
    assert(!fw_add(&fw, &rule) && fw.count == FW_RULES);
    fw_reset(&fw); assert(!fw.count && !fw.enabled && !fw.allowed[0] && !fw.denied[0]);
    rule.protocol = 99; assert(!fw_add(&fw, &rule));
    rule.protocol = NET_ICMP; assert(!fw_add(&fw, &rule)); /* No ports on ICMP. */
    puts("PASS first-match policy, CIDR/port/direction matching, counters, deletion, capacity, reset");
}
void test_bpf(void);
int main(void) { decoding(); parsing(); policy(); test_bpf(); return 0; }
