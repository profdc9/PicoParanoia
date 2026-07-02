// pico_ps2kbd — standalone, reusable RP2040 PS/2 keyboard driver.
//
// A GPIO falling-edge IRQ on the clock line clocks in each PS/2 frame,
// decodes scancodes (with shift/ctrl and arrow-key escape sequences) to
// ASCII, and queues the result in a FIFO. Decoupled from the application
// (PORTING.md §0.B, matching the pico_ntsc library): it knows only about the
// PS/2 wire protocol and hands back plain bytes, nothing app-specific.
//
// Ported from the ParanoiaBox PS2Keyboard library; the scancode table and
// frame state machine are unchanged, only the I/O (digitalRead -> gpio_get,
// attachInterrupt -> gpio edge IRQ) is RP2040-specific.
//
// Pins (PORTING.md §1.1): DATA = GPIO4, CLOCK = GPIO5.
#ifndef PICO_PS2KBD_H
#define PICO_PS2KBD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void pico_ps2kbd_init(void);    // configure GPIO4/5 and the clock-edge IRQ
int  pico_ps2kbd_getkey(void);  // next decoded byte, or -1 if the FIFO is empty

// Keystroke-timing entropy (defense in depth).
//
// The driver mixes a microsecond timestamp into a running accumulator once
// per fully received PS/2 byte (every byte, including modifier presses and
// key-up markers -- not just ones that decode to a visible character), using
// the gap between separate keystrokes rather than intra-byte clock edges
// (those are timed by the keyboard's own oscillator and are fairly regular;
// human typing rhythm is not).
//
// This is raw, UNWHITENED material, not a substitute for a real entropy
// source: it's cheap xorshift-style mixing done inside an ISR, with no
// health checking and no guarantee of any particular amount of unpredictable
// bits per event. It exists so a platform RNG has a fallback input to fold
// into a proper hash-based extractor if its primary source (e.g. analog
// noise) is unavailable or fails its health checks -- not to be trusted
// alone, and only available when a human is actively typing.
uint32_t pico_ps2kbd_entropy_sample(void);  // current accumulator value
uint32_t pico_ps2kbd_entropy_count(void);   // how many byte-events have contributed

#ifdef __cplusplus
}
#endif

#endif // PICO_PS2KBD_H
