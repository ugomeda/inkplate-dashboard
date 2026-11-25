#pragma once

#include "esp_err.h"

enum class BuiltinScreen {
    None,
    Updating,
    WifiUnavailable,
    ServerUnreachable,
    ServerUnexpectedResponse,
    ImageDecodeFailed,
    LowBattery,
    Unknown,
};

esp_err_t show_builtin_screen(BuiltinScreen kind, bool fast);
