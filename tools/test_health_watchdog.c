// SPDX-License-Identifier: MIT
#include <assert.h>
#include <setjmp.h>
#include <string.h>
#include <stdio.h>
#include "health.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
static int64_t now;
static unsigned feeds;
static void (*worker)(void *);
static jmp_buf done;
int64_t esp_timer_get_time(void) { return now; }
uint32_t esp_random(void) { return 123; }
int esp_reset_reason(void) { return 1; }
void test_health_log(int warning,const char *tag,const char *fmt,...)
{ (void)warning; (void)tag; (void)fmt; }
BaseType_t xTaskCreate(void (*fn)(void *),const char *n,uint32_t s,void *a,unsigned p,void *h)
{ (void)n;(void)s;(void)a;(void)p;(void)h;worker=fn;return pdPASS; }
esp_err_t esp_task_wdt_add_user(const char *name,esp_task_wdt_user_handle_t *h)
{ assert(!strcmp(name,"business_health"));*h=(void *)1;return ESP_OK; }
esp_err_t esp_task_wdt_reset_user(esp_task_wdt_user_handle_t h)
{ assert(h==(void *)1);++feeds;return now==140000000 ? -1 : ESP_OK; }
void vTaskDelay(uint32_t ticks) {
 now+=(int64_t)ticks*1000; unsigned sec=now/1000000;
 health_phase(HEALTH_BUTTON,"poll",5000);
 if(sec<=10 || sec>=135) health_phase(HEALTH_USB,"idle",20000);
 // Cloud never updates: its timeout must not stop business feeding.
 if(sec==69) assert(feeds==69);
 if(sec==72 || sec==130) assert(feeds==70);
 if(sec==136) assert(feeds==71);
 if(sec==142) {assert(!strcmp(health_recovery_mode(),"feed_failed"));longjmp(done,1);}
}
int main(void) {
 health_init(); assert(!strcmp(health_recovery_mode(),"task_watchdog"));
 health_phase(HEALTH_USB,"idle",20000);health_phase(HEALTH_BUTTON,"poll",5000);
 health_phase(HEALTH_CLOUD,"offline",45000);health_start_supervisor();
 if(setjmp(done)==0)worker(NULL);
 puts("Actual supervisor TWDT integration: feed gate, cloud exclusion, stalled worker, recovery and feed failure passed.");
}
