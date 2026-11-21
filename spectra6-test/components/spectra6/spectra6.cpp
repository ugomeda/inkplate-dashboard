#include "spectra6.h"
#include "ee02_pins.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr char TAG[] = "spectra6";
constexpr spi_host_device_t SPI_HOST = SPI2_HOST;
constexpr int SPI_CLOCK_HZ = 8 * 1000 * 1000; // conservative bring-up speed
constexpr size_t HALF_ROW_BYTES = Spectra6::WIDTH / 4; // 600 pixels at 4 bpp

// Controller command bytes derived from the Seeed_GFX T133A01 driver.
// Source: https://github.com/Seeed-Studio/Seeed_GFX/blob/master/TFT_Drivers/T133A01_Defines.h
// Preserve applicable upstream copyright and license notices when redistributing.
constexpr uint8_t PON = 0x04;
constexpr uint8_t DRF = 0x12;
constexpr uint8_t POF = 0x02;
constexpr uint8_t DTM = 0x10;


esp_err_t output_pin(int pin, int initial_level) {
    ESP_RETURN_ON_ERROR(gpio_reset_pin(static_cast<gpio_num_t>(pin)), TAG,
                        "GPIO reset failed");
    ESP_RETURN_ON_ERROR(gpio_set_direction(static_cast<gpio_num_t>(pin), GPIO_MODE_OUTPUT),
                        TAG, "GPIO direction failed");
    return gpio_set_level(static_cast<gpio_num_t>(pin), initial_level);
}
} // namespace

void Spectra6::select(Chip chip) {
    gpio_set_level(static_cast<gpio_num_t>(EPD_CS_MASTER), chip == Chip::Slave);
    gpio_set_level(static_cast<gpio_num_t>(EPD_CS_SLAVE), chip == Chip::Master);
}

void Spectra6::deselect() {
    gpio_set_level(static_cast<gpio_num_t>(EPD_CS_MASTER), 1);
    gpio_set_level(static_cast<gpio_num_t>(EPD_CS_SLAVE), 1);
}

esp_err_t Spectra6::transmit(const uint8_t* data, size_t length) {
    if (!length) return ESP_OK;
    spi_transaction_t transaction = {};
    transaction.length = length * 8;
    if (length <= sizeof(transaction.tx_data)) {
        transaction.flags = SPI_TRANS_USE_TXDATA;
        std::memcpy(transaction.tx_data, data, length);
    } else {
        transaction.tx_buffer = data;
    }
    return spi_device_transmit(static_cast<spi_device_handle_t>(spi_device_), &transaction);
}

esp_err_t Spectra6::send_command(Chip chip, uint8_t command,
                                 const uint8_t* data, size_t length) {
    select(chip);
    gpio_set_level(static_cast<gpio_num_t>(EPD_DC), 0);
    esp_err_t err = transmit(&command, 1);
    if (err == ESP_OK && length) {
        gpio_set_level(static_cast<gpio_num_t>(EPD_DC), 1);
        err = transmit(data, length);
    }
    deselect();
    return err;
}

