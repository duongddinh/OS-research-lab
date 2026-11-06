/* Bounded active-open TCP stream. The vendored LARP core is the sole CC.
 * No Linux ABI, allocation, interrupts, or host sockets are used here. */
#include "tcp.h"
#include "../util.h"

static bool before(uint32_t a, uint32_t b) { return (int32_t)(a-b) < 0; }
static uint32_t min32(uint32_t a, uint32_t b) { return a < b ? a : b; }
static unsigned inflight(const Tcp *c) {
    unsigned n=0;
    for (unsigned i=0;i<TCP_SLOTS;i++) n+=c->pending[i].used;
    return n;
}
static TcpSegment *oldest(Tcp *c) {
    TcpSegment *s=0;
    for (unsigned i=0;i<TCP_SLOTS;i++)
        if (c->pending[i].used && (!s || before(c->pending[i].sequence,s->sequence)))
            s=&c->pending[i];
    return s;
}
static bool output(Tcp *c, uint32_t seq, uint8_t flags, const uint8_t *data, unsigned size) {
    uint8_t b[24+TCP_MSS]={0};
    unsigned header=(flags&TCP_SYN)?24:20;
    net_write16(b,c->local_port); net_write16(b+2,c->peer_port);
    net_write32(b+4,seq); net_write32(b+8,c->receive_next);
    b[12]=(header/4)<<4; b[13]=flags;
    /* Reserve one sequence-space byte to receive EOF at the file-size limit.
     * Any additional payload byte is an explicit oversize error, never stored. */
    net_write16(b+14,min32(4096,TCP_BUFFER+1-c->rx_size));
    if (flags&TCP_SYN) { b[20]=2; b[21]=4; net_write16(b+22,TCP_MSS); }
    if (size) memcpy(b+header,data,size);
    return c->emit(c->context,b,header+size);
}
void tcp_abort(Tcp *c, const char *reason) {
    if (c->state==TCP_ESTABLISHED || c->state==TCP_CLOSING)
        output(c,c->next,TCP_RST|TCP_ACK,0,0);
    c->state=TCP_FAILED; c->error=reason;
}
static bool transmit(Tcp *c, TcpSegment *s, uint64_t now) {
    if (!output(c,s->sequence,s->flags,s->length?c->tx+s->offset:0,s->length)) return false;
    s->sent_us=now;
    if (s->length) {
        if (c->last_data_us) {
            uint64_t gap=now-c->last_data_us;
            if (gap<c->minimum_gap_us) c->minimum_gap_us=(uint32_t)gap;
        }
        c->last_data_us=now; c->paced_packets++;
        /* Always schedule from now: no accumulated credit or catch-up burst. */
        c->next_send_us=now+larp_max(1,larp_div((uint64_t)s->length*1000000+
            c->larp.rate-1,c->larp.rate));
    }
    return true;
}
static void feedback(Tcp *c, const struct larp_feedback *f, unsigned acked,
                     enum larp_recovery recovery) {
    larp_update(&c->larp,f); c->feedbacks++;
    uint32_t target=larp_window(&c->larp,c->mss,TCP_SLOTS);
    c->cwnd=larp_ack_window(c->cwnd,target,inflight(c),acked,
        TCP_SLOTS,recovery);
}
void tcp_start(Tcp *c,uint16_t local,uint16_t peer,uint32_t isn,
    const uint8_t *data,size_t size,uint64_t now,
    bool (*emit)(void *,const uint8_t *,size_t),void *context) {
    memset(c,0,sizeof *c);
    c->emit=emit; c->context=context;
    if (!local || !peer || size>TCP_BUFFER || (size && !data)) {
        c->state=TCP_FAILED; c->error="invalid TCP transfer"; return;
    }
    if (size) memcpy(c->tx,data,size);
    c->tx_size=size; c->local_port=local; c->peer_port=peer;
    c->isn=c->una=c->next=isn; c->mss=TCP_MSS;
    c->rto_us=1000000; c->minimum_gap_us=UINT32_MAX;
    c->started_us=now; c->state=TCP_SYN_SENT;
    larp_start(&c->larp,125000000,125000,0,500,now);
    c->cwnd=larp_window(&c->larp,c->mss,TCP_SLOTS);
}
static void rtt_sample(Tcp *c,uint32_t rtt) {
    if (!rtt) rtt=1;
    if (!c->srtt_us) { c->srtt_us=rtt; c->rttvar_us=rtt/2; }
    else {
        uint32_t difference=c->srtt_us>rtt?c->srtt_us-rtt:rtt-c->srtt_us;
        c->rttvar_us=(3*c->rttvar_us+difference)/4;
        c->srtt_us=(7*c->srtt_us+rtt)/8;
    }
    c->rto_us=(uint32_t)larp_min(60000000,larp_max(1000000,
        (uint64_t)c->srtt_us+larp_max(1000,4*(uint64_t)c->rttvar_us)));
}
static void loss(Tcp *c,uint64_t now,bool timeout) {
    TcpSegment *s=oldest(c);
    if (!s) return;
    if (s->retries>=8) { tcp_abort(c,"retransmission limit"); return; }
    if (now<c->next_send_us && s->length) return;
    if (!transmit(c,s,now)) return;
    s->retries++; c->retransmits++; c->timer_us=now;
    if (timeout) {
        c->timeouts++; c->rto_us=min32(60000000,c->rto_us*2);
    } else c->fast_retransmits++;
    /* Handshake loss affects the handshake timer, not established CC state. */
    if (c->state==TCP_SYN_SENT) return;
    if (timeout) c->cwnd=1;
    c->recovery=true; c->recovery_end=c->next;
    struct larp_feedback f={.now_us=now,.loss=1,.in_recovery=1,.inflight=inflight(c)};
    feedback(c,&f,0,timeout?LARP_TIMEOUT:LARP_RECOVERY);
}
static void ack(Tcp *c,uint32_t acknowledgement,uint64_t now) {
    if (before(acknowledgement,c->una) || before(c->next,acknowledgement)) {
        c->invalid_acks++; return;
    }
    if (acknowledgement==c->una) {
        if (oldest(c) && ++c->dupacks==3 && !c->recovery) loss(c,now,false);
        return;
    }
    unsigned packets=0; uint32_t bytes=0,rtt=0;
    bool retransmitted=false,have_sample=false;
    TcpSegment sample={0};
    for (unsigned i=0;i<TCP_SLOTS;i++) {
        TcpSegment *s=&c->pending[i];
        if (!s->used || !before(s->sequence,acknowledgement)) continue;
        uint32_t consumed=acknowledgement-s->sequence;
        unsigned sequence_size=s->length+!!(s->flags&(TCP_SYN|TCP_FIN));
        retransmitted|=s->retries!=0;
        if (!s->retries && (!have_sample || before(sample.sequence,s->sequence))) {
            sample=*s; have_sample=true;
        }
        if (consumed>=sequence_size) {
            bytes+=s->length; packets++;
            if (s->flags&TCP_FIN) c->own_fin_acked=true;
            s->used=false;
        } else {
            bytes+=consumed; s->sequence+=consumed;
            s->offset+=consumed; s->length-=consumed;
        }
    }
    c->una=acknowledgement; c->dupacks=0; c->timer_us=now;
    c->delivered+=bytes;
    if (have_sample && !retransmitted) {
        rtt=(uint32_t)larp_min(now-sample.sent_us,60000000);
        rtt_sample(c,rtt);
    }
    uint64_t interval=have_sample?larp_max(now-sample.delivered_us,
        sample.sent_us-sample.flight_us):0;
    uint32_t delivered=have_sample?c->delivered-sample.delivered:0;
    struct larp_feedback f={.now_us=now,.rtt_us=rtt,.inflight=inflight(c),
        .in_recovery=c->recovery,.ecn=0, /* ECN was not negotiated. */
        .app_limited=!have_sample || sample.app_limited,
        .window_limited=have_sample && !sample.app_limited,
        .valid_delivery=!retransmitted && delivered>=4*c->mss &&
            interval>=larp_max(c->larp.min_rtt_us/8,1),
        .delivery_rate=interval?larp_div((uint64_t)delivered*1000000,interval):0,
        .sample_start_us=have_sample?sample.delivered_us:0};
    feedback(c,&f,packets,c->recovery?LARP_RECOVERY:LARP_OPEN);
    c->delivered_us=now;
    if (c->recovery && !before(acknowledgement,c->recovery_end)) c->recovery=false;
}
void tcp_input(Tcp *c,const uint8_t *b,size_t size,uint64_t now) {
    if (size<20 || (b[12]>>4)<5 || (unsigned)(b[12]>>4)*4>size ||
        net_read16(b)!=c->peer_port || net_read16(b+2)!=c->local_port) return;
    unsigned header=(b[12]>>4)*4;
    uint32_t seq=net_read32(b+4),a=net_read32(b+8);
    uint8_t flags=b[13]; unsigned length=size-header;
    if (c->state==TCP_SYN_SENT) {
        if ((flags&TCP_RST) && (flags&TCP_ACK) && a==c->isn+1) {
            tcp_abort(c,"connection refused"); return;
        }
        if ((flags&(TCP_SYN|TCP_ACK|TCP_RST|TCP_FIN))!=(TCP_SYN|TCP_ACK) ||
            a!=c->isn+1 || c->next!=a || length) return;
        unsigned mss=TCP_MSS;
        for (unsigned i=20;i<header;) {
            unsigned kind=b[i]; if (!kind) break;
            if (kind==1) { i++; continue; }
            if (i+1>=header || b[i+1]<2 || i+b[i+1]>header) return;
            if (kind==2) {
                if (b[i+1]!=4 || !net_read16(b+i+2)) return;
                mss=min32(mss,net_read16(b+i+2));
            }
            i+=b[i+1];
        }
        c->mss=mss; c->receive_next=seq+1;
        c->peer_window=net_read16(b+14); c->window_seq=seq; c->window_ack=a;
        c->una=a; c->pending[0].used=false;
        if (!c->pending[0].retries) {
            rtt_sample(c,(uint32_t)larp_min(now-c->pending[0].sent_us,60000000));
            c->larp.min_rtt_us=c->srtt_us; c->larp.rtt_stamp_us=now;
        } else c->rto_us=3000000;
        c->state=TCP_ESTABLISHED; c->ack_pending=true;
        c->delivered_us=now;
        return;
    }
    if (c->state!=TCP_ESTABLISHED && c->state!=TCP_CLOSING && c->state!=TCP_DONE) return;
    if (flags&TCP_RST) {
        if (seq==c->receive_next) { c->state=TCP_FAILED; c->error="peer reset"; }
        else c->ack_pending=true;
        return;
    }
    /* Exact duplicate SYN/ACK after our handshake ACK was lost. */
    if ((flags&TCP_SYN) && (flags&TCP_ACK) && seq+1==c->receive_next && a==c->isn+1) {
        c->ack_pending=true; return;
    }
    if (flags&TCP_SYN || !(flags&TCP_ACK)) return;
    uint32_t window=min32(4096,TCP_BUFFER+1-c->rx_size);
    unsigned sequence_length=length+!!(flags&TCP_FIN);
    bool acceptable=sequence_length?
        (!before(seq,c->receive_next+window) ? false :
         !before(seq+sequence_length-1,c->receive_next)):
        seq==c->receive_next || (window && !before(seq,c->receive_next) &&
            before(seq,c->receive_next+window));
    if (!acceptable) { c->ack_pending=true; return; }
    if (before(c->next,a)) { c->invalid_acks++; c->ack_pending=true; return; }
    if (before(a,c->una)) return;
    bool window_update=before(c->window_seq,seq) ||
        (c->window_seq==seq && !before(a,c->window_ack));
    bool window_changed=false;
    if (window_update) {
        uint16_t previous=c->peer_window;
        c->peer_window=net_read16(b+14); c->window_seq=seq; c->window_ack=a;
        window_changed=previous!=c->peer_window;
        if (window_changed) c->dupacks=0;
    }
    /* Data-bearing and window-update ACKs are not duplicate-loss signals. */
    if (a!=c->una || (!length && !(flags&TCP_FIN) && !window_changed)) ack(c,a,now);
    if (length || (flags&TCP_FIN)) {
        c->ack_pending=true;
        if (before(seq,c->receive_next)) {
            uint32_t skip=c->receive_next-seq;
            unsigned trim=min32(skip,length);
            b+=trim; length-=trim; seq+=trim;
        }
        if (!c->peer_fin && seq==c->receive_next) {
            if (length>TCP_BUFFER-c->rx_size) { tcp_abort(c,"receive file exceeds 16 KiB"); return; }
            if (length) memcpy(c->rx+c->rx_size,b+header,length);
            c->rx_size+=length; c->receive_next+=length;
            if ((flags&TCP_FIN) && !c->peer_fin) {
                c->peer_fin=true; c->receive_next++;
            }
        }
    }
}
void tcp_tick(Tcp *c,uint64_t now) {
    if (c->state==TCP_IDLE || c->state==TCP_FAILED) return;
    if (c->state!=TCP_DONE && now-c->started_us>120000000) {
        tcp_abort(c,"120-second transfer deadline"); return;
    }
    if (c->ack_pending && output(c,c->next,TCP_ACK,0,0)) c->ack_pending=false;
    if (c->state==TCP_DONE) return;
    TcpSegment *old=oldest(c);
    if (old && now-c->timer_us>=c->rto_us) { loss(c,now,true); return; }
    if (c->state==TCP_SYN_SENT) {
        if (c->next!=c->isn) return;
        TcpSegment s={.sequence=c->isn,.flags=TCP_SYN,.used=true};
        if (transmit(c,&s,now)) {
            c->pending[0]=s; c->next++; c->timer_us=now;
        }
        return;
    }
    if (c->own_fin_acked && c->peer_fin && !c->ack_pending) { c->state=TCP_DONE; return; }
    if (!c->peer_window && now-c->probe_us>=1000000) {
        uint8_t byte=c->tx_offset?c->tx[c->tx_offset-1]:0;
        if (output(c,c->next-1,TCP_ACK,&byte,1)) c->probe_us=now;
        return;
    }
    if (c->own_fin || now<c->next_send_us || inflight(c)>=c->cwnd) return;
    uint32_t outstanding=c->next-c->una;
    if (outstanding>=c->peer_window) return;
    TcpSegment *slot=0;
    for (unsigned i=0;i<TCP_SLOTS;i++) if (!c->pending[i].used) { slot=&c->pending[i]; break; }
    if (!slot) return;
    if (!inflight(c)) { c->flight_us=now; c->delivered_us=now; }
    TcpSegment s={.sequence=c->next,.offset=c->tx_offset,.flags=TCP_ACK|TCP_PSH,
        .delivered=c->delivered,.delivered_us=c->delivered_us,.flight_us=c->flight_us,
        .app_limited=c->tx_size-(c->una-c->isn-1)<min32(c->cwnd*c->mss,c->peer_window),
        .used=true};
    s.length=min32(c->mss,min32(c->tx_size-c->tx_offset,c->peer_window-outstanding));
    if (!s.length) {
        /* FIN follows acknowledged data, simplifying close/recovery ordering. */
        if (c->tx_offset!=c->tx_size || inflight(c)) return;
        s.flags=TCP_ACK|TCP_FIN;
    }
    if (!transmit(c,&s,now)) return;
    if (!old) c->timer_us=now;
    *slot=s; c->next+=s.length+!!(s.flags&TCP_FIN); c->tx_offset+=s.length;
    if (s.flags&TCP_FIN) { c->own_fin=true; c->state=TCP_CLOSING; }
}
