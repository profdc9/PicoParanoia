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

# USB is off by default (least privilege / auditability, PORTING.md §1.7,
# §0.A): the debug console defaults to uart0, and turning USB-CDC stdio on
# is a deliberate opt-in, not the shipping default. With this OFF, no USB
# stdio code links at all.
option(PICOPARANOIA_ENABLE_USB_STDIO "Use USB-CDC for the debug console instead of uart0" OFF)

# Also off by default: RP2040 has one native USB controller, host or device,
# never both. When on, native USB switches to host mode to drive a USB
# keyboard (pico_usbhostkbd, PORTING.md §1.6/§1.7/§2.1 step 9); PS/2 remains
# available at the same time (separate GPIOs, no conflict) via consoleio's
# merged key queue. Mutually exclusive with PICOPARANOIA_ENABLE_USB_STDIO --
# checked below.
option(PICOPARANOIA_ENABLE_USB_HOST "Enable USB-host keyboard support (TinyUSB host + HID)" OFF)

if(PICOPARANOIA_ENABLE_USB_HOST AND PICOPARANOIA_ENABLE_USB_STDIO)
    message(FATAL_ERROR
        "PICOPARANOIA_ENABLE_USB_HOST and PICOPARANOIA_ENABLE_USB_STDIO cannot "
        "both be on: RP2040 has one native USB controller, host or device, not "
        "both (PORTING.md §1.7).")
endif()

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

# Standalone USB-host keyboard driver (PORTING.md §0.B / §1.6) -- opt-in only;
# with PICOPARANOIA_ENABLE_USB_HOST off, this isn't built and no USB/TinyUSB
# host code is linked into the image at all.
if(PICOPARANOIA_ENABLE_USB_HOST)
    add_subdirectory(pico_usbhostkbd)
endif()

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

if(PICOPARANOIA_ENABLE_USB_HOST)
    target_link_libraries(picoparanoia pico_usbhostkbd)
    target_compile_definitions(picoparanoia PRIVATE PICOPARANOIA_ENABLE_USB_HOST)
endif()

# Run the whole image from SRAM — flash-bus hardening (PORTING.md §1.8).
pico_set_binary_type(picoparanoia copy_to_ram)

# Debug console: uart0 (GPIO0/1) by default, matching the shipping config
# (PORTING.md §1.7) -- no USB code linked, native USB stays uninitialized/
# inert. Build with -DPICOPARANOIA_ENABLE_USB_STDIO=1 to switch to USB-CDC
# for bring-up debugging on a board without a UART adapter handy.
if(PICOPARANOIA_ENABLE_USB_STDIO)
    pico_enable_stdio_usb(picoparanoia 1)
    pico_enable_stdio_uart(picoparanoia 0)
else()
    pico_enable_stdio_usb(picoparanoia 0)
    pico_enable_stdio_uart(picoparanoia 1)
endif()

# Generate .uf2, .hex, .bin, .map, etc.
pico_add_extra_outputs(picoparanoia)
