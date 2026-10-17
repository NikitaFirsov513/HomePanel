cmake_minimum_required(VERSION 3.16)
project(home_panel_model_tests LANGUAGES C CXX)
set(CJSON_ROOT "$ENV{IDF_PATH}/components/json/cJSON" CACHE PATH "Directory containing cJSON.c")
if(NOT EXISTS "${CJSON_ROOT}/cJSON.c")
    message(FATAL_ERROR "Set CJSON_ROOT to the ESP-IDF components/json/cJSON directory")
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
