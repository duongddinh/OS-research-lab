/* SPDX-License-Identifier: GPL-2.0-only
 * Adapted from duongddinh/ebpfw bpf/stateful.c, revision d1421287c5d57cb796e4a2e7d2c2767cec865908.
 * JordyOS adaptation, 2026-11-04: bounded maps and native interpreter ABI.
 * See UPSTREAM.md for exact source, scope, and modifications. */
#ifndef JORDY_EBPFW_SHARED_H
#define JORDY_EBPFW_SHARED_H
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
#define INLINE static __inline __attribute__((always_inline))
struct rule {
 u8 action,direction,family,proto;
 u8 src_bits,dst_bits,log,pad;
 u16 sport_lo,sport_hi,dport_lo,dport_hi;
 u32 rate,burst;
 u8 src[16],dst[16],iif[16],oif[16];
 u32 index,reserved;
};
struct header {u32 count,blocks,defaults[3],logging,generation,ready,schema;};
struct packet {
 u8 src[16],dst[16],iif[16],oif[16];
 u32 len,l4,payload,seq,ack;
 u16 sport,dport,window;
 u8 family,proto,direction,flags,icmp_type,icmp_code,hoplimit,wscale;
};
struct counter {u64 packets;u32 generation,pad;};
struct lpm_key {u32 prefix_len;u8 address[16];};
INLINE u16 be16(const u8 *p){return ((u16)p[0]<<8)|p[1];}
INLINE u32 be32(const u8 *p){return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3];}
INLINE int prefix(const u8 *a,const u8 *b,u8 bits){
#pragma unroll
 for(int i=0;i<16;i++){if(bits>i*8){u32 n=bits-i*8;u8 mask=n>=8?255:(u8)(255<<(8-n));if((a[i]&mask)!=(b[i]&mask))return 0;}}
 return 1;
}
INLINE int name_equal(const u8 *a,const u8 *b){
#pragma unroll
 for(int i=0;i<16;i++){if(a[i]!=b[i])return 0;if(!a[i])return 1;}
 return 1;
}
INLINE int matches(struct rule *r,struct packet *p){
 if(r->direction&&r->direction!=p->direction)return 0;
 if(r->family&&r->family!=p->family)return 0;if(r->proto&&r->proto!=p->proto)return 0;
 if(r->sport_lo&&(p->sport<r->sport_lo||p->sport>r->sport_hi))return 0;
 if(r->dport_lo&&(p->dport<r->dport_lo||p->dport>r->dport_hi))return 0;
 if(r->iif[0]&&!name_equal(r->iif,p->iif))return 0;if(r->oif[0]&&!name_equal(r->oif,p->oif))return 0;
 return prefix(r->src,p->src,r->src_bits)&&prefix(r->dst,p->dst,r->dst_bits);
}
#endif
