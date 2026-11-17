#pragma once

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
