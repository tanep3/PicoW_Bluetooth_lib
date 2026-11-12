include_guard(GLOBAL)
include(${CMAKE_CURRENT_LIST_DIR}/picow_spp_sdk_overlay.cmake)
get_filename_component(PICOW_SPP_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
# Keep the definition path available to calls from other directory/function scopes.
set_property(GLOBAL PROPERTY PICOW_SPP_ROOT "${PICOW_SPP_ROOT}")
function(picow_target_spp target config_directory)
    get_property(PICOW_SPP_ROOT GLOBAL PROPERTY PICOW_SPP_ROOT)
    picow_enable_spp_sdk_overlay(${target})
    target_compile_features(${target} PUBLIC cxx_std_17)
    target_sources(${target} PRIVATE
        ${PICOW_SPP_ROOT}/bluetooth-runtime/BluetoothRuntime.cpp
        ${PICOW_SPP_ROOT}/spp-sdk/SppClient.cpp
        ${PICOW_SPP_ROOT}/spp-sdk/SdkPortConfiguration.c
        ${PICO_SDK_PATH}/lib/btstack/src/classic/btstack_link_key_db_memory.c)
    target_include_directories(${target} PUBLIC
        ${PICOW_SPP_ROOT}/bluetooth-runtime ${PICOW_SPP_ROOT}/spp-sdk
        ${PICOW_SPP_ROOT}/spp-core ${config_directory}
        ${PICO_SDK_PATH}/lib/btstack/src/classic)
    target_compile_definitions(${target} PUBLIC CYW43_LWIP=0)
    target_link_libraries(${target} PUBLIC pico_stdlib pico_cyw43_arch_poll
        pico_btstack_classic pico_btstack_cyw43)
endfunction()
