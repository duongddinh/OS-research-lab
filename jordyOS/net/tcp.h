#ifndef JORDY_TCP_H
#define JORDY_TCP_H
#include "packet.h"
#include <larp_core.h>

#define TCP_BUFFER 16384u
#define TCP_SLOTS 16u
#define TCP_MSS 536u
enum { TCP_FIN=1, TCP_SYN=2, TCP_RST=4, TCP_PSH=8, TCP_ACK=16 };
enum { TCP_IDLE, TCP_SYN_SENT, TCP_ESTABLISHED, TCP_CLOSING, TCP_DONE, TCP_FAILED };
typedef struct {
    uint32_t sequence, offset, length, delivered;
    uint64_t sent_us, delivered_us, flight_us;
    uint8_t flags, retries;
    bool used, app_limited;
} TcpSegment;
typedef struct {
    unsigned state;
    const char *error;
    uint16_t local_port, peer_port, mss, peer_window;
    uint32_t isn, una, next, receive_next, window_seq, window_ack;
    uint32_t tx_size, tx_offset, rx_size, delivered, cwnd, recovery_end;
    uint32_t srtt_us, rttvar_us, rto_us, dupacks;
    uint32_t retransmits, timeouts, fast_retransmits, feedbacks, invalid_acks;
    uint32_t paced_packets, minimum_gap_us;
    uint64_t started_us, next_send_us, timer_us, delivered_us, flight_us, probe_us;
    uint64_t last_data_us;
    bool own_fin, own_fin_acked, peer_fin, ack_pending, recovery;
    struct larp_model larp;
    TcpSegment pending[TCP_SLOTS];
    uint8_t tx[TCP_BUFFER], rx[TCP_BUFFER];
    bool (*emit)(void *, const uint8_t *, size_t);
    void *context;
} Tcp;

void tcp_start(Tcp *c, uint16_t local, uint16_t peer, uint32_t isn,
    const uint8_t *data, size_t size, uint64_t now,
    bool (*emit)(void *, const uint8_t *, size_t), void *context);
void tcp_input(Tcp *c, const uint8_t *segment, size_t size, uint64_t now);
void tcp_tick(Tcp *c, uint64_t now);
void tcp_abort(Tcp *c, const char *reason);

#endif
