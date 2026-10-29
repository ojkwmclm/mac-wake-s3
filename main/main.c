// SPDX-License-Identifier: MIT
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "tinyusb.h"
#include "class/hid/hid_device.h"
#include "blinker_cloud.h"

static const char *TAG = "mac_wake";
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
typedef struct { uint32_t id; bool sleep; } control_request_t;
static QueueHandle_t wake_queue;
static bool sleep_keyboard_release, sleep_consumer_release;
static bool mounted, suspended, wake_allowed, busy;
static bool release_pending;
static uint32_t request_id;
static int64_t last_request_us;
static const char *result = "idle";
static esp_timer_handle_t reconnect_timer;
static char serial_number[13];

// Development-only VID/PID; allocate appropriate USB identity before distribution.
static const tusb_desc_device_t device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200, .bDeviceClass = 0, .bDeviceSubClass = 0,
    .bDeviceProtocol = 0, .bMaxPacketSize0 = 64,
    .idVendor = 0x303a, .idProduct = 0x4004, .bcdDevice = 0x0104,
    .iManufacturer = 1, .iProduct = 2, .iSerialNumber = 3,
    .bNumConfigurations = 1,
};
static const uint8_t report_descriptor[] = { TUD_HID_REPORT_DESC_KEYBOARD() };
static const uint8_t consumer_descriptor[] = { TUD_HID_REPORT_DESC_CONSUMER() };
static const uint8_t configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 2, 0, TUD_CONFIG_DESC_LEN + 2 * TUD_HID_DESC_LEN,
                         TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500),
    TUD_HID_DESCRIPTOR(0, 4, HID_ITF_PROTOCOL_KEYBOARD,
                      sizeof(report_descriptor), 0x81, 8, 10),
    TUD_HID_DESCRIPTOR(1, 4, HID_ITF_PROTOCOL_NONE,
                      sizeof(consumer_descriptor), 0x82, 2, 10),
};
static const char *strings[] = {
    (const char[]){0x09, 0x04}, "Local Prototype", "Mac Wake S3",
    serial_number, "Wake Keyboard",
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{ return instance == 1 ? consumer_descriptor : report_descriptor; }
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t id,
    hid_report_type_t type, uint8_t *buffer, uint16_t len)
{
    (void)id;
    uint16_t size = instance == 1 ? 2 : 8;
    if (type == HID_REPORT_TYPE_INPUT && len >= size) {
        memset(buffer, 0, size); return size;
    }
    return 0;
}
void tud_hid_set_report_cb(uint8_t instance, uint8_t id,
    hid_report_type_t type, uint8_t const *buffer, uint16_t len)
{ (void)instance; (void)id; (void)type; (void)buffer; (void)len; }

void tud_mount_cb(void)
{
    portENTER_CRITICAL(&state_lock);
    mounted = true; suspended = false; wake_allowed = false;
    portEXIT_CRITICAL(&state_lock);
    ESP_LOGI(TAG, "USB mounted");
}
void tud_umount_cb(void)
{
    portENTER_CRITICAL(&state_lock);
    mounted = false; suspended = false; wake_allowed = false;
    portEXIT_CRITICAL(&state_lock);
    ESP_LOGI(TAG, "USB unmounted");
}
void tud_suspend_cb(bool remote_wakeup_en)
{
    portENTER_CRITICAL(&state_lock);
    suspended = true; wake_allowed = remote_wakeup_en;
    portEXIT_CRITICAL(&state_lock);
    ESP_LOGI(TAG, "USB suspended; remote wake permission=%d", remote_wakeup_en);
}
void tud_resume_cb(void)
{
    portENTER_CRITICAL(&state_lock);
    suspended = false; wake_allowed = false;
    portEXIT_CRITICAL(&state_lock);
    ESP_LOGI(TAG, "USB resumed (Mac reachability NOT verified)");
}
static void set_result(const char *value)
{
    portENTER_CRITICAL(&state_lock); result = value;
    portEXIT_CRITICAL(&state_lock);
    ESP_LOGI(TAG, "wake result=%s", value);
}

// One request at a time; reject duplicate retries instead of repeatedly waking Mac.
static bool enqueue_action(bool sleep, uint32_t *id)
{
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&state_lock);
    if (busy || (request_id && now - last_request_us < 3000000)) {
        portEXIT_CRITICAL(&state_lock); return false;
    }
    busy = true; last_request_us = now; *id = ++request_id;
    result = "queued";
    portEXIT_CRITICAL(&state_lock);
    control_request_t request = {.id=*id, .sleep=sleep};
    if (xQueueSend(wake_queue, &request, 0) != pdTRUE) {
        portENTER_CRITICAL(&state_lock); busy = false;
        portEXIT_CRITICAL(&state_lock); return false;
    }
    return true;
}

