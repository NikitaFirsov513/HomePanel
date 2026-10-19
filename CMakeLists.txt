# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
project(home_panel)

if(CONFIG_ESP_HOSTED_ENABLED)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/defer_esp_hosted_init.cmake")
    idf_component_get_property(hosted_lib espressif__esp_hosted COMPONENT_LIB)
    home_panel_defer_esp_hosted_init(${hosted_lib})
    if(CONFIG_IDF_TARGET_ESP32P4 AND CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE AND
       IDF_VERSION_MAJOR EQUAL 6 AND IDF_VERSION_MINOR EQUAL 1)
        include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/hosted_psram_buffers.cmake")
        home_panel_hosted_psram_buffers(${hosted_lib})
        target_sources(${hosted_lib} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/compat/idf61/hosted_buffer_alloc.c")
        target_link_libraries(${hosted_lib} PRIVATE idf::esp_mm)
        message(STATUS "P4 SDIO transport buffers prefer cache-aligned PSRAM")
    endif()
    message(STATUS "ESP-Hosted initialization deferred until Wi-Fi is configured")
endif()

# esp_lvgl_adapter 0.1.x uses the private framebuffer-copy API removed in IDF 6.1.
# Keep the registry component intact and bridge it to the public DMA API locally.
if(IDF_VERSION_MAJOR GREATER_EQUAL 6 AND
   NOT EXISTS "${IDF_PATH}/components/esp_lcd/priv_include/esp_async_fbcpy.h")
    idf_component_get_property(adapter_lib espressif__esp_lvgl_adapter COMPONENT_LIB)
    target_include_directories(${adapter_lib} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/compat/idf61")
    target_link_libraries(${adapter_lib} PRIVATE idf::esp_driver_dma)
endif()

# HX8394 is compiled by the BSP even when another panel is selected. Its 1.0.x
# driver still uses the removed alias for the RGB element order field.
if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    idf_component_get_property(bsp_lib waveshare__esp32_p4_platform COMPONENT_LIB)
    target_link_libraries(${bsp_lib} PRIVATE
        idf::esp_driver_ledc idf::esp_driver_sdmmc
        idf::esp_driver_i2c idf::esp_driver_i2s)
    idf_component_get_property(hx8394_lib waveshare__esp_lcd_hx8394 COMPONENT_LIB)
    target_compile_definitions(${hx8394_lib} PRIVATE color_space=rgb_ele_order)
    if(IDF_VERSION_MAJOR GREATER 6 OR IDF_VERSION_MINOR GREATER_EQUAL 1)
        # Keep the option and its argument grouped: the adapter also exports
        # -include, and CMake otherwise deduplicates the two option tokens.
        target_compile_options(${bsp_lib} PRIVATE
            "SHELL:-include \"${CMAKE_CURRENT_SOURCE_DIR}/compat/idf61/bsp_ek79007.h\"")
    endif()
endif()
