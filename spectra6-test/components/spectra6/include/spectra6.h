#pragma once

#include "esp_err.h"
#include <cstdint>
#include <cstddef>

class Spectra6 {
public:
    static constexpr int WIDTH = 1200;
    static constexpr int HEIGHT = 1600;

    esp_err_t init();
    esp_err_t show_test_bars();

    // Raw, unfiltered PNG palette indices (4 or 8 bits per pixel).
    // palette_to_hw maps an index to a T133A01 color code.
    esp_err_t show_indexed_image(const uint8_t *pixels, unsigned bitdepth,
                                 const uint8_t palette_to_hw[256]);
    esp_err_t sleep();

private:
    enum class Chip { Master, Slave, Both };

    esp_err_t send_command(Chip chip, uint8_t command,
                           const uint8_t* data = nullptr, size_t length = 0);
    esp_err_t transmit(const uint8_t* data, size_t length);
    esp_err_t wait_ready(uint32_t timeout_ms);
    esp_err_t stream_test_half(Chip chip, int x_offset);
    esp_err_t stream_indexed_half(Chip chip, int x_offset,
                                  const uint8_t *pixels, unsigned bitdepth,
                                  const uint8_t palette_to_hw[256]);
    esp_err_t begin_image();
    esp_err_t refresh_image();

    void select(Chip chip);
    void deselect();

    void* spi_device_ = nullptr; // stored as opaque handle; cast internally
};
