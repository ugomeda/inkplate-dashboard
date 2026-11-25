#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

struct DisplayInfo {
    uint32_t width;
    uint32_t height;
};

DisplayInfo display_info();
esp_err_t display_render(const uint8_t* pixels);
esp_err_t display_render_bw(const uint8_t* pixels, bool fast);
esp_err_t display_read_battery_mv(int* battery_mv);
void display_enter_deep_sleep(uint32_t seconds);
