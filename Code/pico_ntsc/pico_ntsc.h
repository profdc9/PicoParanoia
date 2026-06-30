// pico_ntsc — standalone, reusable RP2040 composite-video TEXT library.
//
// Monochrome NTSC/PAL text-cell display via PIO + DMA (PORTING.md §1.5).
// Deliberately decoupled from PicoParanoia: it knows only about a character
// cell buffer, a font, and timing — no application dependencies (§0.B).
//
// Cell model (single byte per cell): glyph index = cell & 0x7F; bit 7 = reverse
// video. Font selection is global and runtime-toggleable for readability.
//
// STATUS: step-2 bring-up. Font tables are available; the scanout engine
// currently exposes a static test pattern for first-light (sync-lock) testing.
// Framebuffer + glyph blitter API lands in the next increment.
#ifndef PICO_NTSC_H
#define PICO_NTSC_H

#include <stdbool.h>
#include "fonts/pico_ntsc_fonts.h"

#ifdef __cplusplus
extern "C" {
#endif

// System clock the line timing requires; the engine sets this itself.
#define PICO_NTSC_SYS_CLOCK_KHZ 126000

// First-light: drive a static NTSC 240p test pattern (vertical bars + border)
// on GPIO16 (sync) / GPIO17 (luma). Sets the system clock to 126 MHz and runs
// autonomously via PIO + a self-reloading DMA loop. Returns false if the clock
// could not be set. Call before stdio_init_all().
bool pico_ntsc_init_test_pattern(void);

#ifdef __cplusplus
}
#endif

#endif // PICO_NTSC_H
