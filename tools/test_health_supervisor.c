// SPDX-License-Identifier: MIT
// Executes the actual health.c with a deterministic clock, not a copied policy.
#include <assert.h>
#include <stdarg.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "health.h"
#include "freertos/task.h"
static int64_t now_us;
static unsigned warnings;
static void (*worker)(void *);
static jmp_buf done;
int64_t esp_timer_get_time(void) { return now_us; }
uint32_t esp_random(void) { static uint32_t seed=100; return ++seed; }
int esp_reset_reason(void) { return 1; }
void test_health_log(int warning,const char *tag,const char *fmt,...)
{
    (void)tag; (void)fmt;
    if (warning) ++warnings;
}
BaseType_t xTaskCreate(void (*fn)(void *),const char *name,uint32_t stack,
                      void *arg,unsigned priority,void *handle)
{
    (void)name; (void)stack; (void)arg; (void)priority; (void)handle;
    worker=fn; return pdPASS;
}
void vTaskDelay(uint32_t ticks)
{
    now_us+=(int64_t)ticks*1000;
    unsigned sec=(unsigned)(now_us/1000000);
    // Scripted task scheduling; no Wi-Fi or USB events required for idle health.
    health_phase(HEALTH_BUTTON,"poll",5000);
    if (sec<=10 || sec>=40) health_phase(HEALTH_USB,"idle",20000);
    if (sec<45 && sec%10==0) health_phase(HEALTH_CLOUD,"waiting_wifi",45000);
    if (sec==45) health_phase(HEALTH_CLOUD,"authenticate",120000);
    if (sec>=168 && sec%2==0) health_phase(HEALTH_CLOUD,"waiting_wifi",45000);
    if (sec==10) assert(health_warnings()==0 && warnings==0);
    if (sec==31) assert(health_warnings()==0); // exactly at budget, no premature alert
    if (sec==32) assert(health_warnings()==1 && warnings==1);
    if (sec==39) assert(warnings==1); // do not log the same incident each tick
    if (sec==41) assert(health_warnings()==0 && warnings==1);
    if (sec==165) assert(health_warnings()==0);
    if (sec==167) assert(health_warnings()==4 && warnings==2);
    if (sec==200) {
        assert(health_warnings()==0 && warnings==2);
        longjmp(done,1);
    }
}
int main(void)
{
    health_init(); assert(health_reset_reason()==1);
    assert(strlen(health_boot_id())==16 && !strcmp(health_fault_state(),"off"));
    health_phase(HEALTH_USB,"idle",20000);
    health_phase(HEALTH_BUTTON,"poll",5000);
    health_phase(HEALTH_CLOUD,"waiting_wifi",45000);
    health_start_supervisor(); assert(worker);
    if (setjmp(done)==0) worker(NULL);
    puts("Actual health.c: idle, stalled USB, single alert, recovery, long network call and offline waiting passed.");
}
