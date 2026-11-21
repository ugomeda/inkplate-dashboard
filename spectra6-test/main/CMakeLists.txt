idf_component_register(
    SRCS "wifi.cpp" "main.cpp" "png_decoder.c" "error_screen.cpp"
    EMBED_FILES
        "screens/image_decode_failed.png"
        "screens/low_battery.png"
        "screens/server_error.png"
        "screens/server_unreachable.png"
        "screens/unknown_error.png"
        "screens/wifi_unavailable.png"
    INCLUDE_DIRS "."
    REQUIRES spectra6
    PRIV_REQUIRES esp_wifi esp_netif esp_event esp_http_client nvs_flash
                  espressif__libpng esp_driver_gpio esp_adc
)
