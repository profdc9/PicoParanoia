// pico_ntsc — standalone, reusable RP2040 composite-video TEXT library.
//
// Monochrome NTSC/PAL text-cell display via PIO + DMA (PORTING.md §1.5).
// Deliberately decoupled from PicoParanoia: it knows only about a character
// cell buffer, a font, and timing — no application dependencies (§0.B).
//
// Cell model (single byte per cell): glyph index = cell & 0x7F; bit 7 = reverse
// video. Font selection is global and runtime-toggleable for readability.
//
// STATUS: bring-up in progress. The font tables are available now; the PIO
// scanout engine and its public API land in the next step-2 increment.
#ifndef PICO_NTSC_H
#define PICO_NTSC_H

#include "fonts/pico_ntsc_fonts.h"

#endif // PICO_NTSC_H
