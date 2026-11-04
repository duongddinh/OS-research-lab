#ifndef JORDY_PACKET_H
#define JORDY_PACKET_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NET_FRAME_MAX 1514
#define NET_IP 0xc0000202u /* 192.0.2.2; isolated documentation subnet */
#define NET_MASK 0xffffff00u
enum { NET_ICMP = 1, NET_TCP = 6, NET_UDP = 17 };
typedef struct {
    uint32_t source, destination;
    uint16_t source_port, destination_port, length;
    uint8_t protocol;
} Packet;

uint16_t net_read16(const uint8_t *p);
uint32_t net_read32(const uint8_t *p);
void net_write16(uint8_t *p, uint16_t n);
void net_write32(uint8_t *p, uint32_t n);
uint16_t net_checksum(const uint8_t *p, size_t length, uint32_t sum);
bool net_decode(const uint8_t *ip, size_t available, Packet *packet);
bool net_number(const char *text, uint32_t maximum, uint32_t *value);
bool net_cidr(const char *text, uint32_t *address, uint32_t *mask);
#endif
