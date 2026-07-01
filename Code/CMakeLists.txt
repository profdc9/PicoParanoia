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

# Vendored crypto subset (PORTING.md §2.3).
add_subdirectory(third_party/crypto)

# Standalone composite-video text library (PORTING.md §0.B / §1.5).
add_subdirectory(pico_ntsc)

# Vendored FatFs R0.16 (PORTING.md §2.3).
add_subdirectory(third_party/fatfs)

add_executable(picoparanoia
    src/main.cpp
    src/consoleio.cpp
    src/TNTSCAnsi.cpp
    src/ps2_kbd.c
    src/sd_spi.c
    src/cryptotool.c
    src/editor.c
    src/fileop.cpp
)

target_include_directories(picoparanoia PRIVATE src)

target_link_libraries(picoparanoia
    pico_stdlib
    hardware_spi
    crypto_subset
    pico_ntsc
    fatfs
)

# Run the whole image from SRAM — flash-bus hardening (PORTING.md §1.8).
pico_set_binary_type(picoparanoia copy_to_ram)

# Debug console.
# TEMPORARY bring-up choice: USB-CDC stdio (no UART adapter needed). The §1.7
# default is uart0 + USB off/inert; restore that before USB-host support lands.
# Native USB stays in device mode here, which is fine while host mode is deferred.
pico_enable_stdio_usb(picoparanoia 1)
pico_enable_stdio_uart(picoparanoia 0)

# Generate .uf2, .hex, .bin, .map, etc.
pico_add_extra_outputs(picoparanoia)
