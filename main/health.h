// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum { HEALTH_USB, HEALTH_BUTTON, HEALTH_CLOUD, HEALTH_COUNT } health_task_t;
void health_init(void);
void health_phase(health_task_t task, const char *phase, uint32_t warn_ms);
void health_start_supervisor(void);
uint32_t health_warnings(void);
const char *health_boot_id(void);
int health_reset_reason(void);
const char *health_fault_state(void);
