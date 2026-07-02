// pico_ps2kbd — RP2040 PS/2 keyboard driver. See pico_ps2kbd.h.
// Ported from ParanoiaBox PS2Keyboard.cpp; the scancode table and frame state
// machine are unchanged, only the I/O (digitalRead -> gpio_get, attachInterrupt
// -> gpio edge IRQ) is RP2040-specific.

#include "pico_ps2kbd.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/gpio.h"

#define CLOCK_PIN 5
#define DATA_PIN  4
#define FIFOSIZE  16

static int state, paritybit;
static uint8_t curbyte;
static uint8_t shiftkey;
static uint8_t ctrlkey;
static uint8_t lastkeyup;

struct scancodetable {
    unsigned char scancode;
    unsigned char nonshift;
    unsigned char shifted;
    unsigned char ctrled;
    const char *special;
};

static const struct scancodetable scancodes[] = {
    { 0x1C, 'a', 'A', 'A'-64, 0 }, { 0x32, 'b', 'B', 'B'-64, 0 },
    { 0x21, 'c', 'C', 'C'-64, 0 }, { 0x23, 'd', 'D', 'D'-64, 0 },
    { 0x24, 'e', 'E', 'E'-64, 0 }, { 0x2B, 'f', 'F', 'F'-64, 0 },
    { 0x34, 'g', 'G', 'G'-64, 0 }, { 0x33, 'h', 'H', 'H'-64, 0 },
    { 0x43, 'i', 'I', 'I'-64, 0 }, { 0x3B, 'j', 'J', 'J'-64, 0 },
    { 0x42, 'k', 'K', 'K'-64, 0 }, { 0x4B, 'l', 'L', 'L'-64, 0 },
    { 0x3A, 'm', 'M', 'M'-64, 0 }, { 0x31, 'n', 'N', 'N'-64, 0 },
    { 0x44, 'o', 'O', 'O'-64, 0 }, { 0x4D, 'p', 'P', 'P'-64, 0 },
    { 0x15, 'q', 'Q', 'Q'-64, 0 }, { 0x2D, 'r', 'R', 'R'-64, 0 },
    { 0x1B, 's', 'S', 'S'-64, 0 }, { 0x2C, 't', 'T', 'T'-64, 0 },
    { 0x3C, 'u', 'U', 'U'-64, 0 }, { 0x2A, 'v', 'V', 'V'-64, 0 },
    { 0x1D, 'w', 'W', 'W'-64, 0 }, { 0x22, 'x', 'X', 'X'-64, 0 },
    { 0x35, 'y', 'Y', 'Y'-64, 0 }, { 0x1A, 'z', 'Z', 'Z'-64, 0 },
    { 0x45, '0', ')', '0', 0 }, { 0x16, '1', '!', '1', 0 },
    { 0x1E, '2', '@', '2', 0 }, { 0x26, '3', '#', '3', 0 },
    { 0x25, '4', '$', '4', 0 }, { 0x2E, '5', '%', '5', 0 },
    { 0x36, '6', '^', '6', 0 }, { 0x3D, '7', '&', '7', 0 },
    { 0x3E, '8', '*', '8', 0 }, { 0x46, '9', '(', '9', 0 },
    { 0x0E, '`', '~', '`'-64, 0 }, { 0x4E, '-', '_', '-', 0 },
    { 0x55, '=', '+', '=', 0 }, { 0x5D, '\\', '|', '\\'-64, 0 },
    { 0x66, 0x08, 0x08, 0x08, 0 }, { 0x29, ' ', ' ', ' ', 0 },
    { 0x0D, 0x09, 0x09, 0x09, 0 }, { 0x5A, 0x0D, 0x0D, 0x0D, 0 },
    { 0x76, 27, 27, 27, 0 }, { 0x54, '[', '{', '['-64, 0 },
    { 0x5B, ']', '}', ']'-64, 0 }, { 0x4C, ';', ':', ';', 0 },
    { 0x52, '\'', '\"', '\'', 0 }, { 0x41, ',', '<', ',', 0 },
    { 0x49, '.', '>', '.', 0 }, { 0x4A, '/', '?', '/', 0 },
    { 0x75, 0, 0, 0, "\033[A" }, { 0x6B, 0, 0, 0, "\033[D" },
    { 0x72, 0, 0, 0, "\033[B" }, { 0x74, 0, 0, 0, "\033[C" },
};

#define KB_LEFTSHIFT  0x12
#define KB_RIGHTSHIFT 0x59
#define KB_CTRL       0x14
#define KB_KEY_UP     0xF0

