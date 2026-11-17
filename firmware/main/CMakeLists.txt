idf_component_register(
                    SRCS "firmware.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES "inkplate"
                    PRIV_REQUIRES
                        esp_wifi
                        esp_netif
                        esp_event
                        esp_http_client
                        nvs_flash
)
