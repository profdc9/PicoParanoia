// pico_usbhostkbd.c — RP2040 USB-host boot-keyboard driver. See pico_usbhostkbd.h.
//
// TinyUSB host mode + the HID host class driver run entirely on core1
// (tuh_init()/tuh_task() need consistent core affinity, and PORTING.md
// documents core1 as free precisely for this). Reports are decoded inline in
// the TinyUSB report-received callback (so on core1) and pushed into a FIFO;
// pico_usbhostkbd_getkey() (called from core0, via consoleio) drains it. Same
// single-producer/single-consumer volatile-index pattern pico_ps2kbd uses for
// its IRQ-vs-mainline FIFO, just across cores instead of across an interrupt
// boundary -- RP2040 has no per-core data cache, so plain SRAM reads/writes
// stay coherent without extra barriers.

#include "pico_usbhostkbd.h"
#include <stdbool.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/time.h"
#include "tusb.h"

#define FIFOSIZE 16

static volatile unsigned char fifo_buf[FIFOSIZE];
static volatile int fifo_head, fifo_tail;

static void fifo_put(int ch)
{
    int np = fifo_head + 1;
    if (np >= FIFOSIZE) np = 0;
    if (np == fifo_tail) return;   // full: drop
    fifo_buf[fifo_head] = (unsigned char)ch;
    fifo_head = np;
}

int pico_usbhostkbd_getkey(void)
{
    if (fifo_tail == fifo_head) return -1;
    int ch = fifo_buf[fifo_tail];
    int np = fifo_tail + 1;
    if (np >= FIFOSIZE) np = 0;
    fifo_tail = np;
    return ch;
}

// --- keystroke-timing entropy (defense in depth; see pico_ps2kbd.c) ---
static volatile uint32_t entropy_acc;
static volatile uint32_t entropy_count;

static void entropy_mix(void)
{
    uint32_t t = (uint32_t)time_us_64();
    uint32_t x = entropy_acc ^ t;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    entropy_acc = x;
    entropy_count++;
}

uint32_t pico_usbhostkbd_entropy_sample(void) { return entropy_acc; }
uint32_t pico_usbhostkbd_entropy_count(void)  { return entropy_count; }

// --- HID boot-keyboard report decode ---

// Official TinyUSB keycode->ASCII table (src/class/hid/hid.h), not hand
// transcribed: {unshifted, shifted} per USB HID keyboard usage ID 0x00-0x7F.
static const uint8_t keycode2ascii[128][2] = { HID_KEYCODE_TO_ASCII };

// Arrow keys are 0 in keycode2ascii (not ASCII); emit the same ANSI escape
// sequences pico_ps2kbd does, since TNTSCAnsi's input handling expects them.
static const char *special_sequence(uint8_t keycode)
{
    switch (keycode)
    {
        case 0x52: return "\033[A";   // Up
        case 0x51: return "\033[B";   // Down
        case 0x4F: return "\033[C";   // Right
        case 0x50: return "\033[D";   // Left
        default:   return 0;
    }
}

static bool find_key_in_report(const hid_keyboard_report_t *report, uint8_t keycode)
{
    for (int i = 0; i < 6; i++)
        if (report->keycode[i] == keycode) return true;
    return false;
}

// Ctrl handling matches pico_ps2kbd's scancode table scope exactly: letters
// map to their standard control code (Ctrl+A=0x01 .. Ctrl+Z=0x1A), and the
// same four punctuation keys PS/2's table special-cases (` \ [ ]) do too.
// Anything else held with Ctrl passes through as the plain character.
static void emit_key(uint8_t keycode, bool shift, bool ctrl)
{
    const char *seq = special_sequence(keycode);
    if (seq) { for (const char *c = seq; *c; c++) fifo_put(*c); return; }

    if (keycode >= 128) return;
    uint8_t ch = keycode2ascii[keycode][shift ? 1 : 0];
    if (ch == 0) return;

    if (ctrl)
    {
        if (ch >= 'a' && ch <= 'z') ch = (uint8_t)(ch - 'a' + 1);
        else if (ch == '`' || ch == '\\' || ch == '[' || ch == ']') ch = (uint8_t)(ch - 64);
    }
    fifo_put(ch);
}

static void process_kbd_report(const hid_keyboard_report_t *report)
{
    static hid_keyboard_report_t prev_report;
    bool shift = (report->modifier & (KEYBOARD_MODIFIER_LEFTSHIFT | KEYBOARD_MODIFIER_RIGHTSHIFT)) != 0;
    bool ctrl  = (report->modifier & (KEYBOARD_MODIFIER_LEFTCTRL  | KEYBOARD_MODIFIER_RIGHTCTRL))  != 0;

    for (int i = 0; i < 6; i++)
    {
        uint8_t kc = report->keycode[i];
        if (kc && !find_key_in_report(&prev_report, kc))
            emit_key(kc, shift, ctrl);   // newly pressed (not a held-key repeat in this report)
    }
    prev_report = *report;
}

// --- TinyUSB host callbacks (invoked from tuh_task(), i.e. on core1) ---

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t idx, const uint8_t *desc_report, uint16_t desc_len)
{
    (void)desc_report; (void)desc_len;
    tuh_hid_receive_report(dev_addr, idx);   // arm report reception for every HID interface
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t idx)
{
    (void)dev_addr; (void)idx;
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t idx, const uint8_t *report, uint16_t len)
{
    if (tuh_hid_interface_protocol(dev_addr, idx) == HID_ITF_PROTOCOL_KEYBOARD
        && len >= sizeof(hid_keyboard_report_t))
    {
        entropy_mix();   // every received report contributes a timing sample
        process_kbd_report((const hid_keyboard_report_t *)report);
    }
    tuh_hid_receive_report(dev_addr, idx);   // re-arm for the next report
}

// --- core1: the TinyUSB host stack lives here exclusively ---

static void core1_entry(void)
{
    tusb_rhport_init_t host_init = { .role = TUSB_ROLE_HOST, .speed = TUSB_SPEED_AUTO };
    tusb_init(0, &host_init);   // RP2040 has one native USB controller: root port 0
    for (;;) tuh_task();
}

void pico_usbhostkbd_init(void)
{
    fifo_head = fifo_tail = 0;
    entropy_acc = entropy_count = 0;
    multicore_launch_core1(core1_entry);
}