static bool enqueue_wake(uint32_t *id) { return enqueue_action(false, id); }
static bool enqueue_sleep(uint32_t *id) { return enqueue_action(true, id); }

// Release reports only; never repeat the sleep shortcut after a disconnect/suspend.
static void release_sleep_keys(void)
{
    if (!tud_mounted()) { sleep_keyboard_release = sleep_consumer_release = false; return; }
    if (tud_suspended()) return;
    uint16_t zero = 0;
    if (sleep_consumer_release && tud_hid_n_ready(1) && tud_hid_n_report(1, 0, &zero, sizeof(zero)))
        sleep_consumer_release = false;
    if (sleep_keyboard_release && tud_hid_ready() && tud_hid_keyboard_report(0, 0, NULL))
        sleep_keyboard_release = false;
}
static void send_sleep(void)
{
    if (tud_suspended()) { set_result("sleep_already_suspended"); return; }
    if (sleep_keyboard_release || sleep_consumer_release || !tud_hid_ready() || !tud_hid_n_ready(1)) {
        set_result("hid_not_ready"); return;
    }
    // Apple: Option-Command-Media Eject puts the Mac to sleep.
    if (!tud_hid_keyboard_report(0, KEYBOARD_MODIFIER_LEFTALT | KEYBOARD_MODIFIER_LEFTGUI, NULL)) {
        set_result("hid_not_ready"); return;
    }
    sleep_keyboard_release = true;
    vTaskDelay(pdMS_TO_TICKS(30));
    uint16_t eject = 0x00b8; // Consumer Usage: Eject (not Power).
    if (tud_mounted() && !tud_suspended() && tud_hid_n_report(1, 0, &eject, sizeof(eject))) {
        sleep_consumer_release = true;
        vTaskDelay(pdMS_TO_TICKS(30));
        set_result("sleep_shortcut_sent_unconfirmed");
    } else set_result("sleep_shortcut_failed");
    release_sleep_keys();
}

static void cloud_snapshot(cloud_wake_status_t *s)
{
    portENTER_CRITICAL(&state_lock);
    s->request_id = request_id; s->busy = busy; s->result = result;
    portEXIT_CRITICAL(&state_lock);
}

