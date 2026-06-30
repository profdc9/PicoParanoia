// Public-domain 8x8 font tables for pico_ntsc (readability-tuned set).
// Single-byte cell model: index = cell & 0x7F; bit7 of the cell = reverse video
// (handled by the blitter). MSB of each row byte = leftmost pixel.
#ifndef PICO_NTSC_FONTS_H
#define PICO_NTSC_FONTS_H
#include <stdint.h>

// Regular weight: heavier strokes, best at 80 columns where TV luma bandwidth
// attenuates 1px detail.
extern const uint8_t pico_ntsc_font_unscii8[128][8];
// Thin weight: crisper at 40 columns / higher-bandwidth displays.
extern const uint8_t pico_ntsc_font_unscii8_thin[128][8];

#endif
