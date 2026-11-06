#include "tcp.h"
#include "tcp_api.h"
#include "network.h"
#include "firewall.h"
#include "../util.h"

static Tcp connection;
static bool running, resolving, have_mac, clock_ready;
static uint32_t peer_ip, next_port=49152;
static uint8_t peer_mac[6], source[TCP_BUFFER];
static uint64_t clock_origin, ticks_per_us;
typedef struct {
    uint32_t ip, next, acknowledgement;
    uint16_t local, remote;
    uint8_t mac[6];
    uint64_t expires;
} TimeWait;
static TimeWait timewait[8];

static uint8_t in8(uint16_t p) {
    uint8_t n; __asm__ volatile("inb %1,%0":"=a"(n):"Nd"(p):"memory"); return n;
}
static void out8(uint16_t p,uint8_t n) {
    __asm__ volatile("outb %0,%1"::"a"(n),"Nd"(p):"memory");
}
static uint64_t tsc(void) {
    uint32_t lo,hi;
    __asm__ volatile("lfence; rdtsc":"=a"(lo),"=d"(hi)::"memory");
    return (uint64_t)hi<<32|lo;
}
static bool clock_init(void) {
    if (clock_ready) return true;
    uint32_t a=1,b,c,d;
    __asm__ volatile("cpuid":"+a"(a),"=b"(b),"=c"(c),"=d"(d));
    if (!(d&(1u<<4))) return false;
    uint8_t gate=in8(0x61);
    out8(0x61,gate&~3u); out8(0x43,0xb0);
    out8(0x42,23864&255); out8(0x42,23864>>8); /* approximately 20 ms */
    uint64_t start=tsc(); out8(0x61,(gate&~2u)|1);
    while (!(in8(0x61)&0x20)) {
        if (tsc()-start>10000000000ull) { out8(0x61,gate); return false; }
        __asm__ volatile("pause");
    }
    uint64_t end=tsc(); out8(0x61,gate);
    ticks_per_us=(end-start)/20000;
    if (!ticks_per_us) return false;
    clock_origin=end; clock_ready=true; return true;
}
static uint64_t now_us(void) { return (tsc()-clock_origin)/ticks_per_us; }

