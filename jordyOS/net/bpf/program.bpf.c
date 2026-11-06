/* SPDX-License-Identifier: GPL-2.0-only
 * Native adapter for the pinned ebpfw code in ebpfw_shared.h/ebpfw_early.h.
 * Modifications: bounded legacy maps and stateless policy entry point. */
#include "ebpfw_shared.h"
#define SEC(s) __attribute__((section(s), used))
#define MAX_RULES 32
#define STAT_DROP (MAX_RULES + 9)
#define ALLOW 1
#define BLOCK 5
struct map_definition { u32 type, key_size, value_size, max_entries, flags; };
union policy_entry { struct header header; struct rule rule; };
#define MAP(n,t,k,v,c) struct map_definition n SEC("maps") = {t,sizeof(k),sizeof(v),c,0}
/* The embedder resolves map-symbol relocations to these named native maps. */
MAP(policy_template,2,u32,union policy_entry,MAX_RULES+1);
MAP(active,12,u32,u32,1);
MAP(stats,6,u32,struct counter,MAX_RULES+16);
MAP(early4,11,struct lpm_key,u32,MAX_RULES);
MAP(early6,11,struct lpm_key,u32,MAX_RULES);
MAP(early_enabled,2,u32,u32,1);
MAP(metadata,2,u32,struct packet,1);
static void *(*map_lookup)(void *, const void *) = (void *)1;
#include "ebpfw_count.h"
struct xdp_md {u32 data,data_end,data_meta,ingress_ifindex,rx_queue_index,egress_ifindex;};
#include "ebpfw_early.h"

SEC("jordyos/policy") int jordyos_policy(void *ctx) {
    (void)ctx;
    u32 zero = 0;
    u32 *enabled = map_lookup(&early_enabled, &zero);
    if (!enabled) return 1;
    if (!*enabled) return 2;
    void *policy = map_lookup(&active, &zero);
    if (!policy) return 1;
    struct header *h = map_lookup(policy, &zero);
    struct packet *p = map_lookup(&metadata, &zero);
    if (!h || !p || !h->ready || h->schema != 2 || h->count > MAX_RULES ||
        p->direction < 1 || p->direction > 2) return 1;
    u32 number = h->count, generation = h->generation;
    u32 action = h->defaults[p->direction - 1];
#pragma clang loop unroll(disable)
    for (u32 i = 0; i < MAX_RULES; i++) {
        if (i >= number) break;
        u32 key = i + 1;
        struct rule *r = map_lookup(policy, &key);
        if (!r) return 1;
        if (matches(r, p)) {
            count(r->index, generation);
            action = r->action;
            break;
        }
    }
    return action == ALLOW ? 2 : 1;
}
char LICENSE[] SEC("license") = "GPL";
