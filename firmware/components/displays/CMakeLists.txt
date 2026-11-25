if(IDF_TARGET STREQUAL "esp32")
    set(SOURCE "display_inkplate10.cpp")
    set(DEPS solderedelectronics__inkplate)
elseif(IDF_TARGET STREQUAL "esp32s3")
    set(SOURCE "display_spectra6.cpp")
    set(DEPS esp_driver_gpio esp_driver_spi esp_timer esp_adc)
endif()

idf_component_register(
    SRCS ${SOURCE}
    INCLUDE_DIRS "includes"
    PRIV_REQUIRES ${DEPS}
)