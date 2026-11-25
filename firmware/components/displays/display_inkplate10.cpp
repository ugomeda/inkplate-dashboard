#include <cstddef>
#include <cstdint>

#include "Inkplate.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sleep.h"

#include "display.h"

namespace {
constexpr char TAG[] = "display_inkplate10";
constexpr gpio_num_t BUTTON_GPIO = GPIO_NUM_36;
constexpr int DISPLAY_WIDTH = 1200;
constexpr int DISPLAY_HEIGHT = 825;

class DashboardInkplate : public Inkplate {
public:
    DashboardInkplate()
        : Adafruit_GFX(E_INK_WIDTH, E_INK_HEIGHT) {}

    void load_monochrome(const uint8_t* pixels) {
        constexpr int ROW_BYTES = DISPLAY_WIDTH / 8;

        for (int y = 0; y < DISPLAY_HEIGHT; ++y) {
            const uint8_t* src = pixels + y * DISPLAY_WIDTH;
            uint8_t* dst = m_newFramebuffer + y * ROW_BYTES;
            uint8_t* previous = m_framebuffer + y * ROW_BYTES;

            for (int x = 0; x < ROW_BYTES; ++x) {
                uint8_t packed = 0;

                for (int bit = 0; bit < 8; ++bit) {
                    packed |= uint8_t(src[x * 8 + bit] == 0) << bit;
                }

                dst[x] = packed;
                previous[x] = ~packed;
            }
        }
    }

    void load_grayscale(const uint8_t* pixels) {
        constexpr size_t PIXEL_COUNT = DISPLAY_WIDTH * DISPLAY_HEIGHT;

        for (size_t i = 0; i < PIXEL_COUNT; i += 2) {
            m_framebufferColor[i / 2] = ((pixels[i] & 0x07) << 4) | (pixels[i + 1] & 0x07);
        }
    }
};

DashboardInkplate& get_display() {
    static DashboardInkplate display;
    return display;
}
} // namespace

DisplayInfo display_info() {
    return {DISPLAY_WIDTH, DISPLAY_HEIGHT};
}

esp_err_t display_render_bw(const uint8_t* pixels, bool fast) {
    if (!pixels)
        return ESP_ERR_INVALID_ARG;

    DashboardInkplate& display = get_display();
    display.setDisplayMode(BLACK_AND_WHITE);
    display.load_monochrome(pixels);

    if (fast) {
        ESP_LOGI(TAG, "Launching partial update");
        display.partialUpdate(true);

        return ESP_OK;
    } else {
        return display.display();
    }
}

esp_err_t display_render(const uint8_t* pixels) {
    if (!pixels)
        return ESP_ERR_INVALID_ARG;

    DashboardInkplate& display = get_display();
    display.setDisplayMode(GRAYSCALE);
    display.load_grayscale(pixels);

    ESP_LOGI(TAG, "Launching full update");
    return display.display();
}

esp_err_t display_read_battery_mv(int* battery_mv) {
    DashboardInkplate& display = get_display();
    double battery_volts = display.readBattery();
    *battery_mv = static_cast<int>(battery_volts * 1000);

    return ESP_OK;
}

void display_enter_deep_sleep(uint32_t seconds) {
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(seconds) * 1000000ULL));
    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(BUTTON_GPIO, 0));
    esp_deep_sleep_start();
}
