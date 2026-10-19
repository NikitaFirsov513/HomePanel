cmake_minimum_required(VERSION 3.16)
project(home_panel_model_tests LANGUAGES C CXX)
if(MSVC)
    add_compile_options(/utf-8)
endif()

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
    ../main/src/DeviceAction.cpp
    "${CJSON_ROOT}/cJSON.c")
target_compile_features(device_manager_tests PRIVATE cxx_std_17)
target_include_directories(device_manager_tests PRIVATE
    stubs ../main/include/Device "${CJSON_ROOT}")
enable_testing()
find_program(HOSTED_TEST_MAKE_PROGRAM NAMES "${CMAKE_MAKE_PROGRAM}" REQUIRED)
add_test(NAME hosted_init_guard COMMAND "${CMAKE_COMMAND}"
    -S "${CMAKE_CURRENT_LIST_DIR}/cmake/hosted_init" -B "${CMAKE_CURRENT_BINARY_DIR}/hosted-init-guard"
    -G "${CMAKE_GENERATOR}" "-DCMAKE_MAKE_PROGRAM=${HOSTED_TEST_MAKE_PROGRAM}")
add_test(NAME hosted_init_guard_missing COMMAND "${CMAKE_COMMAND}"
    "-DFIXTURE=${CMAKE_CURRENT_LIST_DIR}/cmake/hosted_init" "-DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}/hosted-init-missing"
    "-DGENERATOR=${CMAKE_GENERATOR}" "-DMAKE_PROGRAM=${HOSTED_TEST_MAKE_PROGRAM}"
    -P "${CMAKE_CURRENT_LIST_DIR}/cmake/verify_missing_hosted_init.cmake")
add_test(NAME device_manager COMMAND device_manager_tests)

add_executable(https_client_tests https_client_test.cpp ../main/src/HttpsClient.cpp)
target_compile_features(https_client_tests PRIVATE cxx_std_17)
target_include_directories(https_client_tests PRIVATE stubs ../main/include)
add_test(NAME https_client COMMAND https_client_tests)

add_executable(yandex_auth_tests yandex_auth_test.cpp ../main/src/YandexAuth.cpp "${CJSON_ROOT}/cJSON.c")
target_compile_features(yandex_auth_tests PRIVATE cxx_std_17)
target_include_directories(yandex_auth_tests PRIVATE ../main/include "${CJSON_ROOT}")
add_test(NAME yandex_auth COMMAND yandex_auth_tests)

add_executable(home_assistant_tests home_assistant_test.cpp ../main/src/HomeAssistant.cpp
    ../main/src/DeviceManager.cpp "${CJSON_ROOT}/cJSON.c")
target_compile_features(home_assistant_tests PRIVATE cxx_std_17)
target_include_directories(home_assistant_tests PRIVATE stubs ../main/include ../main/include/Device "${CJSON_ROOT}")
add_test(NAME home_assistant COMMAND home_assistant_tests)

add_executable(network_tests network_test.cpp ../main/src/Network.cpp)
target_compile_features(network_tests PRIVATE cxx_std_17)
target_include_directories(network_tests PRIVATE ../main/include)
add_test(NAME network COMMAND network_tests)

add_executable(device_load_tests device_load_test.cpp)
target_include_directories(device_load_tests PRIVATE ../main/include)
add_test(NAME device_load COMMAND device_load_tests)

add_executable(hosted_buffer_tests hosted_buffer_test.cpp ../compat/idf61/hosted_buffer_alloc.c)
target_compile_features(hosted_buffer_tests PRIVATE cxx_std_17)
target_include_directories(hosted_buffer_tests PRIVATE hosted_stubs)
add_test(NAME hosted_buffer COMMAND hosted_buffer_tests)

add_executable(floor_plan_tests floor_plan_test.cpp ../main/src/FloorPlan.cpp ../main/src/FloorGeometry.cpp ../main/src/HouseProjection.cpp "${CJSON_ROOT}/cJSON.c")
target_compile_features(floor_plan_tests PRIVATE cxx_std_17)
target_include_directories(floor_plan_tests PRIVATE ../main/include/UI "${CJSON_ROOT}")
add_test(NAME floor_plan COMMAND floor_plan_tests)

if(EXISTS "$ENV{IDF_PATH}/components/esp_driver_dma/include/esp_async_color_convert.h")
    add_executable(dma_compat_tests dma_compat_test.cpp)
    target_compile_features(dma_compat_tests PRIVATE cxx_std_17)
    target_include_directories(dma_compat_tests PRIVATE stubs ../compat/idf61
        "$ENV{IDF_PATH}/components/esp_driver_dma/include"
        "$ENV{IDF_PATH}/components/hal/include")
    add_test(NAME dma_compat COMMAND dma_compat_tests)
endif()

option(HOME_PANEL_UI_TESTS "Build headless LVGL interaction tests" OFF)
if(HOME_PANEL_UI_TESTS)
    set(LV_CONF_SKIP ON CACHE BOOL "" FORCE)
    set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_USE_THORVG OFF CACHE BOOL "" FORCE)
    add_subdirectory(../managed_components/lvgl__lvgl lvgl)
    target_compile_definitions(lvgl PUBLIC LV_USE_STDLIB_MALLOC=1 LV_USE_STDLIB_SPRINTF=1 LV_USE_QRCODE=1)
    add_executable(home_page_tests home_page_test.cpp ../main/src/LoadingPage.cpp ../main/src/HomePage.cpp ../main/src/DeviceIcon.cpp
        ../main/src/FloorPlan.cpp ../main/src/FloorGeometry.cpp ../main/src/FloorPlanEditor.cpp ../main/src/PlanStorage.cpp
        ../main/src/HouseProjection.cpp ../main/src/HouseView.cpp ../main/src/AccountView.cpp
        ../main/src/ConnectionsView.cpp
        ../main/src/NetworkView.cpp
        ../main/src/DeviceManager.cpp ../main/fonts/font_roboto_20.c "${CJSON_ROOT}/cJSON.c")
    target_compile_features(home_page_tests PRIVATE cxx_std_17)
    target_include_directories(home_page_tests PRIVATE stubs ../main/include ../main/include/Device
        ../main/include/UI ../main/include/UI/pages "${CJSON_ROOT}")
    target_link_libraries(home_page_tests PRIVATE lvgl)
    add_test(NAME home_page COMMAND home_page_tests)
endif()

# Some localized MSVC installations yield empty /showIncludes dependency files.
# Track project headers explicitly so native tests cannot reuse an old class ABI.
if(MSVC)
    file(GLOB_RECURSE project_headers CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/../main/include/*.hpp"
        "${CMAKE_CURRENT_LIST_DIR}/stubs/*.h")
    foreach(test_target device_manager_tests https_client_tests yandex_auth_tests home_assistant_tests network_tests floor_plan_tests dma_compat_tests home_page_tests)
        if(TARGET ${test_target})
            get_target_property(test_sources ${test_target} SOURCES)
            set_property(SOURCE ${test_sources} APPEND PROPERTY OBJECT_DEPENDS ${project_headers})
        endif()
    endforeach()
endif()
