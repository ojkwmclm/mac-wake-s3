// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include "recovery_policy.h"
int main(void)
{
    // A press must complete before ready permits submitting its zero report.
    hid_transfer_t t={0};
    assert(transfer_arm(&t,1)); assert(!transfer_arm(&t,1));
    assert(transfer_finish(&t,1,true,true)==0 && !t.armed);
    assert(transfer_arm(&t,1)); t.zero=true;
    assert(!transfer_arm(&t,1));
    assert(transfer_finish(&t,1,true,true)==1 && !t.armed);
    assert(transfer_arm(&t,1)); t.zero=true;
    assert(transfer_finish(&t,1,false,true)==2);
    // Unmount invalidates any armed transfer; trailing callbacks before rearm ignored.
    assert(transfer_arm(&t,1)); t.zero=true;
    t=(hid_transfer_t){0};
    assert(transfer_finish(&t,2,true,true)==0);
    assert(transfer_arm(&t,2)); t.zero=true;
    assert(transfer_finish(&t,1,true,true)==0 && t.armed);
    assert(transfer_finish(&t,2,true,true)==1);
    release_policy_t p={0};
    // Overnight host sleep is not an active-bus failure and must retain release debt.
    release_begin(&p,0);
    assert(!release_poll(&p,100,true,true,false));
    assert(!release_poll(&p,INT64_C(43200000000),true,true,false));
    assert(p.pending && !p.timed_out && p.active_us==0);
    assert(release_poll(&p,INT64_C(43200000001),true,false,false));
    assert(!p.timed_out);
    // Successful submission is not completion; no overlapping submissions.
    release_attempt(&p,INT64_C(43200000001),true);
    assert(!release_poll(&p,INT64_C(43200020001),true,false,false));
    assert(p.pending && p.inflight);
    assert(!release_poll(&p,INT64_C(43200030001),true,false,true));
    assert(!p.pending && !p.inflight);
    // Active-bus stall produces a terminal warning while preserving release debt.
    release_begin(&p,0); assert(release_poll(&p,0,true,false,false));
    assert(release_poll(&p,5000000,true,false,false)); assert(p.timed_out && p.pending);
    release_attempt(&p,5000000,false);
    assert(!release_poll(&p,5500000,true,false,false));
    assert(release_poll(&p,6000000,true,false,false));
    // A missing completion is explicitly diagnosed, not silently recycled.
    release_begin(&p,0); release_poll(&p,0,true,false,false);
    release_attempt(&p,0,true);
    assert(!release_poll(&p,5000000,true,false,false));
    assert(p.pending && p.inflight && p.inflight_timed_out);
    assert(!release_poll(&p,10000000,true,false,false));
    // A real failure callback retires the transfer, not the release debt.
    p.inflight=false;
    assert(release_poll(&p,10020000,true,false,false));
    assert(p.pending);
    release_attempt(&p,10020000,true);
    assert(!p.inflight_timed_out);
    assert(!release_poll(&p,10040000,true,false,true)); assert(!p.pending);
    // Actual endpoint loss invalidates old pending reports.
    assert(!release_poll(&p,6000010,false,false,false)); assert(!p.pending);
    // Separate sleep intervals do not count toward the active budget.
    release_begin(&p,0); release_poll(&p,0,true,false,false);
    release_poll(&p,2000000,true,false,false); assert(p.active_us==2000000);
    release_poll(&p,2000010,true,true,false);
    release_poll(&p,INT64_C(86400000000),true,true,false);
    release_poll(&p,INT64_C(86400000010),true,false,false);
    assert(p.active_us==2000000 && !p.timed_out);
    // Retry backoff has a floor, grows on consecutive failures, saturates and resets.
    const unsigned want[]={1500,1500,3000,6000,12000,24000,30000,30000};
    for (unsigned i=0;i<8;++i) assert(publish_retry_ms(i)==want[i]);
    assert(publish_retry_ms(100000)==30000);
    assert(publish_retry_ms(0)==1500);
    puts("Release policy: long sleep, completion, stall, retry and disconnect scenarios passed.");
}
