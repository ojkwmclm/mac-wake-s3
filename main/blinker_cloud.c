// SPDX-License-Identifier: MIT
#include "blinker_cloud.h"
#include "cloud_command.h"
#include "health.h"
#include "recovery_policy.h"
#include "esp_app_desc.h"
#include "esp_wifi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "cJSON.h"

static const char *TAG = "blinker_cloud";
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static const char *state = "disabled";
static bool online, refresh_auth, report_requested;
static cloud_wake_fn_t wake_fn, sleep_fn;
static cloud_status_fn_t status_fn;
static esp_mqtt_client_handle_t client;
static char device[128], username[128], password[256], owner[128];
static char incoming[160], outgoing[160], will[512], uri[128];
static char rx[1025];
static int rx_used, rx_total;
static bool rx_valid;
static uint32_t report_seq;

static void set_state(const char *value)
{
    portENTER_CRITICAL(&lock);
    bool changed = strcmp(state, value) != 0;
    state = value; portEXIT_CRITICAL(&lock);
    if (!changed) return;
    ESP_LOGI(TAG, "state=%s", value);
}
const char *blinker_cloud_state(void)
{
    portENTER_CRITICAL(&lock); const char *value = state; portEXIT_CRITICAL(&lock);
    return value;
}
static bool copy_field(const cJSON *obj, const char *name, char *dest, size_t cap)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, name);
    if (!cJSON_IsString(v) || !v->valuestring[0] || strlen(v->valuestring) >= cap) return false;
    strcpy(dest, v->valuestring); return true;
}
static bool authenticate(void)
{
    // Only an existing device Key is sent to the official point-light auth endpoint.
    const char *key = CONFIG_WAKE_BLINKER_KEY;
    for (const char *p = key; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '-' || *p == '_')) return false;
    char url[512];
    if (snprintf(url, sizeof(url),
        "https://iot.diandeng.tech/api/v1/user/device/diy/auth?authKey=%s&protocol=wss", key) >= sizeof(url)) return false;
    const esp_http_client_config_t cfg = {
        .url=url, .crt_bundle_attach=esp_crt_bundle_attach,
        .timeout_ms=15000, .buffer_size=2048, .disable_auto_redirect=true,
    };
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) return false;
    char *body = calloc(1, 8192);
    bool ok = false;
    if (!body) goto cleanup;
    if (esp_http_client_open(h, 0) != ESP_OK) goto cleanup;
    int64_t length = esp_http_client_fetch_headers(h);
    if (length >= 8192 || esp_http_client_get_status_code(h) != 200) goto cleanup;
    int n = esp_http_client_read_response(h, body, 8191);
    if (n <= 0 || n >= 8191) goto cleanup;
    cJSON *root = cJSON_ParseWithLength(body, n);
    if (!root) goto cleanup;
    const cJSON *message = cJSON_GetObjectItemCaseSensitive(root, "message");
    const cJSON *d = cJSON_GetObjectItemCaseSensitive(root, "detail");
    const cJSON *host = cJSON_GetObjectItemCaseSensitive(d, "host");
    const cJSON *broker = cJSON_GetObjectItemCaseSensitive(d, "broker");
    const cJSON *port_value = cJSON_GetObjectItemCaseSensitive(d, "port");
    int port = cJSON_IsNumber(port_value) ? port_value->valueint :
               cJSON_IsString(port_value) ? atoi(port_value->valuestring) : 0;
    if (cJSON_IsNumber(message) && message->valueint == 1000 &&
        cJSON_IsString(host) && !strcmp(host->valuestring, "wss://broker.diandeng.tech") &&
        cJSON_IsString(broker) && !strcmp(broker->valuestring, "blinker") &&
        port > 0 && port < 65536 &&
        copy_field(d, "deviceName", device, sizeof(device)) &&
        copy_field(d, "iotId", username, sizeof(username)) &&
        copy_field(d, "iotToken", password, sizeof(password)) &&
        copy_field(d, "uuid", owner, sizeof(owner))) {
        snprintf(uri, sizeof(uri), "wss://broker.diandeng.tech:%d/mqtt", port);
        snprintf(incoming, sizeof(incoming), "/device/%s/r", device);
        snprintf(outgoing, sizeof(outgoing), "/device/%s/s", device);
        snprintf(will, sizeof(will),
            "{\"fromDevice\":\"%s\",\"toDevice\":\"%s\",\"deviceType\":\"OwnApp\",\"data\":{\"state\":\"offline\"}}", device, owner);
        ok = true;
    }
    cJSON_Delete(root);
