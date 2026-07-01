# Vendored FatFs R0.16 (see PROVENANCE.md). Portable core only; the SD-over-SPI
# disk driver (disk_* in src/sd_spi.c) is PicoParanoia platform code.
add_library(fatfs STATIC
    ff.c
    ffunicode.c
    ffsystem.c
)

target_include_directories(fatfs PUBLIC ${CMAKE_CURRENT_LIST_DIR})
