#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#include "error_screen.h"
#include "png_decoder.h"
#include "spectra6.h"

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/rtc_io.h"
#include "esp_attr.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

namespace {
constexpr char TAG[] = "dashboard";
constexpr size_t MAX_PNG_BYTES = 1024 * 1024;
constexpr uint32_t DEFAULT_REFRESH_SECONDS = 1800;
constexpr uint32_t ERROR_RETRY_SECONDS = 300;
constexpr uint32_t MIN_REFRESH_SECONDS = 30;
constexpr uint32_t MAX_REFRESH_SECONDS = 24 * 3600;
constexpr size_t ETAG_BUFFER_SIZE = 128;
constexpr gpio_num_t REFRESH_BUTTON_GPIO = GPIO_NUM_2; // EE02 button 1: active-low
constexpr gpio_num_t BATTERY_ENABLE_GPIO = GPIO_NUM_6; // EE02 ADC_EN, active-high
constexpr adc_unit_t BATTERY_ADC_UNIT = ADC_UNIT_1;
constexpr adc_channel_t BATTERY_ADC_CHANNEL = ADC_CHANNEL_0; // GPIO1
constexpr adc_atten_t BATTERY_ADC_ATTEN = ADC_ATTEN_DB_12;
constexpr int BATTERY_DIVIDER_RATIO = 2; // EE02 R28=R29=10k

// RTC memory survives deep sleep, but not an ordinary power cycle.
RTC_DATA_ATTR char stored_etag[ETAG_BUFFER_SIZE] = {};
RTC_DATA_ATTR uint8_t consecutive_failures = 0;
RTC_DATA_ATTR bool error_screen_visible = false;
constexpr EventBits_t WIFI_READY = BIT0;
constexpr EventBits_t WIFI_FAILED = BIT1;
EventGroupHandle_t wifi_events = nullptr;
int wifi_retries = 0;
bool wifi_stopping = false;

void wifi_event_handler(void *, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (wifi_stopping) return; // Do not reconnect during intentional shutdown.
        auto *event = static_cast<wifi_event_sta_disconnected_t *>(data);
        ESP_LOGW(TAG, "Wi-Fi disconnected (reason=%u, RSSI=%d)",
                 static_cast<unsigned>(event->reason), event->rssi);
        xEventGroupClearBits(wifi_events, WIFI_READY);
        if (wifi_retries++ < 5) {
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(wifi_events, WIFI_FAILED);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto *event = static_cast<ip_event_got_ip_t *>(data);
        ESP_LOGI(TAG, "Connected: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_retries = 0;
        xEventGroupSetBits(wifi_events, WIFI_READY);
    }
}

esp_err_t connect_wifi() {
    wifi_events = xEventGroupCreate();
    if (!wifi_events) return ESP_ERR_NO_MEM;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    if (!esp_netif_create_default_wifi_sta()) return ESP_FAIL;

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                wifi_event_handler, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                wifi_event_handler, nullptr));

    wifi_config_t wifi = {};
    static_assert(sizeof(CONFIG_DASHBOARD_WIFI_SSID) - 1 <= sizeof(wifi.sta.ssid));
    static_assert(sizeof(CONFIG_DASHBOARD_WIFI_PASSWORD) - 1 <= sizeof(wifi.sta.password));
    std::memcpy(wifi.sta.ssid, CONFIG_DASHBOARD_WIFI_SSID,
                sizeof(CONFIG_DASHBOARD_WIFI_SSID) - 1);
    std::memcpy(wifi.sta.password, CONFIG_DASHBOARD_WIFI_PASSWORD,
                sizeof(CONFIG_DASHBOARD_WIFI_PASSWORD) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi));
    ESP_ERROR_CHECK(esp_wifi_start());

    const EventBits_t bits = xEventGroupWaitBits(
        wifi_events, WIFI_READY | WIFI_FAILED,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(20000));
    return (bits & WIFI_READY) ? ESP_OK : ESP_ERR_TIMEOUT;
}