static bool send_to(uint32_t ip,const uint8_t *mac,const uint8_t *body,size_t size) {
    uint8_t frame[14+20+24+TCP_MSS]={0};
    memcpy(frame,mac,6); memcpy(frame+6,net_mac,6); net_write16(frame+12,0x0800);
    uint8_t *h=frame+14;
    h[0]=0x45; h[8]=64; h[9]=NET_TCP; net_write16(h+2,20+size);
    net_write32(h+12,NET_IP); net_write32(h+16,ip);
    net_write16(h+10,net_checksum(h,20,0)); memcpy(h+20,body,size);
    uint32_t pseudo=net_read16(h+12)+net_read16(h+14)+net_read16(h+16)+
        net_read16(h+18)+NET_TCP+size;
    net_write16(h+20+16,net_checksum(h+20,size,pseudo));
    Packet outgoing;
    if (!net_decode(h,20+size,&outgoing) || !fw_accept(&native_firewall,&outgoing,FW_OUT)) return false;
    return rtl_send(frame,14+20+size);
}
static bool emit(void *context,const uint8_t *body,size_t size) {
    (void)context; return have_mac && send_to(peer_ip,peer_mac,body,size);
}
static bool arp_request(void) {
    uint8_t frame[42]={0};
    memset(frame,255,6); memcpy(frame+6,net_mac,6); net_write16(frame+12,0x0806);
    net_write16(frame+14,1); net_write16(frame+16,0x0800);
    frame[18]=6; frame[19]=4; net_write16(frame+20,1);
    memcpy(frame+22,net_mac,6); net_write32(frame+28,NET_IP);
    net_write32(frame+38,peer_ip); return rtl_send(frame,sizeof frame);
}
void tcp_net_arp(const uint8_t *f,size_t size) {
    if (!resolving || size<42 || net_read16(f+14)!=1 || net_read16(f+16)!=0x0800 ||
        f[18]!=6 || f[19]!=4 || net_read16(f+20)!=2 ||
        memcmp(f,net_mac,6) || memcmp(f+6,f+22,6) || (f[6]&1) ||
        net_read32(f+28)!=peer_ip || memcmp(f+32,net_mac,6) || net_read32(f+38)!=NET_IP) return;
    memcpy(peer_mac,f+22,6); have_mac=true;
}
void tcp_net_input(const uint8_t *frame,const Packet *packet) {
    const uint8_t *b=frame+34;
    size_t size=packet->length-20;
    if (!clock_ready) return;
    uint64_t now=now_us();
    for (unsigned i=0;i<8;i++) {
        TimeWait *w=&timewait[i];
        if (w->expires<=now || w->ip!=packet->source || w->local!=packet->destination_port ||
            w->remote!=packet->source_port) continue;
        if ((b[13]&(TCP_FIN|TCP_RST))==TCP_FIN &&
            net_read32(b+4)+(uint32_t)(size-(b[12]>>4)*4)+1==w->acknowledgement) {
            uint8_t reply[20]={0};
            net_write16(reply,w->local); net_write16(reply+2,w->remote);
            net_write32(reply+4,w->next); net_write32(reply+8,w->acknowledgement);
            reply[12]=0x50; reply[13]=TCP_ACK;
            send_to(w->ip,w->mac,reply,sizeof reply);
            w->expires=now+120000000;
        }
        return;
    }
    if (running && packet->source==peer_ip && !memcmp(frame+6,peer_mac,6))
        tcp_input(&connection,b,size,now);
}
void tcp_net_poll(void) {
    if (running) tcp_tick(&connection,now_us());
}
void tcp_net_down(void) {
    if (running) tcp_abort(&connection,"network interface stopped");
    have_mac=false;
}
void tcp_status(void) {
    sys_write("TCP: native active-open IPv4, one foreground transfer, 16 KiB per file.\n"
        "Default congestion control: LARP (vendored upstream core).\n");
    sys_write("last tx_acked="); net_print_number(connection.delivered);
    sys_write(" rx="); net_print_number(connection.rx_size);
    sys_write(" retransmits="); net_print_number(connection.retransmits);
    sys_write(" timeouts="); net_print_number(connection.timeouts);
    sys_write(" fast_retransmits="); net_print_number(connection.fast_retransmits);
    sys_write("\nlarp feedbacks="); net_print_number(connection.feedbacks);
    sys_write(" learned="); net_print_number(connection.larp.learned);
    sys_write(" cwnd="); net_print_number(connection.cwnd);
    sys_write(" rate_bytes_sec="); net_print_number((uint32_t)larp_min(connection.larp.rate,UINT32_MAX));
    sys_write(" paced_packets="); net_print_number(connection.paced_packets);
    sys_write(" min_gap_us="); net_print_number(connection.minimum_gap_us==UINT32_MAX?0:connection.minimum_gap_us);
    sys_write("\n");
}
void tcp_command(char *arguments) {
    char *w[6]; unsigned n=0;
    while (*arguments) {
        while (*arguments==' ') arguments++;
        if (!*arguments) break;
        if (n==6) goto usage;
        w[n++]=arguments;
        while (*arguments && *arguments!=' ') arguments++;
        if (*arguments) *arguments++=0;
    }
    if (!n || (n==1 && !strcmp(w[0],"status"))) { tcp_status(); return; }
    bool demo=!strcmp(w[0],"demo"),send=!strcmp(w[0],"send"),
        receive=!strcmp(w[0],"recv"),exchange=!strcmp(w[0],"exchange");
    if (!((demo && n==3) || ((send || receive) && n==4) || (exchange && n==5))) goto usage;
    uint32_t mask,ip,port;
    if (!net_cidr(w[1],&ip,&mask) || mask!=UINT32_MAX ||
        (ip&NET_MASK)!=(NET_IP&NET_MASK) || (ip&255)==0 || (ip&255)==255 || ip==NET_IP ||
        !net_number(w[2],65535,&port) || !port) goto usage;
    if (!native_network.up) { sys_write("Run net up first.\n"); return; }
    if (!clock_init()) { sys_write("TCP requires a working PIT-calibrated TSC clock.\n"); return; }
    if (next_port>65535) { sys_write("TCP ephemeral ports exhausted; reboot the lab.\n"); return; }
    uint64_t now=now_us(); TimeWait *wait=0;
    for (unsigned i=0;i<8;i++) if (timewait[i].expires<=now) { wait=&timewait[i]; break; }
    if (!wait) { sys_write("TCP TIME-WAIT slots full; retry after 120 seconds.\n"); return; }
    int size=0;
    if (demo) {
        size=TCP_BUFFER;
        for (unsigned i=0;i<TCP_BUFFER;i++) source[i]=(uint8_t)(i*37+(i>>8));
    } else if (send || exchange) {
        size=sys_read_file(w[3],(char *)source,sizeof source);
        if (size<0) { sys_write("Cannot read source RAM file.\n"); return; }
    }
    const char *destination=receive?w[3]:exchange?w[4]:0;
    if (destination && strlen(destination)>=MAX_FILENAME_LEN) goto usage;
    peer_ip=ip; have_mac=false; resolving=true;
    uint64_t end=now_us()+5000000,next_arp=0;
    while (!have_mac && native_network.up && now_us()<end) {
        now=now_us();
        if (now>=next_arp && arp_request()) next_arp=now+1000000;
        rtl_poll(); __asm__ volatile("pause");
    }
    resolving=false;
    if (!have_mac) { sys_write("TCP failed: ARP lookup timed out.\n"); return; }
    uint16_t local=next_port++;
    now=now_us();
    tcp_start(&connection,local,port,(uint32_t)tsc(),source,size,now,emit,0);
    running=true;
    sys_write("TCP connecting; default controller=LARP.\n");
    while (connection.state!=TCP_DONE && connection.state!=TCP_FAILED) {
        char key=sys_pollc();
        if (key==3 || key==27) tcp_abort(&connection,"cancelled");
        __asm__ volatile("pause");
    }
    running=false;
    if (connection.state==TCP_DONE) {
        *wait=(TimeWait){.ip=peer_ip,.next=connection.next,.acknowledgement=connection.receive_next,
            .local=local,.remote=port,.expires=now_us()+120000000};
        memcpy(wait->mac,peer_mac,6);
        if (demo && (connection.rx_size!=TCP_BUFFER || memcmp(connection.rx,source,TCP_BUFFER)))
            sys_write("TCP DEMO FAIL: echoed bytes differ.\n");
        else if (destination && sys_write_file(destination,(char *)connection.rx,connection.rx_size)<0)
            sys_write("TCP transfer complete; could not save destination RAM file.\n");
        else sys_write(demo?"TCP DEMO PASS: 16384 binary bytes echoed exactly using LARP.\n":
            "TCP TRANSFER PASS: bytes acknowledged, peer EOF received.\n");
    } else {
        sys_write("TCP failed: "); sys_write(connection.error); sys_write("\n");
    }
    tcp_status(); return;
usage:
    sys_write("tcp status | demo IP PORT | send IP PORT FILE | recv IP PORT FILE\n"
        "tcp exchange IP PORT SOURCE DESTINATION (local /24, files <=16 KiB)\n");
}
