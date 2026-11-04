/* SPDX-License-Identifier: GPL-2.0-only
 * Adapted from duongddinh/ebpfw bpf/stateful.c, revision d1421287c5d57cb796e4a2e7d2c2767cec865908.
 * JordyOS adaptation, 2026-11-04: bounded maps and native interpreter ABI.
 * See UPSTREAM.md for exact source, scope, and modifications. */
/* Original source-block XDP function; only section name changed. */
SEC("xdp") int ebpfw_early(struct xdp_md *ctx){
 u32 z=0;u32 *enabled=map_lookup(&early_enabled,&z);if(!enabled||!*enabled)return 2;
 const u8 *data=(void *)(unsigned long)ctx->data,*end=(void *)(unsigned long)ctx->data_end;
 if(data+14>end)return 2;u32 off=14;u16 ether=be16(data+12);
#pragma unroll
 for(int i=0;i<2;i++){if(ether==0x8100||ether==0x88a8){if(data+off+4>end)return 2;ether=be16(data+off+2);off+=4;}}
 struct lpm_key key={};u32 *index=0;
 if(ether==0x0800){if(data+off+20>end||(data[off]>>4)!=4)return 2;key.prefix_len=32;__builtin_memcpy(key.address,data+off+12,4);index=map_lookup(&early4,&key);}
 else if(ether==0x86dd){if(data+off+40>end||(data[off]>>4)!=6)return 2;key.prefix_len=128;__builtin_memcpy(key.address,data+off+8,16);index=map_lookup(&early6,&key);}
 if(!index)return 2;void *policy=map_lookup(&active,&z);if(!policy)return 2;struct header *h=map_lookup(policy,&z);if(!h||!h->ready)return 2;
 count(*index,h->generation);count(STAT_DROP,h->generation);return 1;
}
