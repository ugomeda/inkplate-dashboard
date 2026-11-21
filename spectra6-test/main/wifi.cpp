#include <atomic>
#include <cstring>

#include "esp_attr.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

namespace {
constexpr char TAG[] = "wifi";
constexpr EventBits_t WIFI_READY = BIT0;
constexpr EventBits_t WIFI_FAILED = BIT1;
EventGroupHandle_t wifi_events = nullptr;
int wifi_retries = 0;
std::atomic_bool wifi_stopping = false;
RTC_DATA_ATTR uint8_t last_wifi_channel = 0;

void wifi_event_handler(void*, esp_event_base_t base, int32_t id, void* data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (wifi_stopping)
            return; // Do not reconnect during intentional shutdown.
        auto* event = static_cast<wifi_event_sta_disconnected_t*>(data);
        ESP_LOGW(TAG, "Wi-Fi disconnected (reason=%u, RSSI=%d)", static_cast<unsigned>(event->reason), event->rssi);
        xEventGroupClearBits(wifi_events, WIFI_READY);
        if (wifi_retries++ < 5) {
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(wifi_events, WIFI_FAILED);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto* event = static_cast<ip_event_got_ip_t*>(data);
        ESP_LOGI(TAG, "Connected: " IPSTR, IP2STR(&event->ip_info.ip));

        wifi_ap_record_t ap = {};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            last_wifi_channel = ap.primary;
            ESP_LOGI(TAG, "Wi-Fi channel: %u", last_wifi_channel);
        }

        wifi_retries = 0;
        xEventGroupSetBits(wifi_events, WIFI_READY);
    }
}
} // namespace

esp_err_t connect_wifi() {
    wifi_events = xEventGroupCreate();
    if (!wifi_events)
        return ESP_ERR_NO_MEM;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    if (!esp_netif_create_default_wifi_sta())
        return ESP_FAIL;

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, nullptr));

    wifi_config_t wifi = {};
    static_assert(sizeof(CONFIG_DASHBOARD_WIFI_SSID) - 1 <= sizeof(wifi.sta.ssid));
    static_assert(sizeof(CONFIG_DASHBOARD_WIFI_PASSWORD) - 1 <= sizeof(wifi.sta.password));
    std::memcpy(wifi.sta.ssid, CONFIG_DASHBOARD_WIFI_SSID, sizeof(CONFIG_DASHBOARD_WIFI_SSID) - 1);
    std::memcpy(wifi.sta.password, CONFIG_DASHBOARD_WIFI_PASSWORD, sizeof(CONFIG_DASHBOARD_WIFI_PASSWORD) - 1);
    wifi.sta.channel = last_wifi_channel;
    wifi.sta.scan_method = WIFI_FAST_SCAN;

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi));
    ESP_ERROR_CHECK(esp_wifi_start());

    const EventBits_t bits = xEventGroupWaitBits(wifi_events, WIFI_READY | WIFI_FAILED, pdFALSE, pdFALSE, pdMS_TO_TICKS(20000));
    if (bits & WIFI_READY)
        return ESP_OK;
    if (bits & WIFI_FAILED)
        return ESP_FAIL;
    return ESP_ERR_TIMEOUT;
}

esp_err_t stop_wifi() {
    wifi_stopping = true;
    wifi_retries = 0;
    return esp_wifi_stop();
}