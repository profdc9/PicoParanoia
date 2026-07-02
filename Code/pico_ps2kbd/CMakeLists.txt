# pico_ps2kbd — standalone, reusable RP2040 PS/2 keyboard driver (PORTING.md §0.B).
add_library(pico_ps2kbd STATIC
    pico_ps2kbd.c
)

target_include_directories(pico_ps2kbd PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(pico_ps2kbd
    pico_stdlib
    hardware_gpio
)
