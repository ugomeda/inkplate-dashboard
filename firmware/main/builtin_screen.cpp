#include "builtin_screen.h"

#include <cstddef>
#include <cstdint>

#include "display.h"
#include "esp_log.h"
#include "png_decoder.h"

extern const uint8_t updating_png_start[] asm("_binary_updating_png_start");
extern const uint8_t updating_png_end[] asm("_binary_updating_png_end");
extern const uint8_t low_battery_png_start[] asm("_binary_low_battery_png_start");
extern const uint8_t low_battery_png_end[] asm("_binary_low_battery_png_end");
extern const uint8_t image_decode_failed_png_start[] asm("_binary_image_decode_failed_png_start");
extern const uint8_t image_decode_failed_png_end[] asm("_binary_image_decode_failed_png_end");
extern const uint8_t server_unreachable_png_start[] asm("_binary_server_unreachable_png_start");
extern const uint8_t server_unreachable_png_end[] asm("_binary_server_unreachable_png_end");
extern const uint8_t server_error_png_start[] asm("_binary_server_error_png_start");
extern const uint8_t server_error_png_end[] asm("_binary_server_error_png_end");
extern const uint8_t wifi_unavailable_png_start[] asm("_binary_wifi_unavailable_png_start");
extern const uint8_t wifi_unavailable_png_end[] asm("_binary_wifi_unavailable_png_end");
extern const uint8_t unknown_error_png_start[] asm("_binary_unknown_error_png_start");
extern const uint8_t unknown_error_png_end[] asm("_binary_unknown_error_png_end");

namespace {
constexpr char TAG[] = "builtin_screen";

struct EmbeddedPng {
    const uint8_t* begin;
    const uint8_t* end;
};

EmbeddedPng get_embedded_png(BuiltinScreen kind) {
    switch (kind) {
    case BuiltinScreen::None:
        return {unknown_error_png_start, unknown_error_png_end};
    case BuiltinScreen::Updating:
        return {updating_png_start, updating_png_end};
    case BuiltinScreen::WifiUnavailable:
        return {wifi_unavailable_png_start, wifi_unavailable_png_end};
    case BuiltinScreen::ServerUnreachable:
        return {server_unreachable_png_start, server_unreachable_png_end};
    case BuiltinScreen::ServerUnexpectedResponse:
        return {server_error_png_start, server_error_png_end};
    case BuiltinScreen::ImageDecodeFailed:
        return {image_decode_failed_png_start, image_decode_failed_png_end};
    case BuiltinScreen::LowBattery:
        return {low_battery_png_start, low_battery_png_end};
    case BuiltinScreen::Unknown:
        return {unknown_error_png_start, unknown_error_png_end};
    }

    return {unknown_error_png_start, unknown_error_png_end};
}
} // namespace

esp_err_t show_builtin_screen(BuiltinScreen kind, bool fast) {
    const EmbeddedPng png = get_embedded_png(kind);
    const size_t size = static_cast<size_t>(png.end - png.begin);
    if (size == 0)
        return ESP_ERR_INVALID_SIZE;

    const DisplayInfo display = display_info();
    indexed_png_t image = {};
    esp_err_t err = decode_indexed_png(png.begin, size, &image, display.width, display.height);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error image failed to decode: %s", esp_err_to_name(err));
        return err;
    }

    err = display_render_bw(image.pixels, fast);
    free_indexed_png(&image);
    return err;
}
