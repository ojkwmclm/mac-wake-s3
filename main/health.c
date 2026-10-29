// SPDX-License-Identifier: MIT
#include "health.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_log.h"

typedef struct {
    bool enabled, warned;
    int64_t at, budget;
    const char *phase;
} observation_t;
static observation_t obs[HEALTH_COUNT];
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t warning_mask;
static char boot_id[17];
static int reset_reason;
static const char *names[] = {"usb", "button", "cloud"};

void health_init(void)
{
    snprintf(boot_id, sizeof(boot_id), "%08lx%08lx",
             (unsigned long)esp_random(), (unsigned long)esp_random());
    reset_reason = esp_reset_reason();
    ESP_LOGI("health", "boot=%s reset_reason=%d; business supervisor is WARN ONLY", boot_id, reset_reason);
}
const char *health_boot_id(void) { return boot_id; }
int health_reset_reason(void) { return reset_reason; }
uint32_t health_warnings(void)
{
    portENTER_CRITICAL(&lock); uint32_t value = warning_mask;
    portEXIT_CRITICAL(&lock); return value;
}
void health_phase(health_task_t task, const char *phase, uint32_t warn_ms)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&lock);
    obs[task] = (observation_t){.enabled=true, .at=now,
        .budget=(int64_t)warn_ms * 1000, .phase=phase};
    warning_mask &= ~(1U << task);
    portEXIT_CRITICAL(&lock);
}
static void supervisor(void *arg)
{
    (void)arg;
    while (true) {
        for (unsigned i=0; i<HEALTH_COUNT; ++i) {
            int64_t now = esp_timer_get_time();
            portENTER_CRITICAL(&lock);
            observation_t copy = obs[i];
            bool stale = copy.enabled && now - copy.at > copy.budget;
            if (stale) { obs[i].warned=true; warning_mask |= 1U << i; }
            portEXIT_CRITICAL(&lock);
            if (stale && !copy.warned)
                ESP_LOGW("health", "%s phase=%s unchanged %lld ms (observation only; no reset)",
                         names[i], copy.phase, (long long)((now-copy.at)/1000));
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
void health_start_supervisor(void)
{
    // No periodic feed of business tasks and no new TWDT subscription in phase 1.
    BaseType_t ok = xTaskCreate(supervisor, "health", 3072, NULL, 2, NULL);
    configASSERT(ok == pdPASS);
}
