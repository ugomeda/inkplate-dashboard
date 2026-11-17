
#include <cstdio>
#include <cstring>
#include <strings.h>

#include "Inkplate.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "FreeSans18pt7b.h"
#include "icons.h"

static const char *TAG = "dashboard";

static EventGroupHandle_t wifi_events;
static constexpr EventBits_t WIFI_CONNECTED = BIT0;
static constexpr EventBits_t WIFI_FAILED = BIT1;
static int wifi_retries = 0;

static constexpr uint32_t DEFAULT_REFRESH_SECONDS = 1800;
static constexpr size_t MAX_ETAG_LENGTH = 255;

// Retained across deep sleep, cleared after a normal power reset.
RTC_DATA_ATTR static char stored_etag[MAX_ETAG_LENGTH + 1] = {};

// Wi-Fi events
static void wifi_event_handler(
    void *arg,
    esp_event_base_t base,
    int32_t id,
    void *data
) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();

    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(wifi_events, WIFI_CONNECTED);

        if (wifi_retries++ < 5) {
            ESP_LOGW(TAG, "Wi-Fi disconnected, retrying...");
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(wifi_events, WIFI_FAILED);
        }

    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto *event = static_cast<ip_event_got_ip_t *>(data);

        ESP_LOGI(TAG, "Wi-Fi connected: " IPSTR,
                 IP2STR(&event->ip_info.ip));

        wifi_retries = 0;
        xEventGroupSetBits(wifi_events, WIFI_CONNECTED);
    }
}

static bool connect_wifi() {
    ESP_LOGI(TAG, "Connecting to Wi-Fi...");

    wifi_events = xEventGroupCreate();
    if (wifi_events == nullptr) {
        return false;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));

    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID,
        wifi_event_handler, nullptr
    ));
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP,
        wifi_event_handler, nullptr
    ));

    wifi_config_t config = {};

    static_assert(
        sizeof(CONFIG_DASHBOARD_WIFI_SSID) - 1 <= sizeof(config.sta.ssid)
    );
    static_assert(
        sizeof(CONFIG_DASHBOARD_WIFI_PASSWORD) - 1 <= sizeof(config.sta.password)
    );

    std::memcpy(
        config.sta.ssid,
        CONFIG_DASHBOARD_WIFI_SSID,
        sizeof(CONFIG_DASHBOARD_WIFI_SSID) - 1
    );
    std::memcpy(
        config.sta.password,
        CONFIG_DASHBOARD_WIFI_PASSWORD,
        sizeof(CONFIG_DASHBOARD_WIFI_PASSWORD) - 1
    );

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(
        wifi_events,
        WIFI_CONNECTED | WIFI_FAILED,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(20000)
    );

    if (!(bits & WIFI_CONNECTED)) {
        ESP_LOGE(TAG, "Wi-Fi connection failed or timed out");
        return false;
    }

    return true;
}




static constexpr int DISPLAY_WIDTH = 825;
static constexpr int DISPLAY_HEIGHT = 1200;
static constexpr int STATUS_HEIGHT = 70;

static bool status_visible = false;

static void draw_status_bar(
    Inkplate &display,
    const char *message,
    const uint8_t *icon,
    bool inverted = false
) {
    const int y = DISPLAY_HEIGHT - STATUS_HEIGHT;

    // Normal: white text on black background.
    // Inverted: black text on white background.
    display.fillRect(0, y, DISPLAY_WIDTH, STATUS_HEIGHT, inverted ? WHITE : BLACK
    );

    display.setFont(&FreeSans18pt7b);
    display.setTextSize(1);
    display.setTextWrap(false);
    display.setTextColor(inverted ? BLACK : WHITE);
    display.setCursor(50 + ICON_SIZE, y + 45);
    display.print(message);

    display.drawBitmap(
        30, y + (STATUS_HEIGHT - ICON_SIZE) / 2,
        ICON_REFRESH_CW,
        ICON_SIZE,
        ICON_SIZE,
        inverted ? BLACK : WHITE
    );
}

