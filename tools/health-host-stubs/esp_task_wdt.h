#pragma once
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
typedef void *esp_task_wdt_user_handle_t;
esp_err_t esp_task_wdt_add_user(const char *,esp_task_wdt_user_handle_t *);
esp_err_t esp_task_wdt_reset_user(esp_task_wdt_user_handle_t);
