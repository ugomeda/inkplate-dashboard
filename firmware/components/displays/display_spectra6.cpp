
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display.h"

namespace {
constexpr char TAG[] = "spectra6";

constexpr int DISPLAY_WIDTH = 1200;
constexpr int DISPLAY_HEIGHT = 1600;
constexpr size_t HALF_ROW_BYTES = DISPLAY_WIDTH / 4;
enum class Chip { Master, Slave, Both };

// Seeed EE02 / XIAO ESP32-S3 Plus wiring.
// Ref: https://github.com/philippwaller/esphome-epaper-spectra6-133/blob/main/packages/boards/seeed_studio/xiao_ee02.yaml
static constexpr int EPD_SCLK = 7;
static constexpr int EPD_MOSI = 9;
static constexpr int EPD_CS_MASTER = 44;
static constexpr int EPD_CS_SLAVE = 41;
static constexpr int EPD_DC = 10;
static constexpr int EPD_RESET = 38;
static constexpr int EPD_BUSY = 4;
static constexpr int EPD_POWER = 43;

constexpr gpio_num_t REFRESH_BUTTON_GPIO = GPIO_NUM_2; // EE02 button 1: active-low
constexpr gpio_num_t BATTERY_ENABLE_GPIO = GPIO_NUM_6; // EE02 ADC_EN, active-high
constexpr adc_unit_t BATTERY_ADC_UNIT = ADC_UNIT_1;
constexpr adc_channel_t BATTERY_ADC_CHANNEL = ADC_CHANNEL_0; // GPIO1
constexpr adc_atten_t BATTERY_ADC_ATTEN = ADC_ATTEN_DB_12;
constexpr int BATTERY_DIVIDER_RATIO = 2; // EE02 R28=R29=10k

constexpr spi_host_device_t SPI_HOST = SPI2_HOST;
constexpr int SPI_CLOCK_HZ = 8 * 1000 * 1000;

// Controller command bytes derived from the Seeed_GFX T133A01 driver.
// Source: https://github.com/Seeed-Studio/Seeed_GFX/blob/master/TFT_Drivers/T133A01_Defines.h
constexpr uint8_t PON = 0x04;
constexpr uint8_t DRF = 0x12;
constexpr uint8_t POF = 0x02;
constexpr uint8_t DTM = 0x10;

spi_device_handle_t spi_device = nullptr;

esp_err_t output_pin(int pin, int initial_level) {
    ESP_RETURN_ON_ERROR(gpio_reset_pin(static_cast<gpio_num_t>(pin)), TAG, "GPIO reset failed");
    ESP_RETURN_ON_ERROR(gpio_set_direction(static_cast<gpio_num_t>(pin), GPIO_MODE_OUTPUT), TAG, "GPIO direction failed");
    return gpio_set_level(static_cast<gpio_num_t>(pin), initial_level);
}

void spectra6_select(Chip chip) {
    gpio_set_level(static_cast<gpio_num_t>(EPD_CS_MASTER), chip == Chip::Slave);
    gpio_set_level(static_cast<gpio_num_t>(EPD_CS_SLAVE), chip == Chip::Master);
}

void spectra6_deselect() {
    gpio_set_level(static_cast<gpio_num_t>(EPD_CS_MASTER), 1);
    gpio_set_level(static_cast<gpio_num_t>(EPD_CS_SLAVE), 1);
}

esp_err_t spectra6_transmit(const uint8_t* data, size_t length) {
    if (!length)
        return ESP_OK;
    spi_transaction_t transaction = {};
    transaction.length = length * 8;
    if (length <= sizeof(transaction.tx_data)) {
        transaction.flags = SPI_TRANS_USE_TXDATA;
        std::memcpy(transaction.tx_data, data, length);
    } else {
        transaction.tx_buffer = data;
    }
    return spi_device_transmit(static_cast<spi_device_handle_t>(spi_device), &transaction);
}

esp_err_t spectra6_send_command(Chip chip, uint8_t command, const uint8_t* data = nullptr, size_t length = 0) {
    spectra6_select(chip);
    gpio_set_level(static_cast<gpio_num_t>(EPD_DC), 0);
    esp_err_t err = spectra6_transmit(&command, 1);
    if (err == ESP_OK && length) {
        gpio_set_level(static_cast<gpio_num_t>(EPD_DC), 1);
        err = spectra6_transmit(data, length);
    }
    spectra6_deselect();
    return err;
}

esp_err_t spectra6_wait_ready(uint32_t timeout_ms) {
    const int64_t deadline = esp_timer_get_time() + static_cast<int64_t>(timeout_ms) * 1000;
    // Seeed's CHECK_BUSY waits for HIGH (= ready).
    while (gpio_get_level(static_cast<gpio_num_t>(EPD_BUSY)) == 0) {
        if (esp_timer_get_time() >= deadline) {
            ESP_LOGE(TAG, "BUSY timeout after %u ms", static_cast<unsigned>(timeout_ms));
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ESP_OK;
}

esp_err_t spectra6_init() {
    // GPIO43 is also UART0 TX: use USB Serial/JTAG for the console.
    ESP_RETURN_ON_ERROR(output_pin(EPD_CS_MASTER, 1), TAG, "CS0");
    ESP_RETURN_ON_ERROR(output_pin(EPD_CS_SLAVE, 1), TAG, "CS1");
    ESP_RETURN_ON_ERROR(output_pin(EPD_DC, 1), TAG, "DC");
    ESP_RETURN_ON_ERROR(output_pin(EPD_RESET, 1), TAG, "RESET");
    ESP_RETURN_ON_ERROR(output_pin(EPD_POWER, 1), TAG, "POWER");
    ESP_RETURN_ON_ERROR(gpio_set_direction(static_cast<gpio_num_t>(EPD_BUSY), GPIO_MODE_INPUT), TAG, "BUSY");
    vTaskDelay(pdMS_TO_TICKS(100));

    spi_bus_config_t bus = {};
    bus.mosi_io_num = EPD_MOSI;
    bus.miso_io_num = -1; // MISO unused by this write-only driver
    bus.sclk_io_num = EPD_SCLK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 4096;
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "SPI bus init");

    spi_device_interface_config_t device = {};
    device.clock_speed_hz = SPI_CLOCK_HZ;
    device.mode = 0;
    device.spics_io_num = -1; // manually control BOTH CS pins
    device.queue_size = 1;
    spi_device_handle_t handle = nullptr;
    ESP_RETURN_ON_ERROR(spi_bus_add_device(SPI_HOST, &device, &handle), TAG, "SPI add device");
    spi_device = handle;

    gpio_set_level(static_cast<gpio_num_t>(EPD_RESET), 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(static_cast<gpio_num_t>(EPD_RESET), 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(spectra6_wait_ready(5000), TAG, "Panel not ready after reset");

    // T133A01 initialization in the order used by Seeed_GFX.
    // Chip::Both corresponds to the Seeed code holding CS1 LOW while
    // its SPI writer also asserts CS0. Keep this sequence unchanged initially.
    auto cmd = [](Chip chip, uint8_t reg, std::initializer_list<uint8_t> values) -> esp_err_t {
        return spectra6_send_command(chip, reg, values.begin(), values.size());
    };

    ESP_RETURN_ON_ERROR(cmd(Chip::Master, 0x74, {0x00, 0x0C, 0x0C, 0xD9, 0xDD, 0xDD, 0x15, 0x15, 0x55}), TAG, "0x74");
    ESP_RETURN_ON_ERROR(cmd(Chip::Both, 0xF0, {0x49, 0x55, 0x13, 0x5D, 0x05, 0x10}), TAG, "0xF0");
    ESP_RETURN_ON_ERROR(cmd(Chip::Both, 0x00, {0xDF, 0x69}), TAG, "PSR");
    ESP_RETURN_ON_ERROR(cmd(Chip::Both, 0x30, {0x08}), TAG, "PLL");
    ESP_RETURN_ON_ERROR(cmd(Chip::Master, 0xA5, {0x44, 0x54, 0x00}), TAG, "DCDC");
    ESP_RETURN_ON_ERROR(cmd(Chip::Both, 0x50, {0x37}), TAG, "CDI");
    ESP_RETURN_ON_ERROR(cmd(Chip::Both, 0x60, {0x03, 0x03}), TAG, "0x60");
    ESP_RETURN_ON_ERROR(cmd(Chip::Both, 0x86, {0x10}), TAG, "0x86");
    ESP_RETURN_ON_ERROR(cmd(Chip::Both, 0xE3, {0x22}), TAG, "PWS");
    ESP_RETURN_ON_ERROR(cmd(Chip::Both, 0x61, {0x04, 0xB0, 0x03, 0x20}), TAG, "TRES");
    ESP_RETURN_ON_ERROR(cmd(Chip::Master, 0x01, {0x0F, 0x00, 0x28, 0x2C, 0x28, 0x38}), TAG, "PWR");
    ESP_RETURN_ON_ERROR(cmd(Chip::Master, 0xB6, {0x07}), TAG, "0xB6");
    ESP_RETURN_ON_ERROR(cmd(Chip::Master, 0x06, {0xE0, 0x20}), TAG, "BTST_P");
    ESP_RETURN_ON_ERROR(cmd(Chip::Master, 0xB7, {0x01}), TAG, "0xB7");
    ESP_RETURN_ON_ERROR(cmd(Chip::Master, 0x05, {0xE0, 0x20}), TAG, "BTST_N");
    ESP_RETURN_ON_ERROR(cmd(Chip::Master, 0xB0, {0x01}), TAG, "0xB0");
    ESP_RETURN_ON_ERROR(cmd(Chip::Master, 0xB1, {0x02}), TAG, "0xB1");

    ESP_LOGI(TAG, "Panel initialized");
    return ESP_OK;
}

esp_err_t spectra6_begin_image() {
    constexpr uint8_t ccset[] = {0x01};
    ESP_RETURN_ON_ERROR(spectra6_send_command(Chip::Both, 0xE0, ccset, sizeof(ccset)), TAG, "CCSET");
    ESP_RETURN_ON_ERROR(spectra6_wait_ready(5000), TAG, "BUSY before DTM");
    vTaskDelay(pdMS_TO_TICKS(10));

    return ESP_OK;
}

esp_err_t spectra6_refresh_image() {
    // Preserve the working color-bar power / refresh sequence.
    ESP_LOGI(TAG, "Panel power on");
    ESP_RETURN_ON_ERROR(spectra6_send_command(Chip::Both, PON), TAG, "PON");
    ESP_RETURN_ON_ERROR(spectra6_wait_ready(60000), TAG, "BUSY after PON");
    vTaskDelay(pdMS_TO_TICKS(30));

    constexpr uint8_t refresh_data[] = {0x00};
    ESP_LOGI(TAG, "Refreshing display");
    ESP_RETURN_ON_ERROR(spectra6_send_command(Chip::Both, DRF, refresh_data, sizeof(refresh_data)), TAG, "DRF");
    ESP_RETURN_ON_ERROR(spectra6_wait_ready(60000), TAG, "BUSY after DRF");
    vTaskDelay(pdMS_TO_TICKS(30));

    constexpr uint8_t power_off_data[] = {0x00};
    ESP_RETURN_ON_ERROR(spectra6_send_command(Chip::Both, POF, power_off_data, sizeof(power_off_data)), TAG, "POF");
    ESP_RETURN_ON_ERROR(spectra6_wait_ready(30000), TAG, "BUSY after POF");
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_LOGI(TAG, "Refresh completed");

    return ESP_OK;
}

esp_err_t spectra6_stream_indexed_half(Chip chip, int x_offset, const uint8_t* pixels) {
    auto* row = static_cast<uint8_t*>(heap_caps_malloc(HALF_ROW_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!row)
        return ESP_ERR_NO_MEM;

    spectra6_select(chip);
    gpio_set_level(static_cast<gpio_num_t>(EPD_DC), 0);
    constexpr uint8_t command = DTM;
    esp_err_t err = spectra6_transmit(&command, 1);
    gpio_set_level(static_cast<gpio_num_t>(EPD_DC), 1);

    for (int y = 0; err == ESP_OK && y < DISPLAY_HEIGHT; ++y) {
        const size_t start = static_cast<size_t>(y) * DISPLAY_WIDTH + x_offset;
        for (size_t i = 0; i < HALF_ROW_BYTES; ++i) {
            row[i] = (pixels[start + 2 * i] << 4) | pixels[start + 2 * i + 1];
        }
        err = spectra6_transmit(row, HALF_ROW_BYTES);
    }

    spectra6_deselect();
    heap_caps_free(row);

    return err;
}

esp_err_t spectra6_show_image(const uint8_t* pixels) {
    ESP_RETURN_ON_ERROR(spectra6_begin_image(), TAG, "Prepare image");
    ESP_RETURN_ON_ERROR(spectra6_stream_indexed_half(Chip::Master, 0, pixels), TAG, "Master image write");
    ESP_RETURN_ON_ERROR(spectra6_stream_indexed_half(Chip::Slave, DISPLAY_WIDTH / 2, pixels), TAG, "Slave image write");
    return spectra6_refresh_image();
}

void spectra6_cleanup() {
    if (spi_device) {
        spi_bus_remove_device(spi_device);
        spi_device = nullptr;
    }

    spi_bus_free(SPI_HOST);
}

esp_err_t spectra6_sleep() {
    constexpr uint8_t sleep_data[] = {0xA5};
    const esp_err_t err = spectra6_send_command(Chip::Master, 0x07, sleep_data, sizeof(sleep_data));
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    const esp_err_t power_err = gpio_set_level(static_cast<gpio_num_t>(EPD_POWER), 0);

    return err != ESP_OK ? err : power_err;
}
} // namespace

DisplayInfo display_info() {
    return {1200, 1600};
}

esp_err_t display_render(const uint8_t* pixels) {
    if (!pixels) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(spectra6_init(), TAG, "Display initialization failed");
    const esp_err_t render_err = spectra6_show_image(pixels);
    const esp_err_t sleep_err = spectra6_sleep();
    spectra6_cleanup();

    return render_err != ESP_OK ? render_err : sleep_err;
}

esp_err_t display_render_bw(const uint8_t* pixels, bool fast) {
    if (fast) { // not capable of fast refresh
        return ESP_OK;
    }

    return display_render(pixels);
}

esp_err_t display_read_battery_mv(int* battery_mv) {
    if (!battery_mv)
        return ESP_ERR_INVALID_ARG;

    gpio_config_t gpio_cfg = {};
    gpio_cfg.pin_bit_mask = 1ULL << BATTERY_ENABLE_GPIO;
    gpio_cfg.mode = GPIO_MODE_OUTPUT;

    ESP_RETURN_ON_ERROR(gpio_config(&gpio_cfg), TAG, "Battery GPIO configuration failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(BATTERY_ENABLE_GPIO, 0), TAG, "Battery GPIO initialization failed");

    adc_oneshot_unit_handle_t adc = nullptr;
    adc_cali_handle_t calibration = nullptr;

    const auto measure = [&]() -> esp_err_t {
        adc_oneshot_unit_init_cfg_t unit_cfg = {};
        unit_cfg.unit_id = BATTERY_ADC_UNIT;
        ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_cfg, &adc), TAG, "ADC initialization failed");

        adc_oneshot_chan_cfg_t channel_cfg = {};
        channel_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
        channel_cfg.atten = BATTERY_ADC_ATTEN;
        ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(adc, BATTERY_ADC_CHANNEL, &channel_cfg), TAG,
                            "ADC channel configuration failed");

        adc_cali_curve_fitting_config_t cal_cfg = {};
        cal_cfg.unit_id = BATTERY_ADC_UNIT;
        cal_cfg.atten = BATTERY_ADC_ATTEN;
        cal_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
        ESP_RETURN_ON_ERROR(adc_cali_create_scheme_curve_fitting(&cal_cfg, &calibration), TAG, "ADC calibration failed");

        ESP_RETURN_ON_ERROR(gpio_set_level(BATTERY_ENABLE_GPIO, 1), TAG, "Battery divider enable failed");
        vTaskDelay(pdMS_TO_TICKS(20));

        constexpr int SAMPLES = 8;
        int total_mv = 0;

        for (int i = 0; i < SAMPLES; ++i) {
            int sample_mv = 0;
            ESP_RETURN_ON_ERROR(adc_oneshot_get_calibrated_result(adc, calibration, BATTERY_ADC_CHANNEL, &sample_mv), TAG,
                                "Battery ADC reading failed");
            total_mv += sample_mv;
            vTaskDelay(pdMS_TO_TICKS(2));
        }

        const int voltage_mv = total_mv * BATTERY_DIVIDER_RATIO / SAMPLES;

        if (voltage_mv < 2500 || voltage_mv > 4500) {
            ESP_LOGW(TAG, "Implausible battery voltage: %d mV", voltage_mv);
            return ESP_ERR_INVALID_RESPONSE;
        }

        *battery_mv = voltage_mv;
        return ESP_OK;
    };

    const esp_err_t err = measure();

    // Always disable the voltage divider, even on failure.
    const esp_err_t disable_err = gpio_set_level(BATTERY_ENABLE_GPIO, 0);

    if (calibration) {
        adc_cali_delete_scheme_curve_fitting(calibration);
    }
    if (adc) {
        adc_oneshot_del_unit(adc);
    }

    return err != ESP_OK ? err : disable_err;
}

void display_enter_deep_sleep(uint32_t seconds) {
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(seconds) * 1000000ULL));
    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(REFRESH_BUTTON_GPIO, 0));
    esp_deep_sleep_start();
}