static void show_status(Inkplate &display, const char *message, const uint8_t *icon) {
    ESP_LOGI(TAG, "Status: %s", message);

    display.setDisplayMode(BLACK_AND_WHITE);

    if (!status_visible) {
        // After deep sleep, the driver doesn't know the
        // previous contents of the physical display.
        //
        // Preload the inverse status bar to force the
        // partial update to repaint its pixels.
        display.clearDisplay();
        draw_status_bar(display, message, icon, true);
        display.preloadScreen();
    }

    display.clearDisplay();
    draw_status_bar(display, message, icon);

    // Force a partial update, even after deep sleep.
    display.partialUpdate(true);

    status_visible = true;

    // The physical display no longer matches the
    // server image, so invalidate its ETag.
    stored_etag[0] = '\0';
}




static constexpr size_t MAX_IMAGE_SIZE = 1024 * 1024;

struct ImageDownload {
    uint8_t *data = nullptr;
    size_t size = 0;
    size_t capacity = 0;
    bool failed = false;
    char etag[MAX_ETAG_LENGTH + 1] = {};
    uint32_t next_refresh = DEFAULT_REFRESH_SECONDS;

    ~ImageDownload() {
        heap_caps_free(data);
    }

    ImageDownload(const ImageDownload &) = delete;
    ImageDownload &operator=(const ImageDownload &) = delete;
    ImageDownload() = default;
};

static esp_err_t http_event_handler(esp_http_client_event_t *event) {
    auto *image = static_cast<ImageDownload *>(event->user_data);

    if (event->event_id == HTTP_EVENT_ON_HEADER) {
        const char *key = event->header_key;
        const char *value = event->header_value;

        if (!key || !value) {
            return ESP_OK;
        }

        ESP_LOGI(TAG, "HTTP header: %s: %s", key, value);

        if (strcasecmp(key, "etag") == 0) {
            if (strlen(value) < sizeof(image->etag)) {
                strcpy(image->etag, value);
            }
        } else if (strcasecmp(key, "x-inkplate-next-refresh") == 0) {
            char *end = nullptr;
            unsigned long seconds = strtoul(value, &end, 10);

            if (end != value && *end == '\0' &&
                seconds > 0 && seconds <= 65535) {
                image->next_refresh = static_cast<uint32_t>(seconds);
            }
        }

        return ESP_OK;
    }

    if (event->event_id != HTTP_EVENT_ON_DATA ||
        event->data_len <= 0 || image->failed) {
        return ESP_OK;
    }

    size_t length = static_cast<size_t>(event->data_len);

    if (length > MAX_IMAGE_SIZE - image->size) {
        ESP_LOGE(TAG, "PNG exceeds maximum allowed size");
        image->failed = true;
        return ESP_OK;
    }

    size_t required = image->size + length;

    if (required > image->capacity) {
        size_t capacity = image->capacity ? image->capacity : 128 * 1024;

        while (capacity < required) {
            capacity *= 2;
        }

        auto *buffer = static_cast<uint8_t *>(
            heap_caps_realloc(
                image->data,
                capacity,
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
            )
        );

        if (!buffer) {
            ESP_LOGE(TAG, "Failed to allocate %u bytes in PSRAM",
                     static_cast<unsigned>(capacity));
            image->failed = true;
            return ESP_OK;
        }

        image->data = buffer;
        image->capacity = capacity;
    }

    std::memcpy(
        image->data + image->size,
        event->data,
        length
    );

    image->size += length;
    return ESP_OK;
}



