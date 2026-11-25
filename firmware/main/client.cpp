#include "client.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"

namespace {
constexpr size_t MAX_PNG_BYTES = 1024 * 1024;
constexpr char TAG[] = "client";

esp_err_t http_event_handler(esp_http_client_event_t* event) {
    auto* request = static_cast<DashboardImageRequest*>(event->user_data);

    // Handle etag and next-refresh headers
    if (event->event_id == HTTP_EVENT_ON_HEADER) {
        if (!event->header_key || !event->header_value)
            return ESP_OK;

        const char* key = event->header_key;
        const char* value = event->header_value;
        if (strcasecmp(key, "etag") == 0) {
            const size_t length = std::strlen(value);
            if (length < sizeof(request->etag)) {
                std::memcpy(request->etag, value, length + 1);
            } else {
                ESP_LOGW(TAG, "ETag too long");
            }
        } else if (strcasecmp(key, "x-display-next-refresh") == 0) {
            const unsigned long seconds = std::strtoul(value, NULL, 10);
            request->next_refresh = static_cast<uint32_t>(seconds);
        }
    }

    // Handle data
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        const size_t count = static_cast<size_t>(event->data_len);
        if (count > MAX_PNG_BYTES - request->response_size) {
            ESP_LOGE(TAG, "PNG exceeds %u bytes", static_cast<unsigned>(MAX_PNG_BYTES));
            return ESP_ERR_INVALID_SIZE;
        }

        // HTTP 304 has no body; avoid allocating 1 MiB on unchanged images.
        if (!request->response_data) {
            request->response_data = static_cast<uint8_t*>(heap_caps_malloc(MAX_PNG_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (!request->response_data)
                return ESP_ERR_NO_MEM;
        }

        std::memcpy(request->response_data + request->response_size, event->data, count);
        request->response_size += count;
    }

    return ESP_OK;
}
} // namespace

BuiltinScreen request_image(DashboardImageRequest& request) {
    if (!request.url) {
        return BuiltinScreen::Unknown;
    }

    // Initialize client
    esp_http_client_config_t config = {};
    config.url = request.url;
    config.timeout_ms = 20000;
    config.disable_auto_redirect = true;
    config.event_handler = http_event_handler;
    config.user_data = &request;

    auto client = esp_http_client_init(&config);
    if (!client)
        return BuiltinScreen::Unknown;

    // Add etag and battery headers
    esp_err_t header_err;
    if (request.etag[0] != '\0') {
        header_err = esp_http_client_set_header(client, "If-None-Match", request.etag);
        if (header_err != ESP_OK) {
            esp_http_client_cleanup(client);
            return BuiltinScreen::Unknown;
        }
    }

    if (request.battery_mv > 0) {
        char voltage[16];
        std::snprintf(voltage, sizeof(voltage), "%.2fV", request.battery_mv / 1000.0);
        header_err = esp_http_client_set_header(client, "X-Display-Battery-Voltage", voltage);
        if (header_err != ESP_OK) {
            esp_http_client_cleanup(client);
            return BuiltinScreen::Unknown;
        }
    }

    // Launch the request
    ESP_LOGI(TAG, "GET %s", request.url);
    request.etag[0] = '\0'; // reset the tag in case the server doesn't send it
    const esp_err_t err = esp_http_client_perform(client);
    int http_status = esp_http_client_get_status_code(client);
    const int64_t length = esp_http_client_get_content_length(client);
    esp_http_client_cleanup(client);

    // Handle request failed
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        heap_caps_free(request.response_data);
        request.response_data = nullptr;
        return BuiltinScreen::ServerUnreachable;
    }

    // Sanity check response
    ESP_LOGI(TAG, "HTTP %d, %u bytes", http_status, static_cast<unsigned>(request.response_size));
    if (http_status == 304) {
        heap_caps_free(request.response_data);
        request.response_data = nullptr;
        request.response_size = 0;
        return BuiltinScreen::None;
    }

    if (http_status != 200 || request.response_size < 26 || (length >= 0 && static_cast<int64_t>(request.response_size) != length)) {
        ESP_LOGE(TAG, "Unexpected or incomplete HTTP response");
        heap_caps_free(request.response_data);
        request.response_data = nullptr;
        return BuiltinScreen::ServerUnexpectedResponse;
    }

    return BuiltinScreen::None;
}