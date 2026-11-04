#include "packet.h"

uint16_t net_read16(const uint8_t *p) { return (uint16_t)p[0] << 8 | p[1]; }
uint32_t net_read32(const uint8_t *p) {
    return (uint32_t)net_read16(p) << 16 | net_read16(p + 2);
}
void net_write16(uint8_t *p, uint16_t n) { p[0] = n >> 8; p[1] = n; }
void net_write32(uint8_t *p, uint32_t n) {
    net_write16(p, n >> 16); net_write16(p + 2, n);
}
uint16_t net_checksum(const uint8_t *p, size_t length, uint32_t sum) {
    while (length >= 2) { sum += net_read16(p); p += 2; length -= 2; }
    if (length) sum += (uint16_t)*p << 8;
    while (sum >> 16) sum = (sum & 65535) + (sum >> 16);
    return (uint16_t)~sum;
}
bool net_decode(const uint8_t *ip, size_t available, Packet *p) {
    /* No options, reassembly, IPv6, or unknown transports in this milestone.
       Validation precedes policy even while filtering is disabled. */
    if (available < 20 || available > 1500 || ip[0] != 0x45 || !ip[8] ||
        (net_read16(ip + 6) & 0xbfff) || net_checksum(ip, 20, 0)) return false;
    uint16_t length = net_read16(ip + 2);
    if (length < 20 || length > available) return false;
    const uint8_t *body = ip + 20;
    unsigned size = length - 20;
    uint8_t protocol = ip[9];
    uint16_t source_port = 0, destination_port = 0;
    uint32_t pseudo = net_read16(ip + 12) + net_read16(ip + 14) +
                      net_read16(ip + 16) + net_read16(ip + 18) + protocol + size;
    if (protocol == NET_ICMP) {
        if (size < 8 || net_checksum(body, size, 0)) return false;
    } else if (protocol == NET_UDP) {
        if (size < 8 || net_read16(body + 4) != size ||
            (net_read16(body + 6) && net_checksum(body, size, pseudo))) return false;
        source_port = net_read16(body); destination_port = net_read16(body + 2);
    } else if (protocol == NET_TCP) {
        if (size < 20 || (body[12] >> 4) < 5 ||
            (unsigned)(body[12] >> 4) * 4 > size || net_checksum(body, size, pseudo)) return false;
        source_port = net_read16(body); destination_port = net_read16(body + 2);
    } else return false;
    *p = (Packet){.source = net_read32(ip + 12), .destination = net_read32(ip + 16),
        .source_port = source_port, .destination_port = destination_port,
        .length = length, .protocol = protocol};
    return true;
}
bool net_number(const char *text, uint32_t maximum, uint32_t *value) {
    uint32_t n = 0;
    if (!*text) return false;
    for (; *text; text++) {
        if (*text < '0' || *text > '9') return false;
        unsigned digit = *text - '0';
        if (digit > maximum || n > (maximum - digit) / 10) return false;
        n = n * 10 + digit;
    }
    *value = n; return true;
}
bool net_cidr(const char *text, uint32_t *address, uint32_t *mask) {
    uint32_t ip = 0, prefix = 32;
    for (unsigned octet = 0; octet < 4; octet++) {
        char part[4]; unsigned n = 0; uint32_t value;
        while (*text >= '0' && *text <= '9') {
            if (n == 3) return false;
            part[n++] = *text++;
        }
        part[n] = 0;
        if (!net_number(part, 255, &value)) return false;
        ip = ip << 8 | value;
        if (octet < 3 && *text++ != '.') return false;
    }
    if (*text == '/') { if (!net_number(text + 1, 32, &prefix)) return false; }
    else if (*text) return false;
    *mask = prefix ? 0xffffffffu << (32 - prefix) : 0;
    *address = ip & *mask;
    return true;
}
