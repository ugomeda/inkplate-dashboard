#include "error_screen.h"

#include <cstddef>
#include <cstdint>

#include "esp_log.h"
#include "png_decoder.h"
#include "spectra6.h"

// ESP-IDF EMBED_FILES places these compressed PNGs in flash (.rodata).
// Paths in symbol names match main/CMakeLists.txt.
extern const uint8_t error_wifi_png_start[] asm("_binary_error_wifi_png_start");
extern const uint8_t error_wifi_png_end[] asm("_binary_error_wifi_png_end");
extern const uint8_t error_http_png_start[] asm("_binary_error_http_png_start");
extern const uint8_t error_http_png_end[] asm("_binary_error_http_png_end");
extern const uint8_t error_image_png_start[] asm("_binary_error_image_png_start");
extern const uint8_t error_image_png_end[] asm("_binary_error_image_png_end");

namespace {
constexpr char TAG[] = "error_screen";
static_assert(Spectra6::WIDTH == 1200 && Spectra6::HEIGHT == 1600,
              "Error PNGs are generated for a native 1200x1600 panel");

struct EmbeddedPng {
    const uint8_t *begin;
    const uint8_t *end;
};

EmbeddedPng get_error_png(ErrorScreenKind kind) {
    switch (kind) {
        case ErrorScreenKind::Wifi:  return {error_wifi_png_start, error_wifi_png_end};
        case ErrorScreenKind::Http:  return {error_http_png_start, error_http_png_end};
        case ErrorScreenKind::Image: return {error_image_png_start, error_image_png_end};
    }
    return {error_http_png_start, error_http_png_end};
}
} // namespace

esp_err_t show_error_screen(ErrorScreenKind kind) {
    const EmbeddedPng png = get_error_png(kind);
    const size_t size = static_cast<size_t>(png.end - png.begin);
    if (size == 0) return ESP_ERR_INVALID_SIZE;

    indexed_png_t image = {};
    esp_err_t err = decode_indexed_png(png.begin, size, &image);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error image failed to decode: %s", esp_err_to_name(err));
        return err;
    }

    // The generated PNGs always use a 4-bit indexed palette:
    // index 0 = black (0x0); index 1 = white (0x1).
    uint8_t palette_to_hw[256] = {};
    if (image.width != 1200 || image.height != 1600 || image.bitdepth != 4 ||
        image.palette_size < 2 ||
        image.palette_rgb[0][0] != 2 || image.palette_rgb[0][1] != 2 ||
        image.palette_rgb[0][2] != 2 ||
        image.palette_rgb[1][0] != 190 || image.palette_rgb[1][1] != 200 ||
        image.palette_rgb[1][2] != 200) {
        ESP_LOGE(TAG, "Unexpected embedded error PNG palette/format");
        free_indexed_png(&image);
        return ESP_ERR_INVALID_RESPONSE;
    }
    palette_to_hw[0] = 0x0;
    palette_to_hw[1] = 0x1;

    Spectra6 display;
    err = display.init();
    if (err == ESP_OK) {
        err = display.show_indexed_image(image.pixels, image.bitdepth, palette_to_hw);
        const esp_err_t sleep_err = display.sleep();
        if (err == ESP_OK) err = sleep_err;
    }
    free_indexed_png(&image);
    return err;
}
