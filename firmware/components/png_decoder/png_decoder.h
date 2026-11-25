#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t* pixels;
    size_t size;
    uint32_t width;
    uint32_t height;
    uint16_t palette_size;
} indexed_png_t;

esp_err_t decode_indexed_png(const uint8_t* data, size_t size, indexed_png_t* result, uint32_t display_width, uint32_t display_height);
void free_indexed_png(indexed_png_t* image);

#ifdef __cplusplus
}
#endif
