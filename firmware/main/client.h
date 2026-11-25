#pragma once

#include <cstddef>
#include <cstdint>

#include "builtin_screen.h"

constexpr size_t ETAG_BUFFER_SIZE = 128;

struct DashboardImageRequest {
    const char* url = nullptr;
    uint16_t battery_mv = 0;
    char etag[ETAG_BUFFER_SIZE] = {};

    uint8_t* response_data = nullptr;
    size_t response_size = 0;
    uint32_t next_refresh = 0;
};

BuiltinScreen request_image(DashboardImageRequest& request);