// Reads the EE02's battery voltage via its switched 10k/10k divider.
// The ADC is powered only for the duration of a measurement. No battery (or
// an implausible reading) is reported as an error rather than as 0.00 V.
esp_err_t read_battery_voltage_mv(int *battery_mv) {
    if (!battery_mv) return ESP_ERR_INVALID_ARG;

    // The board has an external 100k pulldown on ADC_EN, ensuring the divider
    // is disabled at reset/deep sleep. Explicitly drive LOW when we're awake.
    gpio_config_t enable_cfg = {};
    enable_cfg.pin_bit_mask = 1ULL << BATTERY_ENABLE_GPIO;
    enable_cfg.mode = GPIO_MODE_OUTPUT;
    enable_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    enable_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    esp_err_t err = gpio_config(&enable_cfg);
    if (err != ESP_OK) return err;
    ESP_ERROR_CHECK(gpio_set_level(BATTERY_ENABLE_GPIO, 0));

    adc_oneshot_unit_handle_t adc = nullptr;
    adc_cali_handle_t calibration = nullptr;

    do {
        adc_oneshot_unit_init_cfg_t unit_cfg = {};
        unit_cfg.unit_id = BATTERY_ADC_UNIT;
        err = adc_oneshot_new_unit(&unit_cfg, &adc);
        if (err != ESP_OK) break;

        adc_oneshot_chan_cfg_t channel_cfg = {};
        channel_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
        channel_cfg.atten = BATTERY_ADC_ATTEN;
        err = adc_oneshot_config_channel(adc, BATTERY_ADC_CHANNEL, &channel_cfg);
        if (err != ESP_OK) break;

        adc_cali_curve_fitting_config_t cal_cfg = {};
        cal_cfg.unit_id = BATTERY_ADC_UNIT;
        cal_cfg.atten = BATTERY_ADC_ATTEN;
        cal_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
        err = adc_cali_create_scheme_curve_fitting(&cal_cfg, &calibration);
        if (err != ESP_OK) break;

        err = gpio_set_level(BATTERY_ENABLE_GPIO, 1);
        if (err != ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(20)); // Let the switched divider settle.

        constexpr int samples = 8;
        int total_mv = 0;
        for (int i = 0; i < samples; ++i) {
            int adc_mv = 0;
            err = adc_oneshot_get_calibrated_result(
                adc, calibration, BATTERY_ADC_CHANNEL, &adc_mv);
            if (err != ESP_OK) break;
            total_mv += adc_mv;
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        if (err != ESP_OK) break;

        // Battery is measured through an equal-resistor divider (factor 2).
        const int measured_mv = (total_mv * BATTERY_DIVIDER_RATIO) / samples;
        if (measured_mv < 2500 || measured_mv > 4500) {
            ESP_LOGW(TAG, "Battery not detected / implausible reading: %d mV",
                     measured_mv);
            err = ESP_ERR_INVALID_RESPONSE;
            break;
        }

        *battery_mv = measured_mv;
        ESP_LOGI(TAG, "Battery voltage: %.2f V",
                 static_cast<double>(measured_mv) / 1000.0);
    } while (false);

    // Disable the divider regardless of ADC errors (important for deep sleep).
    const esp_err_t disable_err = gpio_set_level(BATTERY_ENABLE_GPIO, 0);
    if (err == ESP_OK) err = disable_err;
    if (calibration) adc_cali_delete_scheme_curve_fitting(calibration);
    if (adc) adc_oneshot_del_unit(adc);
    return err;
}

struct PngDownload {
    uint8_t *data = nullptr;
    size_t size = 0;
    int http_status = 0;
    uint32_t next_refresh = DEFAULT_REFRESH_SECONDS;
    char etag[ETAG_BUFFER_SIZE] = {};

    ~PngDownload() { heap_caps_free(data); }
    PngDownload() = default;
    PngDownload(const PngDownload &) = delete;
    PngDownload &operator=(const PngDownload &) = delete;
};

esp_err_t http_event_handler(esp_http_client_event_t *event) {
    auto *png = static_cast<PngDownload *>(event->user_data);

    if (event->event_id == HTTP_EVENT_ON_HEADER) {
        if (!event->header_key || !event->header_value) return ESP_OK;

        const char *key = event->header_key;
        const char *value = event->header_value;
        if (strcasecmp(key, "etag") == 0) {
            const size_t length = std::strlen(value);
            if (length < sizeof(png->etag)) {
                std::memcpy(png->etag, value, length + 1);
            } else {
                ESP_LOGW(TAG, "ETag too long; disabling conditional requests");
                png->etag[0] = '\0';
            }
        } else if (strcasecmp(key, "x-inkplate-next-refresh") == 0) {
            char *end = nullptr;
            const unsigned long seconds = std::strtoul(value, &end, 10);
            if (end != value && *end == '\0' &&
                seconds >= MIN_REFRESH_SECONDS && seconds <= MAX_REFRESH_SECONDS) {
                png->next_refresh = static_cast<uint32_t>(seconds);
            } else {
                ESP_LOGW(TAG, "Ignoring invalid refresh interval: %s", value);
            }
        }
        return ESP_OK;
    }

    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
        return ESP_OK;
    }

    const size_t count = static_cast<size_t>(event->data_len);
    if (count > MAX_PNG_BYTES - png->size) {
        ESP_LOGE(TAG, "PNG exceeds %u bytes", static_cast<unsigned>(MAX_PNG_BYTES));
        return ESP_ERR_INVALID_SIZE;
    }

    // HTTP 304 has no body; avoid allocating 1 MiB on unchanged images.
    if (!png->data) {
        png->data = static_cast<uint8_t *>(heap_caps_malloc(
            MAX_PNG_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!png->data) return ESP_ERR_NO_MEM;
    }

    std::memcpy(png->data + png->size, event->data, count);
    png->size += count;
    return ESP_OK;
}

esp_err_t download_png(PngDownload &png, int battery_mv) {
    esp_http_client_config_t config = {};
    config.url = CONFIG_DASHBOARD_URL;
    config.timeout_ms = 20000;
    config.disable_auto_redirect = true;
    config.event_handler = http_event_handler;
    config.user_data = &png;

    auto client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;

    if (stored_etag[0] != '\0') {
        ESP_LOGI(TAG, "Sending conditional request (If-None-Match)");
        esp_http_client_set_header(client, "If-None-Match", stored_etag);
    }

    // A plain decimal voltage in volts, e.g. X-Display-Battery-Voltage: 3.87.
    // Added to every request, even when the server may answer HTTP 304.
    if (battery_mv >= 0) {
        const int centivolts = (battery_mv + 5) / 10;
        char voltage[16];
        std::snprintf(voltage, sizeof(voltage), "%d.%02d",
                      centivolts / 100, centivolts % 100);
        const esp_err_t header_err = esp_http_client_set_header(
            client, "X-Display-Battery-Voltage", voltage);
        if (header_err != ESP_OK) {
            esp_http_client_cleanup(client);
            return header_err;
        }
    }

    ESP_LOGI(TAG, "GET %s", CONFIG_DASHBOARD_URL);
    const esp_err_t err = esp_http_client_perform(client);
    png.http_status = esp_http_client_get_status_code(client);
    const int64_t length = esp_http_client_get_content_length(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "HTTP %d, %u bytes", png.http_status,
             static_cast<unsigned>(png.size));

    if (png.http_status == 304) {
        // Never accept a 304 without a known displayed image.
        return (stored_etag[0] != '\0' && png.size == 0) ? ESP_OK : ESP_FAIL;
    }

    if (png.http_status != 200 || png.size < 26 ||
        (length >= 0 && static_cast<int64_t>(png.size) != length)) {
        ESP_LOGE(TAG, "Unexpected or incomplete HTTP response");
        return ESP_FAIL;
    }
    return ESP_OK;
}

struct PanelColor {
    int r, g, b;
    uint8_t code;
};

// Same reference colors as the server-side Spectra6 palette.
constexpr PanelColor panel_colors[] = {
    {  2,   2,   2, 0x0}, // black
    {190, 200, 200, 0x1}, // white
    {205, 202,   0, 0x2}, // yellow
    {135,  19,   0, 0x3}, // red
    {  5,  64, 158, 0x5}, // blue
    { 39, 102,  60, 0x6}, // green
};

uint8_t closest_panel_color(uint8_t r, uint8_t g, uint8_t b) {
    int best_distance = INT_MAX;
    uint8_t best_code = 0;
    for (const auto &color : panel_colors) {
        const int dr = static_cast<int>(r) - color.r;
        const int dg = static_cast<int>(g) - color.g;
        const int db = static_cast<int>(b) - color.b;
        const int distance = dr * dr + dg * dg + db * db;
        if (distance < best_distance) {
            best_distance = distance;
            best_code = color.code;
        }
    }
    return best_code;
}

esp_err_t render_png(const PngDownload &download) {
    indexed_png_t image = {};
    esp_err_t err = decode_indexed_png(download.data, download.size, &image);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PNG decoding failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Decoded %lux%lu indexed PNG, %u-bit, %u bytes",
             (unsigned long)image.width, (unsigned long)image.height,
             image.bitdepth, (unsigned)image.size);

    uint8_t mapping[256] = {};
    for (size_t i = 0; i < image.palette_size; ++i) {
        const auto &rgb = image.palette_rgb[i];
        mapping[i] = closest_panel_color(rgb[0], rgb[1], rgb[2]);
    }

    Spectra6 display;
    err = display.init();
    if (err == ESP_OK) {
        err = display.show_indexed_image(image.pixels, image.bitdepth, mapping);
        const esp_err_t sleep_err = display.sleep();
        if (err == ESP_OK) err = sleep_err;
    }

    free_indexed_png(&image);
    return err;
}

} // namespace

