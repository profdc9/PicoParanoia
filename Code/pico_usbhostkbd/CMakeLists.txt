# pico_usbhostkbd — standalone RP2040 USB-host boot-keyboard driver
# (PORTING.md §0.B / §1.6). Only built when PICOPARANOIA_ENABLE_USB_HOST is
# set (see the top-level CMakeLists.txt); the caller is responsible for that
# gating and for not also enabling USB-CDC stdio (mutually exclusive: RP2040
# has one native USB controller, host or device, not both).
add_library(pico_usbhostkbd STATIC
    pico_usbhostkbd.c
)

# Public so dependents resolve pico_usbhostkbd.h (matches pico_ntsc/pico_ps2kbd).
# tusb_config.h lives here too; harmless to expose, nothing else consumes it.
target_include_directories(pico_usbhostkbd PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(pico_usbhostkbd
    pico_stdlib
    pico_multicore
    tinyusb_host
)
