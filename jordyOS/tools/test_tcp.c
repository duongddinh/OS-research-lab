#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "net/tcp.h"

static Tcp c;
static uint8_t bytes[TCP_BUFFER],last[600];
static unsigned sent,last_size;
static bool available=true;
static bool emit(void *context,const uint8_t *b,size_t size) {
    (void)context;
    if (!available) return false;
    assert(size<=sizeof last); memcpy(last,b,size); last_size=size; sent++; return true;
}
static void input(uint32_t seq,uint32_t ack,uint8_t flags,uint16_t window,
                  const void *data,unsigned size,uint64_t now) {
    uint8_t b[20+TCP_MSS]={0}; assert(size<=TCP_MSS);
    net_write16(b,9000); net_write16(b+2,49152);
    net_write32(b+4,seq); net_write32(b+8,ack);
    b[12]=0x50; b[13]=flags; net_write16(b+14,window);
    if (size) memcpy(b+20,data,size);
    tcp_input(&c,b,20+size,now);
}
static void start(unsigned size,uint32_t isn) {
    available=true; sent=0;
    tcp_start(&c,49152,9000,isn,bytes,size,100,emit,0);
    tcp_tick(&c,100);
    assert(c.state==TCP_SYN_SENT && last[13]==TCP_SYN && last_size==24);
}
static void connect(unsigned size,uint32_t isn,uint16_t window) {
    start(size,isn);
    input(7000,isn+1,TCP_SYN|TCP_ACK,window,0,0,10100);
    assert(c.state==TCP_ESTABLISHED && c.receive_next==7001);
}
static void fill_window(uint64_t now) {
    for (unsigned i=0;i<TCP_SLOTS+1;i++) tcp_tick(&c,now+i*100);
}
int main(void) {
    for (unsigned i=0;i<TCP_BUFFER;i++) bytes[i]=(uint8_t)(i*37+(i>>8));
    start(1000,1000);
    input(7000,1002,TCP_SYN|TCP_ACK,65535,0,0,10000);
    assert(c.state==TCP_SYN_SENT);
    tcp_tick(&c,1000100);
    assert(c.timeouts==1 && c.retransmits==1 && c.rto_us==2000000);
    input(7000,1001,TCP_SYN|TCP_ACK,65535,0,0,1010100);
    assert(c.state==TCP_ESTABLISHED && c.srtt_us==0); /* Karn */
    puts("PASS handshake validation, SYN retry/backoff and Karn RTT sampling");

    connect(TCP_BUFFER,UINT32_MAX-100,65535);
    fill_window(10200);
    assert(c.tx_offset==TCP_SLOTS*TCP_MSS && c.next<c.isn);
    assert(c.paced_packets==TCP_SLOTS && c.minimum_gap_us>=5);
    uint32_t una=c.una,next=c.next;
    input(7001,next+100,TCP_ACK,0,0,0,20000);
    assert(c.una==una && c.peer_window==65535 && c.invalid_acks==1);
    input(7001,next,TCP_ACK,65535,0,0,30200);
    assert(c.delivered==TCP_SLOTS*TCP_MSS && c.feedbacks && c.larp.learned);
    fill_window(30300);
    input(7001,c.next,TCP_ACK,65535,0,0,50300);
    assert(c.delivered==TCP_BUFFER);
    tcp_tick(&c,50400); assert(c.own_fin && last[13]==(TCP_ACK|TCP_FIN));
    input(7001,c.next,TCP_ACK|TCP_FIN,65535,0,0,60000);
    tcp_tick(&c,60001); assert(c.state==TCP_DONE && c.peer_fin && c.own_fin_acked);
    puts("PASS sequence wrap, byte accounting, real LARP feedback, pacing and FIN close");

    connect(TCP_BUFFER,1000,65535); fill_window(10200);
    uint64_t previous=c.larp.rate;
    for (unsigned i=0;i<3;i++) input(7001,c.una,TCP_ACK,65535,0,0,20000+i*100);
    assert(c.fast_retransmits==1 && c.retransmits==1 && c.larp.rate<previous);
    input(7001,c.next,TCP_ACK,65535,0,0,30000);
    assert(!c.recovery && c.srtt_us==10000); /* no ambiguous retransmit RTT */
    puts("PASS duplicate-ACK fast retransmit, LARP loss response and recovery");

    connect(1000,1000,65535); fill_window(10200);
    tcp_tick(&c,1010200);
    assert(c.timeouts==1 && c.cwnd==1 && c.retransmits==1);
    input(7001,c.next,TCP_ACK,65535,0,0,1020200);
    assert(c.delivered==1000 && c.cwnd>=1);
    puts("PASS data timeout, congestion window reduction and recovery progress");

    connect(1000,1000,0); tcp_tick(&c,10100);
    assert(!c.tx_offset);
    tcp_tick(&c,1000200); assert(!c.tx_offset && last_size==21);
    input(7001,c.una,TCP_ACK,65535,0,0,1010000);
    tcp_tick(&c,1010001); assert(c.tx_offset==TCP_MSS && !c.fast_retransmits);
    available=false; unsigned offset=c.tx_offset;
    tcp_tick(&c,1010100); assert(c.tx_offset==offset);
    available=true;
    puts("PASS zero-window persist/reopen and NIC backpressure");

    connect(1000,1000,65535); fill_window(10200);
    input(7005,c.una,TCP_ACK,65535,"efgh",4,20000);
    assert(c.rx_size==0 && c.receive_next==7001);
    input(7001,c.una,TCP_ACK,65535,"abcd",4,20100);
    input(7001,c.una,TCP_ACK,65535,"abcdefgh",8,20200);
    assert(c.rx_size==8 && !memcmp(c.rx,"abcdefgh",8));
    input(7009,c.una,TCP_ACK|TCP_FIN,65535,0,0,20300);
    input(7009,c.una,TCP_ACK|TCP_FIN,65535,0,0,20400);
    assert(c.rx_size==8 && c.receive_next==7010);
    input(7011,c.una,TCP_RST,65535,0,0,20500);
    assert(c.state!=TCP_FAILED);
    input(7010,c.una,TCP_RST,65535,0,0,20600);
    assert(c.state==TCP_FAILED);
    puts("PASS out-of-order/overlap handling, duplicate FIN and reset sequence checks");

    connect(1000,1000,65535); fill_window(10200);
    input(7001,c.una+100,TCP_ACK,65535,0,0,20000);
    assert(c.delivered==100);
    tcp_tick(&c,1020000);
    assert(net_read32(last+4)==1101 && !memcmp(last+20,bytes+100,TCP_MSS-100));
    tcp_tick(&c,120000101); assert(c.state==TCP_FAILED);
    puts("PASS partial ACK retransmission offset and bounded connection deadline");

    connect(0,1000,65535); tcp_tick(&c,10200);
    assert(c.own_fin);
    for (unsigned i=0;i<TCP_BUFFER/TCP_MSS;i++)
        input(c.receive_next,c.next,TCP_ACK,65535,bytes+i*TCP_MSS,TCP_MSS,20000+i*100);
    unsigned tail=TCP_BUFFER-c.rx_size;
    input(c.receive_next,c.next,TCP_ACK,65535,bytes+c.rx_size,tail,30000);
    assert(c.rx_size==TCP_BUFFER);
    tcp_tick(&c,30001);
    assert(net_read16(last+14)==1); /* FIN must remain sendable by a real peer. */
    input(c.receive_next,c.next,TCP_ACK|TCP_FIN,65535,0,0,30100);
    tcp_tick(&c,30101); assert(c.state==TCP_DONE);
    connect(0,1000,65535); tcp_tick(&c,10200);
    c.rx_size=TCP_BUFFER;
    input(c.receive_next,c.next,TCP_ACK,65535,"x",1,20000);
    assert(c.state==TCP_FAILED && !strcmp(c.error,"receive file exceeds 16 KiB"));
    puts("PASS full-size receive EOF window and explicit oversized-file rejection");
    return 0;
}