#ifdef CONFIG_WAKE_SEND_SHIFT
static bool wait_hid_ready(int timeout_ms)
{
    for (int i = 0; i < timeout_ms / 10; ++i) {
        if (!tud_mounted()) return false;
        if (tud_hid_ready()) return true;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return false;
}
static void send_shift(void)
{
    if (!wait_hid_ready(1000) ||
        !tud_hid_keyboard_report(0, KEYBOARD_MODIFIER_LEFTSHIFT, NULL)) {
        set_result("hid_not_ready"); return;
    }
    release_pending = true;
    vTaskDelay(pdMS_TO_TICKS(40));
    if (wait_hid_ready(1000) && tud_hid_keyboard_report(0, 0, NULL)) {
        release_pending = false; set_result("shift_sent_unconfirmed");
    } else {
        set_result("key_release_pending");
    }
}
#endif
static void wake_task(void *arg)
{
    (void)arg;
    control_request_t request;
    while (true) {
        release_sleep_keys();
        if (release_pending) {
            if (!tud_mounted()) {
                release_pending = false; set_result("usb_disconnected");
            } else if (tud_hid_ready() && tud_hid_keyboard_report(0, 0, NULL)) {
                release_pending = false; set_result("shift_sent_unconfirmed");
            }
            if (!release_pending) {
                portENTER_CRITICAL(&state_lock); busy = false;
                portEXIT_CRITICAL(&state_lock);
            }
            vTaskDelay(pdMS_TO_TICKS(20)); continue;
        }
        if (xQueueReceive(wake_queue, &request, pdMS_TO_TICKS(100)) != pdTRUE) continue;
        set_result("working");
        bool is_mounted, is_suspended, allowed;
        portENTER_CRITICAL(&state_lock);
        is_mounted = mounted; is_suspended = suspended; allowed = wake_allowed;
        portEXIT_CRITICAL(&state_lock);
        ESP_LOGI(TAG, "request=%lu", (unsigned long)request.id);
        if (!is_mounted || !tud_mounted()) {
            set_result("usb_detached");
        } else if (request.sleep) {
            send_sleep();
        } else if (is_suspended) {
            if (!allowed) {
                set_result("remote_wakeup_denied");
            } else {
                // More than 5 ms of bus idle before requesting remote resume.
                vTaskDelay(pdMS_TO_TICKS(20));
                if (!tud_suspended() || !tud_remote_wakeup()) {
                    set_result("resume_signal_rejected");
                } else {
                    const int64_t deadline = esp_timer_get_time() + 5000000;
                    while (tud_mounted() && tud_suspended() && esp_timer_get_time() < deadline)
                        vTaskDelay(pdMS_TO_TICKS(20));
                    if (!tud_mounted()) set_result("usb_disconnected");
                    else if (tud_suspended()) set_result("resume_timeout");
                    else {
                        set_result("usb_resumed_unconfirmed");
#ifdef CONFIG_WAKE_SEND_SHIFT
                        send_shift();
#endif
                    }
                }
            }
        } else {
            set_result("host_bus_active");
#ifdef CONFIG_WAKE_SEND_SHIFT
            send_shift();
#endif
        }
        if (!release_pending) {
            portENTER_CRITICAL(&state_lock); busy = false;
            portEXIT_CRITICAL(&state_lock);
        }
    }
}

static void button_task(void *arg)
{
    (void)arg;
    bool down = false; int low_ticks = 0, high_ticks = 0;
    while (true) {
        if (gpio_get_level(GPIO_NUM_0) == 0) {
            high_ticks = 0;
            if (low_ticks < 3) ++low_ticks;
            if (low_ticks == 3 && !down) {
                down = true; uint32_t id; enqueue_wake(&id);
            }
        } else {
            low_ticks = 0;
            if (++high_ticks >= 3) { down = false; high_ticks = 3; }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void reconnect(void *arg)
{ (void)arg; esp_wifi_connect(); }
static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) esp_wifi_connect();
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi disconnected; retry in 5 seconds");
        esp_timer_stop(reconnect_timer);
        esp_timer_start_once(reconnect_timer, 5000000);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        esp_timer_stop(reconnect_timer);
        const ip_event_got_ip_t *event = data;
        ESP_LOGI(TAG, "Wi-Fi connected: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}
static void start_wifi(void)
{
    if (strlen(CONFIG_WAKE_WIFI_SSID) == 0) {
        ESP_LOGW(TAG, "Button-only mode; Wi-Fi SSID is empty"); return;
    }
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    const esp_timer_create_args_t timer_cfg = {.callback=reconnect, .name="wifi_retry"};
    ESP_ERROR_CHECK(esp_timer_create(&timer_cfg, &reconnect_timer));
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));
    wifi_config_t wifi = {0};
    const size_t ssid_len = strlen(CONFIG_WAKE_WIFI_SSID);
    const size_t pass_len = strlen(CONFIG_WAKE_WIFI_PASSWORD);
    if (ssid_len > sizeof(wifi.sta.ssid) || pass_len > sizeof(wifi.sta.password)) {
        ESP_LOGE(TAG, "Wi-Fi configuration exceeds maximum length"); return;
    }
    memcpy(wifi.sta.ssid, CONFIG_WAKE_WIFI_SSID, ssid_len);
    memcpy(wifi.sta.password, CONFIG_WAKE_WIFI_PASSWORD, pass_len);
    wifi.sta.pmf_cfg.capable = true;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    blinker_cloud_start(enqueue_wake, enqueue_sleep, cloud_snapshot);
}
void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase()); err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    wake_queue = xQueueCreate(1, sizeof(control_request_t));
    configASSERT(wake_queue);
    uint8_t mac[6]; ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    snprintf(serial_number, sizeof(serial_number), "%02X%02X%02X%02X%02X%02X",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    const gpio_config_t button = {.pin_bit_mask=1ULL<<GPIO_NUM_0, .mode=GPIO_MODE_INPUT,
        .pull_up_en=GPIO_PULLUP_ENABLE, .pull_down_en=GPIO_PULLDOWN_DISABLE,
        .intr_type=GPIO_INTR_DISABLE};
    ESP_ERROR_CHECK(gpio_config(&button));
    const tinyusb_config_t usb = {
        .device_descriptor=&device_descriptor, .string_descriptor=strings,
        .string_descriptor_count=5, .external_phy=false,
        .configuration_descriptor=configuration_descriptor,
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&usb));
    ESP_ERROR_CHECK(xTaskCreate(wake_task, "wake", 4096, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(button_task, "button", 2048, NULL, 3, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    start_wifi();
    ESP_LOGI(TAG, "Ready. BOOT button requests wake; do not hold BOOT during reset.");
}
