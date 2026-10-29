// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>

// All input reports use this gate, not only zero/release reports.
typedef struct { bool armed, zero; uint32_t epoch; } hid_transfer_t;
static inline bool transfer_arm(hid_transfer_t *t, uint32_t epoch)
{
    if (t->armed) return false;
    *t=(hid_transfer_t){.armed=true, .zero=false, .epoch=epoch};
    return true;
}
// 0: ignored/non-release completion; 1: zero report complete; 2: zero failed.
static inline unsigned transfer_finish(hid_transfer_t *t, uint32_t epoch,
                                      bool ok, bool full_length)
{
    if (!t->armed || t->epoch!=epoch) return 0;
    unsigned event=t->zero ? ((ok && full_length) ? 1 : 2) : 0;
    t->armed=false;
    return event;
}
// Operational release budget, not a proven hardware failure threshold.
#define RELEASE_ACTIVE_BUDGET_US INT64_C(5000000)
typedef struct {
    bool pending, inflight, timed_out, inflight_timed_out;
    int64_t active_us, last_us, next_try_us, submitted_active_us;
    bool previous_active;
} release_policy_t;
static inline void release_begin(release_policy_t *p, int64_t now)
{
    *p = (release_policy_t){.pending=true, .last_us=now};
}
static inline bool release_poll(release_policy_t *p, int64_t now,
                                bool mounted, bool suspended, bool completed)
{
    if (!mounted || completed) { *p = (release_policy_t){0}; return false; }
    if (!p->pending) return false;
    const bool active = !suspended;
    if (active && p->previous_active) p->active_us += now - p->last_us;
    p->last_us = now; p->previous_active = active;
    if (p->active_us >= RELEASE_ACTIVE_BUDGET_US) p->timed_out = true;
    if (p->inflight && p->active_us-p->submitted_active_us >= RELEASE_ACTIVE_BUDGET_US)
        p->inflight_timed_out=true;
    // A missing callback is ambiguous even if the endpoint appears ready.
    // Keep the debt; only a matching callback or new USB epoch can retire it.
    return active && !p->inflight && now >= p->next_try_us;
}
static inline void release_attempt(release_policy_t *p, int64_t now, bool submitted)
{
    p->inflight = submitted;
    p->inflight_timed_out=false;
    if (submitted) p->submitted_active_us=p->active_us;
    p->next_try_us = now + (p->timed_out ? INT64_C(1000000) : INT64_C(20000));
}
static inline uint32_t publish_retry_ms(unsigned failures)
{
    // First retry at 1.5 seconds; cap at 30 seconds. Do not update only on success.
    unsigned shift = failures > 5 ? 5 : (failures ? failures - 1 : 0);
    uint32_t ms = 1500U << shift;
    return ms > 30000 ? 30000 : ms;
}
