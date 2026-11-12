# Include after pico_sdk_init(). Do not link the legacy background/plugin target.
include_guard(GLOBAL)
get_filename_component(PICOW_A2DP_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
# Keep the definition path available to calls from other directory/function scopes.
set_property(GLOBAL PROPERTY PICOW_A2DP_ROOT "${PICOW_A2DP_ROOT}")
function(picow_target_a2dp_source target config_directory)
    get_property(PICOW_A2DP_ROOT GLOBAL PROPERTY PICOW_A2DP_ROOT)
    target_compile_features(${target} PUBLIC cxx_std_17)
    target_sources(${target} PRIVATE
        ${PICOW_A2DP_ROOT}/bluetooth-runtime/BluetoothRuntime.cpp
        ${PICOW_A2DP_ROOT}/audio-core/A2dpSourcePlayer.cpp
        ${PICOW_A2DP_ROOT}/audio-core/AudioPipeline.cpp
        ${PICOW_A2DP_ROOT}/audio-core/BTStackA2DPTransport.cpp
        ${PICOW_A2DP_ROOT}/audio-core/encoders/SBCEncoder_BTStack.cpp
        ${PICO_SDK_PATH}/lib/btstack/src/classic/btstack_link_key_db_memory.c)
    target_include_directories(${target} PUBLIC
        ${PICOW_A2DP_ROOT}/bluetooth-runtime
        ${config_directory} ${PICOW_A2DP_ROOT}/audio-core
        ${PICOW_A2DP_ROOT}/audio-core/encoders
        ${PICO_SDK_PATH}/lib/btstack/src/classic)
    target_compile_definitions(${target} PUBLIC CYW43_LWIP=0)
    target_link_libraries(${target} PUBLIC
        pico_stdlib pico_cyw43_arch_poll pico_btstack_classic
        pico_btstack_cyw43 pico_btstack_sbc_encoder)
endfunction()
