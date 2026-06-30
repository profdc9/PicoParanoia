cmake_minimum_required(VERSION 3.13)

# Point the SDK at the in-repo submodule (../pico-sdk) unless the
# environment already overrides PICO_SDK_PATH.
if(NOT DEFINED PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/../pico-sdk")
endif()

# Target board: the Raspberry Pi Pico (RP2040).
# Use "pico_w" for the Pico W, or "pico2" for the Pico 2 (RP2350).
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in the SDK (must come before project()).
include(pico_sdk_import.cmake)

project(picoparanoia C CXX ASM)

# Initialise the SDK.
pico_sdk_init()

add_executable(picoparanoia
    main.c
)

# pico_stdlib gives you GPIO, timers, UART/USB stdio, etc.
target_link_libraries(picoparanoia pico_stdlib)

# Route printf() to USB serial (set the UART one to 1 instead if preferred).
pico_enable_stdio_usb(picoparanoia 1)
pico_enable_stdio_uart(picoparanoia 0)

# Generate .uf2, .hex, .bin, .map, etc.
pico_add_extra_outputs(picoparanoia)