static uint32_t fetch_image(Inkplate &display) {
    ESP_LOGI(TAG, "Fetching %s", CONFIG_DASHBOARD_URL);

    ImageDownload image;

    esp_http_client_config_t config = {};
    config.url = CONFIG_DASHBOARD_URL;
    config.event_handler = http_event_handler;
    config.user_data = &image;
    config.timeout_ms = 20000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "HTTP client initialization failed");
        return DEFAULT_REFRESH_SECONDS;
    }

    if (stored_etag[0] != '\0') {
        ESP_LOGI(TAG, "Sending ETag: %s", stored_etag);
        esp_http_client_set_header(
            client, "If-None-Match", stored_etag
        );
    }

    char voltage[16];
    std::snprintf(
        voltage, sizeof(voltage), "%.2fV", display.readBattery()
    );

    esp_http_client_set_header(
        client, "X-Inkplate-Battery-Voltage", voltage
    );

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    int64_t expected_size = esp_http_client_get_content_length(client);

    esp_http_client_cleanup(client);


    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        return DEFAULT_REFRESH_SECONDS;
    }

    ESP_LOGI(TAG, "HTTP %d, downloaded %u bytes",
             status, static_cast<unsigned>(image.size));

    if (status == 304) {
        ESP_LOGI(TAG, "Image unchanged, skipping refresh");
        return image.next_refresh;
    }

    if (status != 200 || image.failed || image.size < 8 ||
        image.size > INT32_MAX ||
        (expected_size >= 0 &&
         static_cast<int64_t>(image.size) != expected_size)) {
        ESP_LOGE(TAG, "Invalid or incomplete image response");
        return DEFAULT_REFRESH_SECONDS;
    }

    const uint8_t png_signature[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
    };

    if (std::memcmp(image.data, png_signature, 8) != 0) {
        ESP_LOGE(TAG, "Response is not a PNG");
        return DEFAULT_REFRESH_SECONDS;
    }

    display.setDisplayMode(GRAYSCALE);
    display.clearDisplay();

    if (!display.image.draw(
        image.data,
        static_cast<int32_t>(image.size),
        0, 0,
        false, false
    )) {
        ESP_LOGE(TAG, "PNG decoding failed");
        return DEFAULT_REFRESH_SECONDS;
    }

    ESP_LOGI(TAG, "Refreshing display");
    ESP_ERROR_CHECK(display.display());

    // Only commit the ETag after a successful display refresh.
    // An absent ETag clears the previous one.
    std::memcpy(stored_etag, image.etag, sizeof(stored_etag));

    ESP_LOGI(TAG, "Display refreshed successfully");

    return image.next_refresh;
}

static void go_to_sleep(uint32_t seconds) {
    ESP_LOGI(TAG, "Going to sleep for %lu seconds",
             static_cast<unsigned long>(seconds));

    // Stop Wi-Fi before deep sleep.
    ESP_ERROR_CHECK(esp_wifi_stop());

    // Timer wakeup.
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(
        static_cast<uint64_t>(seconds) * 1000000ULL
    ));

    // Physical WAKE button on Inkplate 10 V2.
    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(
        GPIO_NUM_36, 0
    ));

    esp_deep_sleep_start();
}

extern "C" void app_main(void) {
    // Configure memory
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Initialize display
    Inkplate display;
    display.setRotation(3);

    // Display message if wakeup is caused by user
    auto wakeup = esp_sleep_get_wakeup_cause();
    ESP_LOGI(TAG, "Wakeup cause: %d", wakeup);

    if (wakeup == ESP_SLEEP_WAKEUP_EXT0) {
        show_status(display, "Updating display...", ICON_REFRESH_CW);
    } else if (wakeup != ESP_SLEEP_WAKEUP_TIMER) {
        show_status(display, "Loading dashboard...", ICON_REFRESH_CW);
    }

    // Connect to wifi, load image and display it
    uint32_t sleep_seconds = DEFAULT_REFRESH_SECONDS;
    if (connect_wifi()) {
        sleep_seconds = fetch_image(display);
    } else {
        show_status(display, "Wi-Fi connection failed", ICON_WIFI_OFF);
    }

    // Go back to sleep
    go_to_sleep(sleep_seconds);
}