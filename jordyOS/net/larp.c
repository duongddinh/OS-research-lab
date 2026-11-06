/* Exercise the unmodified vendored controller in both the kernel and host tests.
 * A future TCP sender must supply real feedback and enforce rate/window outputs. */
#include "larp.h"
#include <larp_core.h>

const char *larp_selftest(void) {
    struct larp_model m;
    struct larp_feedback f;

    /* Upstream launch defaults: 1 Gbps = 125,000,000 bytes/s; 512 packets. */
    larp_start(&m, 125000000, 125000, 10000, 500, 0);
    if (m.rate != 125000000 || m.learned ||
        larp_window(&m, 1000, 512) != 512)
        return "launch rate and flight limit";
    f = (struct larp_feedback){.now_us = 10000, .rtt_us = 10000,
        .valid_delivery = 1, .delivery_rate = 200000000,
        .sample_start_us = 0, .inflight = 512, .window_limited = 1};
    larp_update(&m, &f);
    /* Clear-delay delivery feedback permits 102% of the measured rate. */
    if (!m.learned || m.rate != 204000000 ||
        larp_window(&m, 1000, 65535) != 4080)
        return "measured delivery growth";
    f = (struct larp_feedback){.now_us = 20000, .rtt_us = 10000,
        .loss = 1, .in_recovery = 1};
    larp_update(&m, &f);
    if (m.rate != 142800000 || m.mode != LARP_RELAX)
        return "loss reduction";

    larp_start(&m, 1000000, 125000, 10000, 500, 0);
    f = (struct larp_feedback){.now_us = 10000, .rtt_us = 10000, .ecn = 1};
    larp_update(&m, &f);
    if (m.rate != 700000) return "ECN reduction";
    f.now_us++;
    larp_update(&m, &f);
    if (m.rate != 700000 || !m.pending_congestion)
        return "once-per-RTT congestion guard";
    f.now_us = 20000;
    f.ecn = 0;
    larp_update(&m, &f);
    if (m.rate != 490000 || m.pending_congestion)
        return "latched congestion feedback";

    /* Fresh delay samples are required before increasing the rate. */
    larp_start(&m, 1000000, 125000, 10000, 500, 0);
    f = (struct larp_feedback){.now_us = 10000,
        .valid_delivery = 1, .delivery_rate = 2000000};
    larp_update(&m, &f);
    if (m.rate != 1000000 || m.learned) return "missing RTT inhibits growth";
    f.rtt_us = 10000;
    f.app_limited = 1;
    larp_update(&m, &f);
    if (m.rate != 1000000 || m.learned) return "application-limited sample";

    /* Expired minimum RTT requires actual low-flight samples to refresh. */
    larp_start(&m, 125000000, 125000, 10000, 500, 0);
    f = (struct larp_feedback){.now_us = 10000, .rtt_us = 10000,
        .valid_delivery = 1, .delivery_rate = 200000000, .inflight = 10};
    larp_update(&m, &f);
    f.now_us = 10000000;
    larp_update(&m, &f);
    if (!m.refresh_active || m.mode != LARP_PROBE_RTT ||
        larp_window(&m, 1000, 65535) != 4)
        return "RTT probe flight limit";
    f.inflight = 4;
    f.now_us = 10010000;
    larp_update(&m, &f);
    f.now_us = 10110000;
    larp_update(&m, &f);
    f.now_us = 10210000;
    larp_update(&m, &f);
    if (m.refresh_active || m.refresh_samples != 3 ||
        m.min_rtt_us != 10000 || m.rtt_stamp_us != f.now_us)
        return "RTT probe completion";

    if (larp_ack_window(10, 100, 8, 2, 5, LARP_OPEN) != 12 ||
        larp_ack_window(10, 100, 8, 2, 5, LARP_RECOVERY) != 10 ||
        larp_ack_window(10, 100, 8, 2, 5, LARP_RECOVERY_GROW) != 12 ||
        larp_ack_window(10, 100, 8, 2, 5, LARP_CWR) != 5 ||
        larp_ack_window(10, 100, 8, 2, 5, LARP_TIMEOUT) != 12)
        return "ACK and recovery windows";

    larp_u64 maximum = ~(larp_u64)0;
    if (larp_add(maximum, 1) != maximum ||
        larp_scale(maximum, 2, 1) != maximum)
        return "saturating arithmetic";
    return 0;
}
