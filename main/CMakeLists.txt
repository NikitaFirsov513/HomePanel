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
    esp_event
    esp_eth
    esp_netif
    json
    mbedtls
)

target_compile_features(
    ${COMPONENT_LIB}
    PUBLIC
    cxx_std_17
)
