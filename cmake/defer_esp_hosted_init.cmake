# ESP-Hosted 2.12.9 starts its transport in a global constructor. On IDF 6.1
# constructors run before SECONDARY system init, including P4 sleep-retention
# allocation. Keep the registry sources intact and omit just that constructor TU.
# NetworkManager explicitly calls esp_hosted_init() after startup, on demand.
function(home_panel_defer_esp_hosted_init hosted_target)
    get_target_property(hosted_sources ${hosted_target} SOURCES)
    set(filtered_sources)
    set(removed_count 0)
    foreach(source IN LISTS hosted_sources)
        get_filename_component(filename "${source}" NAME)
        if(filename STREQUAL "port_esp_hosted_host_init.c")
            math(EXPR removed_count "${removed_count} + 1")
        else()
            list(APPEND filtered_sources "${source}")
        endif()
    endforeach()
    if(NOT removed_count EQUAL 1)
        message(FATAL_ERROR "Expected one ESP-Hosted auto-init source; review the deferred-init compatibility layer")
    endif()
    set_property(TARGET ${hosted_target} PROPERTY SOURCES "${filtered_sources}")
endfunction()
