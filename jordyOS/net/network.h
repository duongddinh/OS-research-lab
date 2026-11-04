#ifndef JORDY_NETWORK_H
#define JORDY_NETWORK_H
#include "packet.h"
typedef struct {
    bool up, echo;
    uint32_t received, transmitted, invalid, ignored, tx_dropped, driver_errors;
} Network;
extern Network native_network;
extern uint8_t net_mac[6];
bool rtl_up(void);
void rtl_down(void);
void rtl_poll(void);
bool rtl_send(const uint8_t *frame, size_t size);
void net_init(void);
void net_receive(uint8_t *frame, size_t size);
void net_command(char *arguments);
void net_print_number(uint32_t value);
void net_print_ip(uint32_t ip);
#endif
