#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Decoded pixel indices stay in PNG format: 4-bpp packed, or 8-bpp bytes.
// The caller owns pixels and must release them using free_indexed_png().
typedef struct {
    uint8_t *pixels;
    size_t size;
    uint32_t width;
    uint32_t height;
    uint8_t bitdepth;
    uint16_t palette_size;
    uint8_t palette_rgb[256][3];
} indexed_png_t;

esp_err_t decode_indexed_png(const uint8_t *data, size_t size,
                             indexed_png_t *result);
void free_indexed_png(indexed_png_t *image);

#ifdef __cplusplus
}
#endif

