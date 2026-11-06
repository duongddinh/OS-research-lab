#ifndef JORDY_TCP_API_H
#define JORDY_TCP_API_H
#include "packet.h"
void tcp_net_input(const uint8_t *frame, const Packet *packet);
void tcp_net_arp(const uint8_t *frame, size_t size);
void tcp_net_poll(void);
void tcp_net_down(void);
void tcp_command(char *arguments);
void tcp_status(void);
#endif
