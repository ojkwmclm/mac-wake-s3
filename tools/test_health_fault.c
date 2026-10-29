// SPDX-License-Identifier: MIT
// Exercises the actual compile-time fault hook and supervisor using a fake clock.
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "health.h"
#include "freertos/task.h"
static int64_t now_us;
static unsigned pauses, warnings;
static bool in_supervisor, observed_stale;
static void (*worker)(void *);
static jmp_buf supervisor_step;
int64_t esp_timer_get_time(void) { return now_us; }
uint32_t esp_random(void) { return 123; }
int esp_reset_reason(void) { return 1; }
void test_health_log(int warning,const char *tag,const char *fmt,...)
{
    (void)tag;
    if (warning && strstr(fmt,"unchanged")) ++warnings;
}
BaseType_t xTaskCreate(void (*fn)(void *),const char *name,uint32_t stack,
                      void *arg,unsigned priority,void *handle)
{
    (void)name; (void)stack; (void)arg; (void)priority; (void)handle;
    worker=fn; return pdPASS;
}
void vTaskDelay(uint32_t ticks)
{
    if (in_supervisor) longjmp(supervisor_step,1);
    assert(ticks==1000 && !strcmp(health_fault_state(),"usb_paused"));
    ++pauses; now_us+=(int64_t)ticks*1000;
    health_phase(HEALTH_BUTTON,"poll",5000);
    health_phase(HEALTH_CLOUD,"poll",45000);
    in_supervisor=true;
    if (setjmp(supervisor_step)==0) worker(NULL);
    in_supervisor=false;
    if (health_warnings()==1) observed_stale=true;
    assert((health_warnings() & ~1U)==0);
}
int main(void)
{
    health_init(); health_start_supervisor(); assert(worker);
    char boot[17]; strcpy(boot,health_boot_id());
    assert(!strcmp(health_fault_state(),"armed"));
    now_us=59000000; health_phase(HEALTH_USB,"idle",20000);
    now_us=60000000; health_phase(HEALTH_USB,"idle",20000);
    assert(pauses==60 && now_us==120000000 && warnings==1 && observed_stale);
    assert(!strcmp(health_fault_state(),"recovered") && health_warnings()==0);
    assert(!strcmp(boot,health_boot_id()));
    now_us=121000000; health_phase(HEALTH_USB,"idle",20000);
    assert(pauses==60); // Once per boot, no repeated fault injection.
    puts("Actual fault hook: one 60s pause, USB-only alert, continued cloud/button health, automatic recovery, same boot ID passed.");
}