static void enter_deep_sleep(uint32_t seconds) {
    ESP_LOGI(TAG, "Sleeping for %lu seconds", static_cast<unsigned long>(seconds));

    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(
        static_cast<uint64_t>(seconds) * 1000000ULL));

    // GPIO2 is button 1 on the EE02, with an external pull-up resistor.
    // EXT0 uses RTC IO, so restore the digital GPIO function before reading it.
    ESP_ERROR_CHECK(rtc_gpio_deinit(REFRESH_BUTTON_GPIO));
    gpio_config_t button_config = {};
    button_config.pin_bit_mask = 1ULL << REFRESH_BUTTON_GPIO;
    button_config.mode = GPIO_MODE_INPUT;
    button_config.pull_up_en = GPIO_PULLUP_ENABLE;
    button_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&button_config));

    // EXT0 is level-triggered. Entering sleep while the button is held LOW
    // would immediately wake the board and could cause a reboot loop.
    bool button_released = false;
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (gpio_get_level(REFRESH_BUTTON_GPIO) == 1) {
            vTaskDelay(pdMS_TO_TICKS(30));  // Debounce release.
            if (gpio_get_level(REFRESH_BUTTON_GPIO) == 1) {
                button_released = true;
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (button_released) {
        ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(REFRESH_BUTTON_GPIO, 0));
        ESP_LOGI(TAG, "Refresh button wakeup enabled on GPIO2 (LOW)");
    } else {
        // Still sleep on timer even if the button is stuck LOW.
        ESP_LOGW(TAG, "Button held LOW; disabling button wakeup for this cycle");
    }

    vTaskDelay(pdMS_TO_TICKS(100)); // Flush log output.
    esp_deep_sleep_start();
}

