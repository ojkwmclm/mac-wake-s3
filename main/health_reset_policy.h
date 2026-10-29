// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
// USB worker has bounded waits below 10s; button polls every 20ms.
// This conservative 60s guard covers task scheduling stalls, not network health.
static inline bool health_required_fresh(int64_t now, int64_t start,
    bool usb_seen, int64_t usb_at, bool button_seen, int64_t button_at)
{
    if (now-start<INT64_C(60000000)) return true;
    return usb_seen && button_seen && now-usb_at<INT64_C(60000000)
           && now-button_at<INT64_C(60000000);
}
