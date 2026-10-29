#pragma once
void test_health_log(int warning,const char *tag,const char *fmt,...);
#define ESP_LOGI(tag,...) test_health_log(0,tag,__VA_ARGS__)
#define ESP_LOGW(tag,...) test_health_log(1,tag,__VA_ARGS__)
