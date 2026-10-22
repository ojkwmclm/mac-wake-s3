// SPDX-License-Identifier: MIT
#include "cloud_command.h"
#include <ctype.h>
#include <string.h>
#include "cJSON.h"

cloud_command_t cloud_parse_command(const char *payload, size_t len,
                                   const char *owner, bool retained, bool duplicate)
{
    if (!payload || !owner || !owner[0] || !len || len > 1024 || retained || duplicate)
        return CLOUD_IGNORE;
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(payload, len, &end, false);
    if (!root) return CLOUD_IGNORE;
    while (end < payload + len && isspace((unsigned char)*end)) ++end;
    cloud_command_t action = CLOUD_IGNORE;
    if (end != payload + len || !cJSON_IsObject(root)) goto out;
    const cJSON *sender = cJSON_GetObjectItemCaseSensitive(root, "fromDevice");
    if (!cJSON_IsString(sender) || strcmp(sender->valuestring, owner)) goto out;
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    cJSON *decoded = NULL;
    if (cJSON_IsString(data)) {
        decoded = cJSON_ParseWithOpts(data->valuestring, NULL, true); data = decoded;
    }
    if (cJSON_IsObject(data)) {
        const cJSON *get = cJSON_GetObjectItemCaseSensitive(data, "get");
        const cJSON *button = cJSON_GetObjectItemCaseSensitive(data, "btn-wakeup");
        const cJSON *sleep = cJSON_GetObjectItemCaseSensitive(data, "btn-sleep");
        // Reject mixed control messages rather than guessing which action was intended.
        if (button && sleep) action = CLOUD_IGNORE;
        else if (cJSON_IsString(sleep) &&
                 (!strcmp(sleep->valuestring, "tap") || !strcmp(sleep->valuestring, "press")))
            action = CLOUD_SLEEP;
        else if (cJSON_IsString(get) && !strcmp(get->valuestring, "state"))
            action = CLOUD_GET_STATE;
        else if (cJSON_IsString(button) &&
                 (!strcmp(button->valuestring, "tap") || !strcmp(button->valuestring, "on") ||
                  !strcmp(button->valuestring, "press")))
            action = CLOUD_WAKE;
    }
    cJSON_Delete(decoded);
out:
    cJSON_Delete(root);
    return action;
}