esp_err_t Spectra6::wait_ready(uint32_t timeout_ms) {
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

esp_err_t Spectra6::init() {
    // GPIO43 is also UART0 TX: use USB Serial/JTAG for the console.
    ESP_RETURN_ON_ERROR(output_pin(EPD_CS_MASTER, 1), TAG, "CS0");
    ESP_RETURN_ON_ERROR(output_pin(EPD_CS_SLAVE, 1), TAG, "CS1");
    ESP_RETURN_ON_ERROR(output_pin(EPD_DC, 1), TAG, "DC");
    ESP_RETURN_ON_ERROR(output_pin(EPD_RESET, 1), TAG, "RESET");
    ESP_RETURN_ON_ERROR(output_pin(EPD_POWER, 1), TAG, "POWER");
    ESP_RETURN_ON_ERROR(gpio_set_direction(static_cast<gpio_num_t>(EPD_BUSY), GPIO_MODE_INPUT),
                        TAG, "BUSY");
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
    spi_device_ = handle;

    gpio_set_level(static_cast<gpio_num_t>(EPD_RESET), 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(static_cast<gpio_num_t>(EPD_RESET), 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(wait_ready(5000), TAG, "Panel not ready after reset");

    // T133A01 initialization in the order used by Seeed_GFX.
    // Chip::Both corresponds to the Seeed code holding CS1 LOW while
    // its SPI writer also asserts CS0. Keep this sequence unchanged initially.
    auto cmd = [this](Chip chip, uint8_t reg,
                      std::initializer_list<uint8_t> values) -> esp_err_t {
        return send_command(chip, reg, values.begin(), values.size());
    };

    ESP_RETURN_ON_ERROR(cmd(Chip::Master, 0x74, {0x00,0x0C,0x0C,0xD9,0xDD,0xDD,0x15,0x15,0x55}), TAG, "0x74");
    ESP_RETURN_ON_ERROR(cmd(Chip::Both,   0xF0, {0x49,0x55,0x13,0x5D,0x05,0x10}), TAG, "0xF0");
    ESP_RETURN_ON_ERROR(cmd(Chip::Both,   0x00, {0xDF,0x69}), TAG, "PSR");
    ESP_RETURN_ON_ERROR(cmd(Chip::Both,   0x30, {0x08}), TAG, "PLL");
    ESP_RETURN_ON_ERROR(cmd(Chip::Master, 0xA5, {0x44,0x54,0x00}), TAG, "DCDC");
    ESP_RETURN_ON_ERROR(cmd(Chip::Both,   0x50, {0x37}), TAG, "CDI");
    ESP_RETURN_ON_ERROR(cmd(Chip::Both,   0x60, {0x03,0x03}), TAG, "0x60");
    ESP_RETURN_ON_ERROR(cmd(Chip::Both,   0x86, {0x10}), TAG, "0x86");
    ESP_RETURN_ON_ERROR(cmd(Chip::Both,   0xE3, {0x22}), TAG, "PWS");
    ESP_RETURN_ON_ERROR(cmd(Chip::Both,   0x61, {0x04,0xB0,0x03,0x20}), TAG, "TRES");
    ESP_RETURN_ON_ERROR(cmd(Chip::Master, 0x01, {0x0F,0x00,0x28,0x2C,0x28,0x38}), TAG, "PWR");
    ESP_RETURN_ON_ERROR(cmd(Chip::Master, 0xB6, {0x07}), TAG, "0xB6");
    ESP_RETURN_ON_ERROR(cmd(Chip::Master, 0x06, {0xE0,0x20}), TAG, "BTST_P");
    ESP_RETURN_ON_ERROR(cmd(Chip::Master, 0xB7, {0x01}), TAG, "0xB7");
    ESP_RETURN_ON_ERROR(cmd(Chip::Master, 0x05, {0xE0,0x20}), TAG, "BTST_N");
    ESP_RETURN_ON_ERROR(cmd(Chip::Master, 0xB0, {0x01}), TAG, "0xB0");
    ESP_RETURN_ON_ERROR(cmd(Chip::Master, 0xB1, {0x02}), TAG, "0xB1");

    ESP_LOGI(TAG, "Panel initialized");
    return ESP_OK;
}

esp_err_t Spectra6::stream_test_half(Chip chip, int x_offset) {
    // One DMA-safe row buffer. The full 960 KB framebuffer isn't necessary
    // for the first bring-up test.
    auto* row = static_cast<uint8_t*>(
        heap_caps_malloc(HALF_ROW_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!row) return ESP_ERR_NO_MEM;

    // T133A01 hardware color codes: black, white, yellow, red, blue, green.
    constexpr uint8_t colors[6] = {0x0, 0x1, 0x2, 0x3, 0x5, 0x6};

    select(chip);
    gpio_set_level(static_cast<gpio_num_t>(EPD_DC), 0);
    constexpr uint8_t DTM_CMD = DTM;
    esp_err_t err = transmit(&DTM_CMD, 1);
    gpio_set_level(static_cast<gpio_num_t>(EPD_DC), 1);

    for (int y = 0; err == ESP_OK && y < HEIGHT; ++y) {
        for (int i = 0; i < static_cast<int>(HALF_ROW_BYTES); ++i) {
            const int x0 = x_offset + i * 2;
            const int x1 = x0 + 1;
            const uint8_t c0 = colors[x0 * 6 / WIDTH];
            const uint8_t c1 = colors[x1 * 6 / WIDTH];
            row[i] = (c0 << 4) | c1;
        }
        err = transmit(row, HALF_ROW_BYTES);
    }

    deselect();
    heap_caps_free(row);
    return err;
}

esp_err_t Spectra6::begin_image() {
    constexpr uint8_t ccset[] = {0x01};
    ESP_RETURN_ON_ERROR(send_command(Chip::Both, 0xE0, ccset, sizeof(ccset)), TAG, "CCSET");
    ESP_RETURN_ON_ERROR(wait_ready(5000), TAG, "BUSY before DTM");
    vTaskDelay(pdMS_TO_TICKS(10));

    return ESP_OK;
}

esp_err_t Spectra6::refresh_image() {
    // Preserve the working color-bar power / refresh sequence.
    ESP_LOGI(TAG, "Panel power on");
    ESP_RETURN_ON_ERROR(send_command(Chip::Both, PON), TAG, "PON");
    ESP_RETURN_ON_ERROR(wait_ready(60000), TAG, "BUSY after PON");
    vTaskDelay(pdMS_TO_TICKS(30));

    constexpr uint8_t refresh_data[] = {0x00};
    ESP_LOGI(TAG, "Refreshing (~30s)");
    ESP_RETURN_ON_ERROR(send_command(Chip::Both, DRF, refresh_data, sizeof(refresh_data)), TAG, "DRF");
    ESP_RETURN_ON_ERROR(wait_ready(60000), TAG, "BUSY after DRF");
    vTaskDelay(pdMS_TO_TICKS(30));

    constexpr uint8_t power_off_data[] = {0x00};
    ESP_RETURN_ON_ERROR(send_command(Chip::Both, POF, power_off_data, sizeof(power_off_data)), TAG, "POF");
    ESP_RETURN_ON_ERROR(wait_ready(30000), TAG, "BUSY after POF");
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_LOGI(TAG, "Refresh completed");

    return ESP_OK;
}

esp_err_t Spectra6::stream_indexed_half(
    Chip chip, int x_offset, const uint8_t *pixels, unsigned bitdepth,
    const uint8_t palette_to_hw[256]) {
    auto *row = static_cast<uint8_t *>(
        heap_caps_malloc(HALF_ROW_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!row) return ESP_ERR_NO_MEM;

    select(chip);
    gpio_set_level(static_cast<gpio_num_t>(EPD_DC), 0);
    constexpr uint8_t command = DTM;
    esp_err_t err = transmit(&command, 1);
    gpio_set_level(static_cast<gpio_num_t>(EPD_DC), 1);

    for (int y = 0; err == ESP_OK && y < HEIGHT; ++y) {
        if (bitdepth == 4) {
            // LodePNG emits packed 4-bit indices, high nibble first,
            // without per-scanline padding. WIDTH and x_offset are even.
            const size_t start = static_cast<size_t>(y) * (WIDTH / 2) + x_offset / 2;
            for (size_t i = 0; i < HALF_ROW_BYTES; ++i) {
                const uint8_t value = pixels[start + i];
                row[i] = (palette_to_hw[value >> 4] << 4) |
                          palette_to_hw[value & 0x0F];
            }
        } else {
            const size_t start = static_cast<size_t>(y) * WIDTH + x_offset;
            for (size_t i = 0; i < HALF_ROW_BYTES; ++i) {
                row[i] = (palette_to_hw[pixels[start + 2 * i]] << 4) |
                          palette_to_hw[pixels[start + 2 * i + 1]];
            }
        }
        err = transmit(row, HALF_ROW_BYTES);
    }

    deselect();
    heap_caps_free(row);
    return err;
}

esp_err_t Spectra6::show_indexed_image(
    const uint8_t *pixels, unsigned bitdepth, const uint8_t palette_to_hw[256]) {
    if (!pixels || !palette_to_hw || (bitdepth != 4 && bitdepth != 8)) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(begin_image(), TAG, "Prepare image");
    ESP_LOGI(TAG, "Writing indexed master half");
    ESP_RETURN_ON_ERROR(stream_indexed_half(Chip::Master, 0, pixels, bitdepth, palette_to_hw),
                        TAG, "Master image write");
    ESP_LOGI(TAG, "Writing indexed slave half");
    ESP_RETURN_ON_ERROR(stream_indexed_half(Chip::Slave, WIDTH / 2, pixels, bitdepth, palette_to_hw),
                        TAG, "Slave image write");
    return refresh_image();
}

esp_err_t Spectra6::sleep() {
    constexpr uint8_t sleep_data[] = {0xA5};
    ESP_RETURN_ON_ERROR(send_command(Chip::Master, 0x07, sleep_data, sizeof(sleep_data)), TAG, "Sleep command");
    vTaskDelay(pdMS_TO_TICKS(1));
    ESP_RETURN_ON_ERROR(gpio_set_level(static_cast<gpio_num_t>(EPD_POWER), 0), TAG, "Power off");
    return ESP_OK;
}
