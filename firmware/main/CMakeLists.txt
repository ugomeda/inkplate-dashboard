if(IDF_TARGET STREQUAL "esp32")
    set(SCREEN_DIR "screens/inkplate10")
elseif(IDF_TARGET STREQUAL "esp32s3")
    set(SCREEN_DIR "screens/spectra6")
else()
    message(FATAL_ERROR "Unsupported target")
endif()


idf_component_register(
    SRCS
        "main.cpp"
        "client.cpp"
        "wifi_manager.cpp"
        "builtin_screen.cpp"
    EMBED_FILES
        "${SCREEN_DIR}/image_decode_failed.png"
        "${SCREEN_DIR}/low_battery.png"
        "${SCREEN_DIR}/server_error.png"
        "${SCREEN_DIR}/server_unreachable.png"
        "${SCREEN_DIR}/unknown_error.png"
        "${SCREEN_DIR}/updating.png"
        "${SCREEN_DIR}/wifi_unavailable.png"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_wifi
        esp_netif
        esp_event
        esp_http_client
        nvs_flash
        esp_driver_gpio
        png_decoder
        displays
)
