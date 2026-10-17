cmake_minimum_required(VERSION 3.16)
project(home_panel_model_tests LANGUAGES C CXX)
set(cjson_default "$ENV{IDF_PATH}/components/json/cJSON")
if(NOT EXISTS "${cjson_default}/cJSON.c")
    set(cjson_default "${CMAKE_CURRENT_LIST_DIR}/../managed_components/espressif__cjson/cJSON")
endif()
set(CJSON_ROOT "${cjson_default}" CACHE PATH "Directory containing cJSON.c")
if(NOT EXISTS "${CJSON_ROOT}/cJSON.c")
    message(FATAL_ERROR "Set CJSON_ROOT to the directory containing cJSON.c (ESP-IDF 5 json or managed espressif__cjson)")
endif()
add_executable(device_manager_tests
    device_manager_test.cpp
    ../main/src/DeviceManager.cpp
    "${CJSON_ROOT}/cJSON.c")
target_compile_features(device_manager_tests PRIVATE cxx_std_17)
target_include_directories(device_manager_tests PRIVATE
    stubs ../main/include/Device "${CJSON_ROOT}")
enable_testing()
add_test(NAME device_manager COMMAND device_manager_tests)

add_executable(https_client_tests https_client_test.cpp ../main/src/HttpsClient.cpp)
target_compile_features(https_client_tests PRIVATE cxx_std_17)
target_include_directories(https_client_tests PRIVATE stubs ../main/include)
add_test(NAME https_client COMMAND https_client_tests)

if(EXISTS "$ENV{IDF_PATH}/components/esp_driver_dma/include/esp_async_color_convert.h")
    add_executable(dma_compat_tests dma_compat_test.cpp)
    target_compile_features(dma_compat_tests PRIVATE cxx_std_17)
    target_include_directories(dma_compat_tests PRIVATE stubs ../compat/idf61
        "$ENV{IDF_PATH}/components/esp_driver_dma/include"
        "$ENV{IDF_PATH}/components/hal/include")
    add_test(NAME dma_compat COMMAND dma_compat_tests)
endif()
