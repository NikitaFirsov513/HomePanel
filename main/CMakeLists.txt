# ESP-IDF 6 moved cJSON to the component registry; 5.x provides json in-tree.
set(json_requirements)
if(IDF_VERSION_MAJOR LESS 6)
    list(APPEND json_requirements json)
endif()

idf_component_register(
    SRCS
    "src/main.cpp"
    "src/ethernet_init.c"
    "src/HttpsClient.cpp"
    "src/DeviceManager.cpp"
    "src/DeviceAction.cpp"
    "src/DeviceIcon.cpp"
    "src/UiManager.cpp"
    "src/HomePage.cpp"
    "src/LoadingPage.cpp"
    "src/FloorPlan.cpp"
    "src/FloorPlanEditor.cpp"
    "src/PlanStorage.cpp"
    "src/HouseProjection.cpp"
    "src/HouseView.cpp"
    "fonts/font_roboto_20.c"

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
    nvs_flash
    ${json_requirements}
    mbedtls
)

target_compile_features(
    ${COMPONENT_LIB}
    PUBLIC
    cxx_std_17
)
