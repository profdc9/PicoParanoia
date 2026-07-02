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

# The whole image runs from SRAM (copy-to-RAM, §1.8), so code size is RAM.
# Optimize for size by DEFAULT; speed-critical code opts back up to -O2 per
# target (e.g. crypto_subset for AES/GCM/BLAKE2s throughput). Overrides the
# Release build type's -O3.
set(CMAKE_C_FLAGS_RELEASE   "-Os -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-Os -DNDEBUG")

# Initialise the SDK.
pico_sdk_init()

# Vendored crypto subset (PORTING.md §2.3).
add_subdirectory(third_party/crypto)

# Standalone composite-video text library (PORTING.md §0.B / §1.5).
add_subdirectory(pico_ntsc)

# Standalone PS/2 keyboard driver (PORTING.md §0.B / §1.6).
add_subdirectory(pico_ps2kbd)

# Vendored FatFs R0.16 (PORTING.md §2.3).
add_subdirectory(third_party/fatfs)

add_executable(picoparanoia
    src/main.cpp
    src/consoleio.cpp
    src/TNTSCAnsi.cpp
    src/sd_spi.c
    src/cryptotool.cpp
    src/editor.c
    src/fileop.cpp
    src/random.cpp
    src/flashstruct.c
    src/arduino_rng_glue.cpp
    src/keymanager.cpp
    src/fileenc.cpp
    src/fault.c
)

target_include_directories(picoparanoia PRIVATE src)

# A missing return in a non-void function is UB that, at -O2/-Os on Cortex-M0+,
# falls through into the next function and HardFaults (cost us the key-export
# crash). Make it a hard error in our code so it can never recur silently.
target_compile_options(picoparanoia PRIVATE -Werror=return-type)

target_link_libraries(picoparanoia
    pico_stdlib
    hardware_spi
    hardware_adc
    hardware_flash
    crypto_subset
    pico_ntsc
    pico_ps2kbd
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
