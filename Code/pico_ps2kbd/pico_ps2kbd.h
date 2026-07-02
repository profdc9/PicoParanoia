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

#ifdef __cplusplus
extern "C" {
#endif

void pico_ps2kbd_init(void);    // configure GPIO4/5 and the clock-edge IRQ
int  pico_ps2kbd_getkey(void);  // next decoded byte, or -1 if the FIFO is empty

#ifdef __cplusplus
}
#endif

#endif // PICO_PS2KBD_H