cleanup:
    if (body) { memset(body, 0, 8192); free(body); }
    esp_http_client_close(h); esp_http_client_cleanup(h);
    return ok;
}
static void receive_data(esp_mqtt_event_handle_t e)
{
    if (e->current_data_offset == 0) {
        rx_used = 0; rx_total = e->total_data_len;
        rx_valid = !e->retain && !e->dup && rx_total > 0 && rx_total <= 1024 &&
            e->topic_len == strlen(incoming) && e->topic &&
            !memcmp(e->topic, incoming, e->topic_len);
    }
    if (!rx_valid || e->data_len < 0 || e->current_data_offset != rx_used ||
        rx_used + e->data_len > rx_total) { rx_valid = false; return; }
    memcpy(rx + rx_used, e->data, e->data_len); rx_used += e->data_len;
    if (rx_used != rx_total) return;
    rx[rx_used] = 0;
    cloud_command_t action = cloud_parse_command(rx, rx_used, owner, false, false);
    rx_valid = false;
    if (action == CLOUD_WAKE || action == CLOUD_SLEEP) {
        uint32_t id;
        bool accepted = (action == CLOUD_SLEEP ? sleep_fn : wake_fn)(&id);
        ESP_LOGI(TAG, "App %s request %s", action == CLOUD_SLEEP ? "sleep" : "wake", accepted ? "accepted" : "rate limited");
    }
    if (action != CLOUD_IGNORE) {
        portENTER_CRITICAL(&lock); report_requested = true; portEXIT_CRITICAL(&lock);
    }
}
static void mqtt_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base;
    esp_mqtt_event_handle_t e = data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        set_state("subscribing");
        esp_mqtt_client_subscribe(e->client, incoming, 0); break;
    case MQTT_EVENT_SUBSCRIBED:
        portENTER_CRITICAL(&lock); online = true; report_requested = true; portEXIT_CRITICAL(&lock);
        set_state("online"); break;
    case MQTT_EVENT_DISCONNECTED:
        portENTER_CRITICAL(&lock); online = false; portEXIT_CRITICAL(&lock);
        rx_valid = false; set_state("reconnecting"); break;
    case MQTT_EVENT_DATA: receive_data(e); break;
    case MQTT_EVENT_ERROR:
        if (e->error_handle && e->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED &&
            (e->error_handle->connect_return_code == MQTT_CONNECTION_REFUSE_BAD_USERNAME ||
             e->error_handle->connect_return_code == MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED)) {
            portENTER_CRITICAL(&lock); refresh_auth = true; portEXIT_CRITICAL(&lock);
        }
        ESP_LOGW(TAG, "Cloud connection error (credentials and message payload omitted)"); break;
    default: break;
    }
}
static bool publish_state(const cloud_wake_status_t *s)
{
    cJSON *root = cJSON_CreateObject(), *d = cJSON_CreateObject();
    if (!root || !d) { cJSON_Delete(root); cJSON_Delete(d); return false; }
    cJSON_AddStringToObject(root, "fromDevice", device);
    cJSON_AddStringToObject(root, "toDevice", owner);
    cJSON_AddStringToObject(root, "deviceType", "OwnApp");
    cJSON_AddStringToObject(d, "state", "online");
    cJSON_AddStringToObject(d, "result", s->result);
    cJSON_AddNumberToObject(d, "request_id", s->request_id);
    cJSON_AddNullToObject(d, "mac_online");
    cJSON *diag=cJSON_AddObjectToObject(d, "health");
    if (!diag) { cJSON_Delete(root); cJSON_Delete(d); return false; }
    cJSON_AddStringToObject(diag, "boot_id", health_boot_id());
    cJSON_AddNumberToObject(diag, "seq", ++report_seq);
    cJSON_AddStringToObject(diag, "firmware", esp_app_get_description()->version);
    cJSON_AddNumberToObject(diag, "uptime_s", esp_timer_get_time()/1000000);
    cJSON_AddNumberToObject(diag, "reset_reason", health_reset_reason());
    cJSON_AddStringToObject(diag, "supervisor", "warn_only");
    cJSON_AddStringToObject(diag, "fault_test", health_fault_state());
    cJSON_AddNumberToObject(diag, "stale_tasks", health_warnings());
    cJSON_AddNumberToObject(diag, "release_mask", s->release_mask);
    cJSON_AddBoolToObject(diag, "release_timeout", s->release_timeout);
    cJSON_AddBoolToObject(diag, "inflight_timeout", s->inflight_timeout);
    cJSON_AddNumberToObject(diag, "usb_epoch", s->usb_epoch);
    cJSON_AddBoolToObject(diag, "usb_mounted", s->usb_mounted);
    cJSON_AddBoolToObject(diag, "usb_suspended", s->usb_suspended);
    cJSON_AddItemToObject(root, "data", d);
    char *payload = cJSON_PrintUnformatted(root);
    bool ok = payload && strlen(payload) <= 1024 &&
        esp_mqtt_client_publish(client, outgoing, payload, 0, 0, 0) >= 0;
    free(payload); cJSON_Delete(root); return ok;
}
static bool have_ip(void)
{
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return false;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;
    return netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0;
}
static void ensure_dns(void)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_dns_info_t dns;
    if (!netif || esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns) != ESP_OK) return;
    if (dns.ip.type != ESP_IPADDR_TYPE_V4 || dns.ip.u_addr.ip4.addr != 0) return;
    // Some DHCP servers assign an IP but omit DNS. Preserve any supplied resolver.
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    ESP_ERROR_CHECK(esp_netif_str_to_ip4("223.5.5.5", &dns.ip.u_addr.ip4));
    ESP_ERROR_CHECK(esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns));
    ESP_ERROR_CHECK(esp_netif_str_to_ip4("1.1.1.1", &dns.ip.u_addr.ip4));
    ESP_ERROR_CHECK(esp_netif_set_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns));
    ESP_LOGI(TAG, "DHCP omitted DNS; fallback resolvers configured");
}
static void cloud_task(void *arg)
{
    (void)arg;
    bool clock_started = false;
    uint32_t last_id = 0; const char *last_result = "";
    int64_t last_publish = 0, next_publish = 0;
    unsigned publish_failures=0;
    while (true) {
        health_phase(HEALTH_CLOUD, "poll_or_retry_wait", 45000);
        if (!have_ip()) { set_state("waiting_wifi"); vTaskDelay(pdMS_TO_TICKS(2000)); continue; }
        ensure_dns();
        if (!clock_started) {
            esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, "ntp.aliyun.com");
            esp_sntp_setservername(1, "pool.ntp.org");
            esp_sntp_init(); clock_started = true;
        }
        if (time(NULL) < 1704067200) { set_state("waiting_clock"); vTaskDelay(pdMS_TO_TICKS(2000)); continue; }
        portENTER_CRITICAL(&lock); bool refresh = refresh_auth; refresh_auth = false; portEXIT_CRITICAL(&lock);
        if (client && refresh) {
            health_phase(HEALTH_CLOUD, "stop_for_auth_refresh", 120000);
            esp_mqtt_client_stop(client); esp_mqtt_client_destroy(client); client = NULL;
            memset(password, 0, sizeof(password));
            set_state("refreshing_auth"); vTaskDelay(pdMS_TO_TICKS(30000));
        }
        if (!client) {
            set_state("authenticating");
            health_phase(HEALTH_CLOUD, "authenticate_unbounded_total", 120000);
            if (!authenticate()) { set_state("auth_failed"); vTaskDelay(pdMS_TO_TICKS(30000)); continue; }
            const esp_mqtt_client_config_t cfg = {
                .broker.address.uri=uri,
                .broker.verification.crt_bundle_attach=esp_crt_bundle_attach,
                .credentials.client_id=device, .credentials.username=username,
                .credentials.authentication.password=password,
                .session.keepalive=30, .session.disable_clean_session=false,
                .session.last_will={.topic=outgoing, .msg=will, .qos=0, .retain=0},
                .network.reconnect_timeout_ms=5000, .network.timeout_ms=10000,
                .buffer.size=2048, .task.stack_size=6144,
            };
            client = esp_mqtt_client_init(&cfg);
            if (!client) { set_state("init_failed"); vTaskDelay(pdMS_TO_TICKS(10000)); continue; }
            esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event, NULL);
            set_state("connecting");
            if (esp_mqtt_client_start(client) != ESP_OK) {
                esp_mqtt_client_destroy(client); client = NULL;
                set_state("connect_failed"); vTaskDelay(pdMS_TO_TICKS(10000)); continue;
            }
        }
        portENTER_CRITICAL(&lock); bool is_online = online, requested = report_requested;
        report_requested = false; portEXIT_CRITICAL(&lock);
        cloud_wake_status_t s; status_fn(&s);
        int64_t now = esp_timer_get_time();
        bool changed = last_id != s.request_id || strcmp(last_result, s.result);
        if (is_online && now >= next_publish &&
            (requested || changed || now - last_publish >= 25000000)) {
            health_phase(HEALTH_CLOUD, "mqtt_publish", 120000);
            bool sent=publish_state(&s);
            int64_t finished=esp_timer_get_time();
            if (sent) {
                last_publish=finished; last_id=s.request_id; last_result=s.result;
                publish_failures=0; next_publish=finished+1500000;
            } else {
                if (publish_failures<6) ++publish_failures;
                next_publish=finished+(int64_t)publish_retry_ms(publish_failures)*1000;
                portENTER_CRITICAL(&lock); report_requested=true; portEXIT_CRITICAL(&lock);
            }
        } else if (requested) {
            portENTER_CRITICAL(&lock); report_requested = true; portEXIT_CRITICAL(&lock);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
void blinker_cloud_start(cloud_wake_fn_t request, cloud_wake_fn_t sleep_request, cloud_status_fn_t snapshot)
{
    if (!CONFIG_WAKE_BLINKER_KEY[0]) return;
    wake_fn = request; sleep_fn = sleep_request; status_fn = snapshot;
    ESP_ERROR_CHECK(xTaskCreate(cloud_task, "blinker_cloud", 12288, NULL, 4, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
