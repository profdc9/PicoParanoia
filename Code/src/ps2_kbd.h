// PS/2 keyboard driver (RP2040).
//
// Ported from the ParanoiaBox PS2Keyboard library: a GPIO falling-edge IRQ on
// the clock line clocks in each PS/2 frame, decodes scancodes (with shift/ctrl
// and arrow-key escape sequences) to ASCII, and queues them in a FIFO.
//
// Pins (PORTING.md §1.1): DATA = GPIO4, CLOCK = GPIO5.
#ifndef PS2_KBD_H
#define PS2_KBD_H

#ifdef __cplusplus
extern "C" {
#endif

void ps2_kbd_init(void);    // configure GPIO4/5 and the clock-edge IRQ
int  ps2_kbd_getkey(void);  // next decoded byte, or -1 if the FIFO is empty

#ifdef __cplusplus
}
#endif

#endif // PS2_KBD_H
