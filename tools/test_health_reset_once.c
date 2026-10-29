// SPDX-License-Identifier: MIT
#include <assert.h>
#include <setjmp.h>
#include <string.h>
#include <stdio.h>
#include "health.h"
#include "freertos/task.h"
#include "nvs.h"
static int mode, staged, committed, delays;
static int64_t now;
static jmp_buf reset;
int64_t esp_timer_get_time(void){return now;}
uint32_t esp_random(void){return 123;}
int esp_reset_reason(void){return 1;}
void test_health_log(int w,const char *t,const char *f,...){(void)w;(void)t;(void)f;}
BaseType_t xTaskCreate(void (*f)(void *),const char *n,uint32_t s,void *a,unsigned p,void *h)
{(void)f;(void)n;(void)s;(void)a;(void)p;(void)h;return pdPASS;}
esp_err_t esp_task_wdt_add_user(const char *n,esp_task_wdt_user_handle_t *h){(void)n;*h=(void *)1;return ESP_OK;}
esp_err_t esp_task_wdt_reset_user(esp_task_wdt_user_handle_t h){(void)h;return ESP_OK;}
esp_err_t nvs_open(const char *n,int m,nvs_handle_t *h){assert(!strcmp(n,"health_test"));(void)m;*h=1;return mode==2 ? ESP_FAIL : ESP_OK;}
esp_err_t nvs_get_u8(nvs_handle_t h,const char *k,uint8_t *v){(void)h;assert(!strcmp(k,"r052_01"));*v=mode==1;return mode==1 ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;}
esp_err_t nvs_set_u8(nvs_handle_t h,const char *k,uint8_t v){(void)h;(void)k;assert(v==1);staged=1;return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h){(void)h;assert(staged);if(mode==3)return ESP_FAIL;committed=1;return ESP_OK;}
void nvs_close(nvs_handle_t h){(void)h;}
void vTaskDelay(uint32_t ticks){assert(committed && !strcmp(health_fault_state(),"usb_paused"));now+=(int64_t)ticks*1000;if(++delays==60)longjmp(reset,1);}
int main(int argc,char **argv){assert(argc==2);mode=argv[1][0]-'0';health_init();
 now=59000000;health_phase(HEALTH_USB,"idle",20000);now=60000000;
 if(setjmp(reset)==0)health_phase(HEALTH_USB,"idle",20000);
 if(mode==0)assert(committed && delays==60);
 if(mode==1)assert(!delays && !strcmp(health_fault_state(),"consumed"));
 if(mode==2 || mode==3)assert(!delays && !strcmp(health_fault_state(),"test_storage_failed"));
 puts("One-shot reset test scenario passed (simulated reset/storage, not hardware TWDT).");}