extern "C" void app_main(void) {
    const esp_sleep_wakeup_cause_t wakeup = esp_sleep_get_wakeup_cause();
    ESP_LOGI(TAG, "Spectra 6 wakeup cause: %d", static_cast<int>(wakeup));

    // Don't trust RTC ETags following a cold boot, reset, or crash: the
    // physical display may not hold the image represented by that ETag.
    if (wakeup == ESP_SLEEP_WAKEUP_EXT0) {
        ESP_LOGI(TAG, "Manual refresh requested (EE02 button 1)");
    }
    if (wakeup != ESP_SLEEP_WAKEUP_TIMER && wakeup != ESP_SLEEP_WAKEUP_EXT0) {
        stored_etag[0] = '\0';
        consecutive_failures = 0;
        error_screen_visible = false;
    }

    ESP_LOGI(TAG, "PSRAM available: %u bytes",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Sample before enabling Wi-Fi to reduce ADC interference. A failed or
    // missing battery reading must not prevent the dashboard from updating.
    int battery_mv = -1;
    const esp_err_t battery_err = read_battery_voltage_mv(&battery_mv);
    if (battery_err != ESP_OK) {
        ESP_LOGW(TAG, "Battery measurement unavailable: %s",
                 esp_err_to_name(battery_err));
        battery_mv = -1;
    }

    uint32_t next_refresh = ERROR_RETRY_SECONDS;
    bool update_succeeded = false;
    ErrorScreenKind failure_kind = ErrorScreenKind::Http;
    const esp_err_t wifi_err = connect_wifi();
    if (wifi_err == ESP_OK) {
        // Ensure the 1 MiB HTTP buffer is released before the error renderer
        // allocates its own framebuffer in PSRAM.
        PngDownload png;
        err = download_png(png, battery_mv);
        if (err == ESP_OK) {
            if (png.http_status == 304) {
                ESP_LOGI(TAG, "Image unchanged; skipping panel refresh");
                next_refresh = png.next_refresh;
                update_succeeded = true;
            } else {
                err = render_png(png);
                if (err == ESP_OK) {
                    // Commit ETag only after the panel has refreshed AND slept.
                    // Missing ETag clears any previously cached value.
                    std::memcpy(stored_etag, png.etag, sizeof(stored_etag));
                    next_refresh = png.next_refresh;
                    update_succeeded = true;
                    error_screen_visible = false;
                    ESP_LOGI(TAG, "Image displayed successfully");
                } else {
                    // A failed panel update may have changed some pixels.
                    // Avoid accepting 304 for a potentially inconsistent screen.
                    stored_etag[0] = '\0';
                    failure_kind = ErrorScreenKind::Image;
                }
            }
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Update failed: %s", esp_err_to_name(err));
            // A valid HTTP 200 with invalid PNG is a rendering failure rather
            // than a connection failure.
            if (png.http_status == 200) failure_kind = ErrorScreenKind::Image;
        }
    } else {
        failure_kind = ErrorScreenKind::Wifi;
        ESP_LOGE(TAG, "Wi-Fi connection failed: %s", esp_err_to_name(wifi_err));
    }

    // A deliberate stop emits a disconnect event; don't reconnect then.
    wifi_stopping = true;
    const esp_err_t stop_err = esp_wifi_stop();
    if (stop_err != ESP_OK && stop_err != ESP_ERR_WIFI_NOT_STARTED &&
        stop_err != ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGW(TAG, "Failed to stop Wi-Fi: %s", esp_err_to_name(stop_err));
    }

    if (update_succeeded) {
        consecutive_failures = 0;
    } else {
        if (consecutive_failures < UINT8_MAX) ++consecutive_failures;
        ESP_LOGW(TAG, "Consecutive failures: %u", consecutive_failures);
        // Keep the previous dashboard image through transient failures.
        // Without a known image (cold boot), show the error immediately.
        // Once an error screen is visible, don't redraw it every 5 minutes.
        if (!error_screen_visible &&
            (stored_etag[0] == '\0' || consecutive_failures >= 3)) {
            ESP_LOGW(TAG, "Displaying landscape error screen");
            const esp_err_t screen_err = show_error_screen(failure_kind);
            // Even if the refresh failed partway through, the old ETag can no
            // longer be trusted to describe the contents of the panel.
            stored_etag[0] = '\0';
            if (screen_err == ESP_OK) {
                error_screen_visible = true;
            } else {
                ESP_LOGE(TAG, "Error screen failed: %s", esp_err_to_name(screen_err));
            }
        }
    }

    enter_deep_sleep(next_refresh);
}
