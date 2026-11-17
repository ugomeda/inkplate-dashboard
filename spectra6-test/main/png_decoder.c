#include "png_decoder.h"

#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "png.h"

static const char *TAG = "png_decoder";

#define IMAGE_WIDTH  1200U
#define IMAGE_HEIGHT 1600U

typedef struct {
    png_structp png;
    png_infop info;
    const uint8_t *input;
    size_t input_size;
    size_t offset;
    uint8_t *pixels;
} decode_state_t;

static void read_memory(png_structp png, png_bytep destination,
                        png_size_t length) {
    decode_state_t *state = (decode_state_t *)png_get_io_ptr(png);
    if (!state || length > state->input_size - state->offset) {
        png_error(png, "Truncated PNG");
        return;
    }
    memcpy(destination, state->input + state->offset, length);
    state->offset += length;
}

void free_indexed_png(indexed_png_t *image) {
    if (!image) return;
    heap_caps_free(image->pixels);
    memset(image, 0, sizeof(*image));
}

esp_err_t decode_indexed_png(const uint8_t *data, size_t size,
                             indexed_png_t *result) {
    if (!data || !result || size < 8 || png_sig_cmp((png_const_bytep)data, 0, 8)) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(result, 0, sizeof(*result));

    // Heap-allocate the state so values survive libpng's longjmp error path.
    decode_state_t *state = (decode_state_t *)calloc(1, sizeof(*state));
    if (!state) return ESP_ERR_NO_MEM;
    state->input = data;
    state->input_size = size;

    esp_err_t status = ESP_FAIL;
    state->png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!state->png) goto cleanup;
    state->info = png_create_info_struct(state->png);
    if (!state->info) goto cleanup;

    if (setjmp(png_jmpbuf(state->png))) {
        ESP_LOGE(TAG, "PNG decode failed (invalid or truncated image)");
        goto cleanup;
    }

    png_set_read_fn(state->png, state, read_memory);
    png_set_user_limits(state->png, IMAGE_WIDTH, IMAGE_HEIGHT);
    png_set_crc_action(state->png, PNG_CRC_ERROR_QUIT, PNG_CRC_ERROR_QUIT);
    png_read_info(state->png, state->info);

    png_uint_32 width = 0, height = 0;
    int bitdepth = 0, color_type = 0, interlace = 0;
    if (!png_get_IHDR(state->png, state->info, &width, &height,
                      &bitdepth, &color_type, &interlace, NULL, NULL)) {
        status = ESP_ERR_INVALID_ARG;
        goto cleanup;
    }

    // The server outputs a native-orientation, non-interlaced palette PNG.
    // Don't expand to RGB, apply gamma, or unpack 4-bit indices.
    if (width != IMAGE_WIDTH || height != IMAGE_HEIGHT ||
        color_type != PNG_COLOR_TYPE_PALETTE ||
        (bitdepth != 4 && bitdepth != 8) ||
        interlace != PNG_INTERLACE_NONE ||
        png_get_valid(state->png, state->info, PNG_INFO_tRNS)) {
        ESP_LOGE(TAG, "Expected 1200x1600 indexed 4/8-bit non-interlaced PNG");
        status = ESP_ERR_NOT_SUPPORTED;
        goto cleanup;
    }

    png_colorp palette = NULL;
    int palette_count = 0;
    if (!png_get_PLTE(state->png, state->info, &palette, &palette_count) ||
        palette_count < 1 || palette_count > (1 << bitdepth)) {
        status = ESP_ERR_INVALID_ARG;
        goto cleanup;
    }

    png_read_update_info(state->png, state->info);
    const size_t rowbytes = png_get_rowbytes(state->png, state->info);
    const size_t expected_rowbytes = ((size_t)width * (size_t)bitdepth + 7) / 8;
    if (rowbytes != expected_rowbytes) {
        ESP_LOGE(TAG, "Unexpected PNG row size: %u", (unsigned)rowbytes);
        status = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }

    const size_t output_size = rowbytes * height;
    state->pixels = (uint8_t *)heap_caps_malloc(
        output_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!state->pixels) {
        status = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    for (png_uint_32 y = 0; y < height; ++y) {
        png_read_row(state->png, state->pixels + (size_t)y * rowbytes, NULL);
    }
    png_read_end(state->png, NULL);

    result->width = width;
    result->height = height;
    result->bitdepth = (uint8_t)bitdepth;
    result->size = output_size;
    result->palette_size = (uint16_t)palette_count;
    for (int i = 0; i < palette_count; ++i) {
        result->palette_rgb[i][0] = palette[i].red;
        result->palette_rgb[i][1] = palette[i].green;
        result->palette_rgb[i][2] = palette[i].blue;
    }
    result->pixels = state->pixels;
    state->pixels = NULL;
    status = ESP_OK;

cleanup:
    if (state->png) {
        png_destroy_read_struct(&state->png, &state->info, NULL);
    }
    heap_caps_free(state->pixels);
    free(state);
    return status;
}
