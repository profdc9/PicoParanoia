# pico_ntsc — standalone, reusable composite-video text library (PORTING.md §0.B).
# Currently provides the public-domain font tables; the PIO/DMA scanout engine
# is added in the next step-2 increment (will pull in hardware_pio, hardware_dma,
# pico_multicore).
add_library(pico_ntsc STATIC
    fonts/pico_ntsc_fonts.c
)

target_include_directories(pico_ntsc PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)
