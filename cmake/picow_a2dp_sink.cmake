# Include after pico_sdk_init(). Application supplies btstack_config.h.
include_guard(GLOBAL)
get_filename_component(PICOW_A2DP_SINK_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
# Keep the definition path available to calls from other directory/function scopes.
set_property(GLOBAL PROPERTY PICOW_A2DP_SINK_ROOT "${PICOW_A2DP_SINK_ROOT}")
include(${CMAKE_CURRENT_LIST_DIR}/picow_sink_raw_configuration.cmake)
function(picow_target_a2dp_sink target config_directory)
    picow_enable_sink_raw_configuration(${target})
    get_property(PICOW_A2DP_SINK_ROOT GLOBAL PROPERTY PICOW_A2DP_SINK_ROOT)
    target_compile_features(${target} PUBLIC cxx_std_17)
    target_sources(${target} PRIVATE
        ${PICOW_A2DP_SINK_ROOT}/bluetooth-runtime/BluetoothRuntime.cpp
        ${PICOW_A2DP_SINK_ROOT}/audio-core/A2dpSinkReceiver.cpp
        ${PICO_SDK_PATH}/lib/btstack/src/classic/btstack_link_key_db_memory.c)
    target_include_directories(${target} PUBLIC
        ${PICOW_A2DP_SINK_ROOT}/bluetooth-runtime ${PICOW_A2DP_SINK_ROOT}/audio-core
        ${config_directory} ${PICO_SDK_PATH}/lib/btstack/src/classic)
    target_compile_definitions(${target} PUBLIC CYW43_LWIP=0)
    target_link_libraries(${target} PUBLIC pico_stdlib pico_cyw43_arch_poll
        pico_btstack_classic pico_btstack_cyw43 pico_btstack_sbc_decoder)
    # Conservative decoder stack reservation, inherited from the validated probe.
    # User callbacks must still be bounded; this is not a measured peak guarantee.
    target_compile_definitions(${target} PRIVATE PICO_STACK_SIZE=32768)
    # Generate a target-private layout from the installed SDK without editing it.
    # Keep a real, non-overlapping 32 KiB stack in main SRAM and cap the heap below
    # it. Merely increasing PICO_STACK_SIZE with the default scratch-Y layout does
    # not reduce the heap limit, and its non-ALLOC dummy can conceal that overlap.
    set(SINK_LINKER_SOURCE "${PICO_SDK_PATH}/src/rp2_common/pico_crt0/rp2040/memmap_default.ld")
    file(READ "${SINK_LINKER_SOURCE}" SINK_LINKER)
    foreach(REQUIRED_TEXT
        "RAM(rwx) : ORIGIN =  0x20000000, LENGTH = 256k"
        "KEEP(*(.stack*))\n    } > SCRATCH_Y"
        "__StackTop = ORIGIN(SCRATCH_Y) + LENGTH(SCRATCH_Y);")
      string(FIND "${SINK_LINKER}" "${REQUIRED_TEXT}" POSITION)
      if(POSITION EQUAL -1)
        message(FATAL_ERROR "Installed SDK linker layout changed; review the sink stack layout")
      endif()
    endforeach()
    string(REPLACE "RAM(rwx) : ORIGIN =  0x20000000, LENGTH = 256k"
        "RAM(rwx) : ORIGIN = 0x20000000, LENGTH = 224k\n    STACK0(rwx) : ORIGIN = 0x20038000, LENGTH = 32k"
        SINK_LINKER "${SINK_LINKER}")
    string(REPLACE "KEEP(*(.stack*))\n    } > SCRATCH_Y" "KEEP(*(.stack*))\n    } > STACK0" SINK_LINKER "${SINK_LINKER}")
    string(REPLACE "__StackTop = ORIGIN(SCRATCH_Y) + LENGTH(SCRATCH_Y);"
        "__StackTop = ORIGIN(STACK0) + LENGTH(STACK0);" SINK_LINKER "${SINK_LINKER}")
    string(APPEND SINK_LINKER "\nASSERT(__StackBottom == __HeapLimit, \"sink heap/stack boundary mismatch\")\nASSERT(SIZEOF(.stack_dummy) == LENGTH(STACK0), \"sink stack size mismatch\")\n")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${target}_sink_memmap.ld" "${SINK_LINKER}")
    pico_set_linker_script(${target} "${CMAKE_CURRENT_BINARY_DIR}/${target}_sink_memmap.ld")
endfunction()

# Opt-in symbols and Sink-only generated gate. Call after picow_target_a2dp_sink.
# Settings::require_start_decision must additionally opt the receiver in.
function(picow_enable_sink_start_decisions target)
    get_target_property(enabled ${target} PICOW_SINK_RAW_CONFIGURATION)
    if(NOT enabled)
        message(FATAL_ERROR "Call picow_target_a2dp_sink before picow_enable_sink_start_decisions")
    endif()
    target_compile_definitions(${target} PUBLIC PICOW_A2DP_SINK_START_DECISIONS=1
        ENABLE_AVDTP_ACCEPTOR_EXPLICIT_START_STREAM_CONFIRMATION=1)
endfunction()
