// SPDX-License-Identifier: MIT
#include "health.h"
#include "sdkconfig.h"
#include "health_reset_policy.h"
#ifdef CONFIG_WAKE_HEALTH_RESET
#include "esp_task_wdt.h"
#include "esp_attr.h"
#endif
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
static const char *recovery_mode="warn_only";
#ifdef CONFIG_WAKE_HEALTH_RESET
static RTC_NOINIT_ATTR struct { uint32_t magic, consecutive; } reset_guard;
static bool suppress_recovery;
#endif
const char *health_recovery_mode(void)
{ portENTER_CRITICAL(&lock); const char *v=recovery_mode; portEXIT_CRITICAL(&lock); return v; }
#ifdef CONFIG_WAKE_HEALTH_FAULT_TEST
static bool fault_started;
static const char *fault_state="armed";
#endif
const char *health_fault_state(void)
{
#ifdef CONFIG_WAKE_HEALTH_FAULT_TEST
    portENTER_CRITICAL(&lock); const char *value=fault_state;
    portEXIT_CRITICAL(&lock); return value;
#else
    return "off";
#endif
}
static const char *names[] = {"usb", "button", "cloud"};

void health_init(void)
{
    snprintf(boot_id, sizeof(boot_id), "%08lx%08lx",
             (unsigned long)esp_random(), (unsigned long)esp_random());
    reset_reason = esp_reset_reason();
#ifdef CONFIG_WAKE_HEALTH_RESET
    if (reset_guard.magic!=0x57444f47 || reset_reason!=ESP_RST_TASK_WDT) {
        reset_guard.magic=0x57444f47; reset_guard.consecutive=0;
    } else if (reset_guard.consecutive<2) ++reset_guard.consecutive;
    suppress_recovery=reset_guard.consecutive>=2;
    recovery_mode=suppress_recovery ? "reset_suppressed" : "task_watchdog";
#endif
    ESP_LOGI("health", "boot=%s reset_reason=%d recovery=%s",boot_id,reset_reason,recovery_mode);
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
#ifdef CONFIG_WAKE_HEALTH_FAULT_TEST
    // Dedicated test image only: stop ONE worker's heartbeat, still yield CPU.
    // No cloud command can enable this. Once per boot, automatically recovers.
    if (task==HEALTH_USB && !fault_started && now>=INT64_C(60000000)) {
        fault_started=true;
        portENTER_CRITICAL(&lock); fault_state="usb_paused";
        portEXIT_CRITICAL(&lock);
        ESP_LOGW("health", "FAULT TEST: USB worker paused for 60s; keep Mac awake");
        for (unsigned i=0;i<60;++i) vTaskDelay(pdMS_TO_TICKS(1000));
        portENTER_CRITICAL(&lock); fault_state="recovered";
        portEXIT_CRITICAL(&lock);
        now=esp_timer_get_time();
    }
#endif
    portENTER_CRITICAL(&lock);
    obs[task] = (observation_t){.enabled=true, .at=now,
        .budget=(int64_t)warn_ms * 1000, .phase=phase};
    warning_mask &= ~(1U << task);
    portEXIT_CRITICAL(&lock);
}
static void supervisor(void *arg)
{
    (void)arg;
#ifdef CONFIG_WAKE_HEALTH_RESET
    const int64_t started=esp_timer_get_time();
    int64_t healthy_since=started;
    esp_task_wdt_user_handle_t user=NULL;
    bool feed_error=false;
    if (!suppress_recovery) {
        // IDF already initialized TWDT; preserve its 60s and idle subscriptions.
        esp_err_t err=esp_task_wdt_add_user("business_health", &user);
        if (err!=ESP_OK) { user=NULL; portENTER_CRITICAL(&lock); recovery_mode="subscription_failed"; portEXIT_CRITICAL(&lock); }
    }
#endif
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
#ifdef CONFIG_WAKE_HEALTH_RESET
        portENTER_CRITICAL(&lock);
        observation_t usb=obs[HEALTH_USB], button=obs[HEALTH_BUTTON];
        portEXIT_CRITICAL(&lock);
        bool fresh=health_required_fresh(esp_timer_get_time(),started,
            usb.enabled,usb.at,button.enabled,button.at);
        // Cloud connectivity and legal USB suspend never participate in this gate.
        if (user && fresh) {
            esp_err_t err=esp_task_wdt_reset_user(user);
            if (err!=ESP_OK && !feed_error) {
                feed_error=true;
                portENTER_CRITICAL(&lock); recovery_mode="feed_failed";
                portEXIT_CRITICAL(&lock);
                ESP_LOGW("health", "TWDT user feed failed: %d", (int)err);
            }
        }
        if (!fresh) healthy_since=esp_timer_get_time();
        if (fresh && esp_timer_get_time()-healthy_since>INT64_C(600000000))
            reset_guard.consecutive=0;
#endif
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
void health_start_supervisor(void)
{
    // Supervisor feeds only its own optional TWDT user after checking required workers.
    BaseType_t ok = xTaskCreate(supervisor, "health", 3072, NULL, 2, NULL);
    configASSERT(ok == pdPASS);
}
