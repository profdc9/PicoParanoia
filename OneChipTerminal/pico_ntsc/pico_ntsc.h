// pico_ntsc — standalone, reusable RP2040 composite-video TEXT library.
//
// Monochrome NTSC 240p text-cell display via PIO + DMA (PORTING.md §1.5).
// Decoupled from the application (§0.B): it knows only about character cells,
// a font, and a cursor.
//
// Design (as built):
//  - Single PIO SM emits 2 bits/sample (GPIO16 sync, GPIO17 luma); a
//    self-reloading DMA loop streams a precomputed frame -> autonomous scanout,
//    no per-line CPU, core1 unused.
//  - Glyphs are blitted into the frame's active region on text change (core0).
//  - Cell model: glyph index = cell & 0x7F; bit 7 = reverse video.
//  - Cursor (position, visibility, blink) is owned here and rendered by XOR-ing
//    the cursor cell from a repeating hardware timer.
//  - 25 rows; 80 or 40 columns (40-col = double-width pixels). Global,
//    switchable font for readability.
#ifndef PICO_NTSC_H
#define PICO_NTSC_H

#include <stdbool.h>
#include <stdint.h>
#include "fonts/pico_ntsc_fonts.h"

#ifdef __cplusplus
extern "C" {
#endif

// System clock the line timing requires; the engine sets this itself.
#define PICO_NTSC_SYS_CLOCK_KHZ 126000
#define PICO_NTSC_ROWS 25

typedef enum {
    PICO_NTSC_MODE_80 = 0,   // 80 columns (8px cells)
    PICO_NTSC_MODE_40 = 1,   // 40 columns (double-width 16px cells)
} pico_ntsc_mode_t;

// Bring up composite video in the given mode: sets the system clock to 126 MHz,
// starts the autonomous PIO+DMA scanout (blank screen) and the cursor-blink
// timer. Default font is unscii-8 (regular). Call before stdio_init_all().
// Returns false if the system clock could not be set.
bool pico_ntsc_init(pico_ntsc_mode_t mode);

int  pico_ntsc_cols(void);                          // 80 or 40
// Set mode; clears the screen and selects the readability default font for that
// mode (80-col = unscii-8 regular, 40-col = unscii-8 thin).
void pico_ntsc_set_mode(pico_ntsc_mode_t mode);
// Override the font (call after set_mode); redraw to apply.
void pico_ntsc_set_font(const uint8_t font[128][8]);
void pico_ntsc_clear(void);

// Draw one cell. `cell` low 7 bits = glyph index; bit 7 = reverse video.
void pico_ntsc_put_cell(int row, int col, uint8_t cell);
// Convenience: draw a NUL-terminated string from (row,col), clipped to the row.
void pico_ntsc_put_text(int row, int col, const char *s, bool reverse);

// Fast-clear a linear span of cells to a solid fill (black, or white if
// `reverse`) -- no glyph rendering, so it's much cheaper than looping
// pico_ntsc_put_cell() over the same cells, and touches each sample once
// instead of once per glyph row, which reduces clear-induced flicker.
//
// The span is (row1,col1)..(row2,col2) inclusive in row-major reading order:
// if row1 == row2 it's that row's [col1,col2]; otherwise it's the tail of
// row1 from col1, all of the rows in between, and the head of row2 up to
// col2 -- i.e. exactly the cells a linear walk over a rows*columns buffer
// from (row1,col1) to (row2,col2) would touch.
void pico_ntsc_clear_span(int row1, int col1, int row2, int col2, bool reverse);

// Cursor (rendered as a blinking reverse-video block by the library).
void pico_ntsc_set_cursor(int row, int col, bool visible);

#ifdef __cplusplus
}
#endif

#endif // PICO_NTSC_H
