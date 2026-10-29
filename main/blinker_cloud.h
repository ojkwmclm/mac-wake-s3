#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    uint32_t request_id;
    bool busy;
    uint8_t release_mask;
    uint32_t usb_epoch;
    bool usb_mounted, usb_suspended;
    bool release_timeout, inflight_timeout;
    const char *result;
} cloud_wake_status_t;
typedef bool (*cloud_wake_fn_t)(uint32_t *id);
typedef void (*cloud_status_fn_t)(cloud_wake_status_t *status);
void blinker_cloud_start(cloud_wake_fn_t wake_fn, cloud_wake_fn_t sleep_fn, cloud_status_fn_t status_fn);
const char *blinker_cloud_state(void);