static volatile unsigned char fifo_buf[FIFOSIZE];
static volatile int fifo_head, fifo_tail;

// --- keystroke-timing entropy (defense in depth; see pico_ps2kbd.h) ---
static volatile uint32_t entropy_acc;
static volatile uint32_t entropy_count;

// Mix a microsecond timestamp into the accumulator. Called once per fully
// received PS/2 byte (not per clock edge -- the intra-byte edges are timed
// by the keyboard's own internal oscillator and are fairly regular; the
// unpredictable signal is the human-driven GAP between separate keystrokes).
// This is deliberately cheap, ISR-safe mixing, not a real extractor: it just
// accumulates raw jitter for a caller to fold into a proper hash-based
// whitener (see pico_ps2kbd_entropy_sample() in the header).
static void entropy_mix(void) {
    uint32_t t = (uint32_t)time_us_64();
    uint32_t x = entropy_acc ^ t;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    entropy_acc = x;
    entropy_count++;
}

static void fifo_put(int ch) {
    int np = fifo_head + 1;
    if (np >= FIFOSIZE) np = 0;
    if (np == fifo_tail) return;   // full: drop
    fifo_buf[fifo_head] = (unsigned char)ch;
    fifo_head = np;
}

int pico_ps2kbd_getkey(void) {
    if (fifo_tail == fifo_head) return -1;
    int ch = fifo_buf[fifo_tail];
    int np = fifo_tail + 1;
    if (np >= FIFOSIZE) np = 0;
    fifo_tail = np;
    return ch;
}

// Falling-edge on the clock line: sample one bit and advance the frame decoder
// (start bit, 8 data LSB-first, parity, stop), then decode on the stop bit.
static void ps2_irq(uint gpio, uint32_t events) {
    (void)gpio; (void)events;
    int clockbit = gpio_get(CLOCK_PIN);
    int databit  = gpio_get(DATA_PIN);

    if (state == 0) {
        if ((!clockbit) && (!databit)) { paritybit = 0; curbyte = 0; state++; }
    } else if (state <= 8) {
        curbyte >>= 1;
        if (databit) { curbyte |= 0x80; paritybit ^= 0x01; }
        state++;
    } else if (state == 9) {
        state = (paritybit != (databit != 0)) ? 10 : 0;
    } else if (state == 10) {
        if (databit) {                                   // valid stop bit
            entropy_mix();   // every completed byte contributes a timing sample
            if (curbyte == KB_KEY_UP) {
                lastkeyup = 1;
            } else {
                if (curbyte == KB_LEFTSHIFT || curbyte == KB_RIGHTSHIFT) {
                    shiftkey = !lastkeyup;
                } else if (curbyte == KB_CTRL) {
                    ctrlkey = !lastkeyup;
                } else if (!lastkeyup) {
                    for (unsigned i = 0; i < sizeof(scancodes)/sizeof(scancodes[0]); i++) {
                        if (scancodes[i].scancode == curbyte) {
                            if (scancodes[i].special) {
                                for (const char *c = scancodes[i].special; *c; c++)
                                    fifo_put(*c);
                            } else if (ctrlkey) {
                                fifo_put(scancodes[i].ctrled);
                            } else {
                                fifo_put(shiftkey ? scancodes[i].shifted
                                                  : scancodes[i].nonshift);
                            }
                            break;
                        }
                    }
                }
                lastkeyup = 0;
            }
        }
        state = 0;
    }
}

// Non-destructive peek at the accumulator + how many byte-events fed it. Not
// synchronized with the IRQ: worst case a reader sees entropy_count slightly
// ahead of/behind the exact acc value it eventually corresponds to, which has
// no security impact here (there's no correctness property being relied on
// beyond "at least this many timing samples have been mixed in so far").
uint32_t pico_ps2kbd_entropy_sample(void) { return entropy_acc; }
uint32_t pico_ps2kbd_entropy_count(void)  { return entropy_count; }

void pico_ps2kbd_init(void) {
    state = curbyte = paritybit = shiftkey = ctrlkey = lastkeyup = 0;
    fifo_head = fifo_tail = 0;
    entropy_acc = entropy_count = 0;

    gpio_init(CLOCK_PIN);
    gpio_init(DATA_PIN);
    gpio_set_dir(CLOCK_PIN, GPIO_IN);
    gpio_set_dir(DATA_PIN, GPIO_IN);
    gpio_pull_up(CLOCK_PIN);   // PS/2 lines idle high (open-collector)
    gpio_pull_up(DATA_PIN);

    gpio_set_irq_enabled_with_callback(CLOCK_PIN, GPIO_IRQ_EDGE_FALL, true, &ps2_irq);
}
