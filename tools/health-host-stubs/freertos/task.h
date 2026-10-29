#pragma once
#include "FreeRTOS.h"
void vTaskDelay(uint32_t ticks);
BaseType_t xTaskCreate(void (*fn)(void *),const char *name,uint32_t stack,void *arg,unsigned priority,void *handle);
