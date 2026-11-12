include_guard(GLOBAL)
get_filename_component(PICOW_AVRCP_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
# Keep the definition path available to calls from other directory/function scopes.
set_property(GLOBAL PROPERTY PICOW_AVRCP_ROOT "${PICOW_AVRCP_ROOT}")
include(${CMAKE_CURRENT_LIST_DIR}/picow_avrcp_sdk_overlay.cmake)
function(picow_target_avrcp_target target config_directory)
    get_property(PICOW_AVRCP_ROOT GLOBAL PROPERTY PICOW_AVRCP_ROOT)
    picow_enable_avrcp_sdk_overlay(${target})
    target_compile_features(${target} PUBLIC cxx_std_17)
    target_sources(${target} PRIVATE
        ${PICOW_AVRCP_ROOT}/bluetooth-runtime/BluetoothRuntime.cpp
        ${PICOW_AVRCP_ROOT}/avrcp-sdk/AvrcpTarget.cpp
        ${PICO_SDK_PATH}/lib/btstack/src/classic/btstack_link_key_db_memory.c)
    target_include_directories(${target} PUBLIC
        ${PICOW_AVRCP_ROOT}/bluetooth-runtime ${PICOW_AVRCP_ROOT}/avrcp-sdk
        ${PICOW_AVRCP_ROOT}/avrcp-core ${config_directory}
        ${PICO_SDK_PATH}/lib/btstack/src/classic)
    target_compile_definitions(${target} PUBLIC CYW43_LWIP=0)
    target_link_libraries(${target} PUBLIC pico_stdlib pico_cyw43_arch_poll
        pico_btstack_classic pico_btstack_cyw43)
endfunction()
