#pragma once
#include <stdbool.h>
#include <stddef.h>
typedef enum { CLOUD_IGNORE, CLOUD_GET_STATE, CLOUD_WAKE, CLOUD_SLEEP } cloud_command_t;
cloud_command_t cloud_parse_command(const char *payload, size_t len,
                                   const char *owner, bool retained, bool duplicate);
