#include "network.h"
#include "firewall.h"
#include "fw_bpf.h"
#include "../util.h"

Network native_network;
Firewall native_firewall;
void net_init(void) { fw_reset(&native_firewall); }
static bool peer_ip(uint32_t ip) {
    return (ip & NET_MASK) == (NET_IP & NET_MASK) &&
           (ip & 255) != 0 && (ip & 255) != 255 && ip != NET_IP;
}
void net_receive(uint8_t *frame, size_t size) {
    native_network.received++;
    if (size < 14 || size > NET_FRAME_MAX) { native_network.invalid++; return; }
    /* Actual upstream ebpfw XDP bytecode runs before Ethernet/IP handling. */
    if (!fw_bpf_xdp(&native_firewall, frame, size)) return;
    static const uint8_t broadcast[6] = {255,255,255,255,255,255};
    bool local = !memcmp(frame, net_mac, 6);
    if ((!local && memcmp(frame, broadcast, 6)) || (frame[6] & 1)) {
        native_network.ignored++; return;
    }
    uint16_t type = net_read16(frame + 12);
    if (type == 0x0806) {
        /* Only answer Ethernet/IPv4 ARP requests for our fixed address.
           No neighbor cache, unsolicited replies, or proxy ARP. */
        if (size < 42 || net_read16(frame + 14) != 1 || net_read16(frame + 16) != 0x0800 ||
            frame[18] != 6 || frame[19] != 4 || net_read16(frame + 20) != 1 ||
            memcmp(frame + 6, frame + 22, 6) || !peer_ip(net_read32(frame + 28)) ||
            net_read32(frame + 38) != NET_IP) { native_network.ignored++; return; }
        uint8_t reply[42] = {0};
        memcpy(reply, frame + 6, 6); memcpy(reply + 6, net_mac, 6);
        net_write16(reply + 12, 0x0806); memcpy(reply + 14, frame + 14, 8);
        net_write16(reply + 20, 2); memcpy(reply + 22, net_mac, 6);
        net_write32(reply + 28, NET_IP); memcpy(reply + 32, frame + 22, 10);
        rtl_send(reply, sizeof reply); return;
    }
    if (type != 0x0800 || !local) { native_network.ignored++; return; }
    Packet packet;
    uint8_t *ip = frame + 14;
    if (!net_decode(ip, size - 14, &packet)) { native_network.invalid++; return; }
    if (packet.destination != NET_IP || !peer_ip(packet.source)) {
        native_network.ignored++; return;
    }
    if (!fw_accept(&native_firewall, &packet, FW_IN)) return;
    uint8_t *body = ip + 20;
    if (packet.protocol == NET_ICMP && body[0] == 8 && body[1] == 0) {
        body[0] = 0; net_write16(body + 2, 0);
        net_write16(body + 2, net_checksum(body, packet.length - 20, 0));
    } else if (packet.protocol == NET_UDP && packet.destination_port == 7777 && native_network.echo) {
        net_write16(body, packet.destination_port); net_write16(body + 2, packet.source_port);
        net_write16(body + 6, 0); /* IPv4 UDP permits omitted checksum. */
    } else return; /* TCP is classified only; no TCP listener/stack yet. */
    net_write32(ip + 12, NET_IP); net_write32(ip + 16, packet.source);
    ip[8] = 64; net_write16(ip + 10, 0); net_write16(ip + 10, net_checksum(ip, 20, 0));
    Packet outgoing;
    if (!net_decode(ip, packet.length, &outgoing) || !fw_accept(&native_firewall, &outgoing, FW_OUT)) return;
    memcpy(frame, frame + 6, 6); memcpy(frame + 6, net_mac, 6);
    rtl_send(frame, 14 + packet.length);
}
