#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "builtin_screen.h"
#include "client.h"
#include "display.h"
#include "png_decoder.h"
#include "wifi_manager.h"

namespace {
constexpr char TAG[] = "dashboard";

constexpr uint32_t DEFAULT_REFRESH_SECONDS = 1800; // used if the server returns no refresh header
constexpr uint32_t ERROR_RETRY_SECONDS = 300;

constexpr int LOW_BATTERY_MV = CONFIG_DASHBOARD_LOW_BATTERY_MV;

// Persisted state
enum class DisplayContent : uint8_t { Unknown, Dashboard, BuiltinScreen };
RTC_DATA_ATTR DisplayContent displayed_content = DisplayContent::Unknown;
RTC_DATA_ATTR BuiltinScreen displayed_builtin_screen = BuiltinScreen::None;
RTC_DATA_ATTR char stored_etag[ETAG_BUFFER_SIZE] = {};
RTC_DATA_ATTR bool battery_low_level = false;

esp_err_t render_png(const uint8_t* png_data, int size) {
    const DisplayInfo display = display_info();
    indexed_png_t image = {};
    esp_err_t err = decode_indexed_png(png_data, size, &image, display.width, display.height);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PNG decoding failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Decoded %lux%lu indexed PNG, %u-bit, %u bytes", (unsigned long)image.width, (unsigned long)image.height,
             (unsigned)image.size);

    err = display_render(image.pixels);
    free_indexed_png(&image);
    return err;
}

static void show_screen(BuiltinScreen screen, bool fast = false) {
    if (displayed_content == DisplayContent::BuiltinScreen && displayed_builtin_screen == screen) {
        return; // Screen already up to date
    }

    displayed_content = DisplayContent::Unknown;
    stored_etag[0] = '\0';

    const esp_err_t screen_err = show_builtin_screen(screen, fast);
    if (screen_err == ESP_OK) {
        displayed_content = DisplayContent::BuiltinScreen;
        displayed_builtin_screen = screen;
    } else {
        ESP_LOGE(TAG, "Error screen failed: %s", esp_err_to_name(screen_err));
    }
}

static bool handle_wakeup() {
    const esp_sleep_wakeup_cause_t wakeup = esp_sleep_get_wakeup_cause();
    ESP_LOGI(TAG, "wakeup cause: %d", static_cast<int>(wakeup));
    if (wakeup == ESP_SLEEP_WAKEUP_EXT0) {
        ESP_LOGI(TAG, "Manual refresh requested (EE02 button 1)");
        return true;
    }
    if (wakeup != ESP_SLEEP_WAKEUP_TIMER && wakeup != ESP_SLEEP_WAKEUP_EXT0) {
        // Reset state following a cold boot, reset, or crash
        stored_etag[0] = '\0';
        displayed_content = DisplayContent::Unknown;
        displayed_builtin_screen = BuiltinScreen::None;
        battery_low_level = false;
    }

    return false;
}

static void initialize_nvram() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static int update_battery_state() {
    // Read battery voltage
    int battery_mv = -1;
    const esp_err_t battery_err = display_read_battery_mv(&battery_mv);
    ESP_LOGI(TAG, "Battery voltage: %.2f V", static_cast<double>(battery_mv) / 1000.0);

    if (battery_err != ESP_OK) {
        ESP_LOGW(TAG, "Battery measurement unavailable: %s", esp_err_to_name(battery_err));
    }

    // Lock the display if battery if below level
    else if (!battery_low_level && battery_mv <= LOW_BATTERY_MV) {
        ESP_LOGW(TAG, "Battery low (%d mV <= %d mV): locking display", battery_mv, LOW_BATTERY_MV);
        battery_low_level = true;
    }

    // Unlock the display if battery above recovery level
    else if (battery_low_level && battery_mv >= LOW_BATTERY_MV + 200) {
        ESP_LOGI(TAG, "Battery recovered (%d mV >= %d mV), resuming", battery_mv, LOW_BATTERY_MV + 200);
        battery_low_level = false;
    }

    return battery_mv;
}
} // namespace

extern "C" void app_main(void) {
    // Board initialization
    bool manual_wakeup = handle_wakeup();
    initialize_nvram();

    // Battery management
    int battery_mv = update_battery_state();
    if (battery_low_level) {
        show_screen(BuiltinScreen::LowBattery);
        display_enter_deep_sleep(ERROR_RETRY_SECONDS);
        return;
    }

    // Show pending update if user clicked on the button
    if (manual_wakeup) {
        show_screen(BuiltinScreen::Updating, true);
    }

    // Connect to wifi
    const esp_err_t wifi_err = connect_wifi();
    if (wifi_err != ESP_OK) {
        stop_wifi();
        show_screen(BuiltinScreen::WifiUnavailable);
        display_enter_deep_sleep(ERROR_RETRY_SECONDS);
        return;
    }

    // Download the image and immediately
    DashboardImageRequest request{};
    request.url = CONFIG_DASHBOARD_URL;
    request.battery_mv = battery_mv;
    std::snprintf(request.etag, sizeof(request.etag), "%s", stored_etag);
    BuiltinScreen err_download = request_image(request);

    // Stop wifi now, we no longer need it
    stop_wifi();

    // Display the error and stop now if the request failed
    if (err_download != BuiltinScreen::None) {
        show_screen(err_download);
        display_enter_deep_sleep(ERROR_RETRY_SECONDS);
        return;
    }

    // Display the image
    if (request.response_size > 0) {
        displayed_content = DisplayContent::Unknown; // in case we crash here
        esp_err_t err = render_png(request.response_data, request.response_size);
        heap_caps_free(request.response_data);

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Image displayed successfully");
            std::memcpy(stored_etag, request.etag, sizeof(stored_etag));
            displayed_content = DisplayContent::Dashboard;
        } else {
            show_screen(BuiltinScreen::ImageDecodeFailed);
            display_enter_deep_sleep(ERROR_RETRY_SECONDS);
            return;
        }
    } else {
        ESP_LOGI(TAG, "Image unchanged; skipping panel refresh");
    }

    if (request.next_refresh > 0) {
        display_enter_deep_sleep(request.next_refresh);
    } else {
        display_enter_deep_sleep(DEFAULT_REFRESH_SECONDS);
    }
}
