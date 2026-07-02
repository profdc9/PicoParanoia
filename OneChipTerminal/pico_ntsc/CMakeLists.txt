# pico_ntsc — standalone, reusable composite-video text library (PORTING.md §0.B).
# Public-domain font tables + the PIO/DMA scanout engine (first-light: a static
# NTSC 240p test pattern; framebuffer text path to follow).
add_library(pico_ntsc STATIC
    fonts/pico_ntsc_fonts.c
    pico_ntsc.c
)

pico_generate_pio_header(pico_ntsc ${CMAKE_CURRENT_LIST_DIR}/ntsc.pio)

target_include_directories(pico_ntsc PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(pico_ntsc
    pico_stdlib
    hardware_pio
    hardware_dma
    hardware_clocks
)
