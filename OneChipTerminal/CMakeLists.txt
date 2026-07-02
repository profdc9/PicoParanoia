cmake_minimum_required(VERSION 3.13)

# Point the SDK at PicoParanoia's top-level submodule (../pico-sdk) unless
# the environment already overrides PICO_SDK_PATH. This project lives
# alongside PicoParanoia's Code/ in the same repo (same physical board:
# shared MAX232/uart0, PS/2 header, NTSC output -- you flash one firmware or
# the other) and shares its SDK checkout rather than vendoring a second one;
# both pin the same SDK version (2.2.0). Everything else here -- pico_ntsc,
# pico_ps2kbd, TNTSCAnsi, flashstruct -- is still its own vendored/ported
# copy, and this is a fully separate CMake project/executable, so a build of
# one never touches the other.
if(NOT DEFINED PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/../pico-sdk")
endif()

# Target board: the Raspberry Pi Pico (RP2040).
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in the SDK (must come before project()).
include(pico_sdk_import.cmake)

project(onechipterminal C CXX ASM)

# Initialise the SDK.
pico_sdk_init()

# Standalone composite-video text library, vendored from PicoParanoia
# (verbatim -- see repo notes). Decoupled from the application, so no changes
# were needed for this project.
add_subdirectory(pico_ntsc)

# Standalone PS/2 keyboard driver, vendored from PicoParanoia with one
# addition: altkey() state tracking, needed by this project's "hold Alt for
# the config menu" UI. Purely additive to the vendored copy; PicoParanoia's
# own copy is untouched.
add_subdirectory(pico_ps2kbd)

add_executable(onechipterminal
    src/main.cpp
    src/consoleio.cpp
    src/TNTSCAnsi.cpp
    src/flashstruct.c
)

target_include_directories(onechipterminal PRIVATE src)

# A missing return in a non-void function is UB that, at -O2/-Os on
# Cortex-M0+, falls through into the next function and HardFaults --
# PicoParanoia hit this for real (see its CMakeLists.txt). Carry the guard
# over here too.
target_compile_options(onechipterminal PRIVATE -Werror=return-type)

target_link_libraries(onechipterminal
    pico_stdlib
    hardware_uart
    hardware_flash
    pico_ntsc
    pico_ps2kbd
)

# Plain flash XIP execution -- no copy_to_ram: that hardening in PicoParanoia
# is specific to it being an encryption device handling private keys, and
# doesn't apply here.

# No debug console: uart0 is fully owned by the terminal's own data link
# (GPIO0-3), not a printf/stdio console.
pico_enable_stdio_usb(onechipterminal 0)
pico_enable_stdio_uart(onechipterminal 0)

# Generate .uf2, .hex, .bin, .map, etc.
pico_add_extra_outputs(onechipterminal)
