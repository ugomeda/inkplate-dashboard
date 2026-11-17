idf_component_register(
    SRCS "main.cpp" "png_decoder.c"
    INCLUDE_DIRS "."
    REQUIRES spectra6
    PRIV_REQUIRES esp_wifi esp_netif esp_event esp_http_client nvs_flash
                  espressif__libpng
)