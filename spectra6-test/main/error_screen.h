#pragma once

#include "esp_err.h"

enum class ErrorScreenKind {
    None, // use as zero value in main.cpp
    WifiUnavailable,
    ServerUnreachable,
    ServerUnexpectedReponse,
    ImageDecodeFailed,
    LowBattery,
};

// Draw a landscape 1600x1200 black/white error page on the native 1200x1600 panel.
// Returns ESP_OK only when the refresh and panel sleep both succeeded.
esp_err_t show_error_screen(ErrorScreenKind kind);
