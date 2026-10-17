# ESP-IDF 6 moved cJSON to the component registry; 5.x provides json in-tree.
set(json_requirements)
if(IDF_VERSION_MAJOR LESS 6)
    list(APPEND json_requirements json)
endif()

idf_component_register(
    SRC_DIRS
    "src"
    "fonts"

    INCLUDE_DIRS
    "include"
    "include/Device"
    "include/UI"
    "include/UI/pages"

    PRIV_REQUIRES
    freertos
    esp_http_client
    esp_driver_gpio
    # The BSP public header also includes the SDMMC, I2C and I2S driver headers.
    esp_driver_sdmmc
    esp_driver_i2c
    esp_driver_i2s
    esp_driver_ledc
    esp_event
    esp_eth
    esp_netif
    ${json_requirements}
    mbedtls
)

target_compile_features(
    ${COMPONENT_LIB}
    PUBLIC
    cxx_std_17
)
