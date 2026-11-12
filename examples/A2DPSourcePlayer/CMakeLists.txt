cmake_minimum_required(VERSION 3.13)
set(PICO_BOARD pico_w)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(A2DPSourcePlayer C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
set(PICO_CXX_ENABLE_EXCEPTIONS 1)
pico_sdk_init()
set(PLAYER_TARGET_ADDRESS "" CACHE STRING "Only this speaker may be connected: XX:XX:XX:XX:XX:XX")
if(PLAYER_TARGET_ADDRESS AND NOT PLAYER_TARGET_ADDRESS MATCHES "^[0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]:[0-9A-Fa-f][0-9A-Fa-f]$")
    message(FATAL_ERROR "Invalid PLAYER_TARGET_ADDRESS")
endif()
set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
include(${ROOT}/cmake/picow_a2dp_source.cmake)
add_executable(a2dp_source_player main.cpp usb_descriptors.c
    ${PICO_SDK_PATH}/src/rp2_common/pico_stdio_usb/reset_interface.c)
picow_target_a2dp_source(a2dp_source_player ${CMAKE_CURRENT_LIST_DIR})
target_include_directories(a2dp_source_player PRIVATE
    ${PICO_SDK_PATH}/src/rp2_common/pico_stdio_usb/include)
target_compile_definitions(a2dp_source_player PRIVATE
    PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE=1
    PICO_STDIO_USB_ENABLE_RESET_VIA_BAUD_RATE=1
    PICO_STDIO_USB_RESET_INTERFACE_SUPPORT_MS_OS_20_DESCRIPTOR=0
    PLAYER_TARGET_ADDRESS="${PLAYER_TARGET_ADDRESS}")
target_link_libraries(a2dp_source_player PRIVATE
    pico_unique_id pico_usb_reset_interface tinyusb_device)
# Keep SDK-standard BOOTSEL/reset support with our own nonblocking CDC pump.
set_source_files_properties(${PICO_SDK_PATH}/src/rp2_common/pico_stdio_usb/reset_interface.c
    PROPERTIES COMPILE_OPTIONS "-include;pico/stdio_usb.h")
# No stdio waits or background USB task.
pico_enable_stdio_usb(a2dp_source_player 0)
pico_enable_stdio_uart(a2dp_source_player 0)
pico_add_extra_outputs(a2dp_source_player)
