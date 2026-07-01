// PicoParanoia — bring-up demo.
//
// Step 1: verifies the vendored crypto subset (AES-256-GCM + BLAKE2s KATs).
// Step 2: composite video via pico_ntsc.
// Step 3: console — consoleio + TNTSCAnsi (VT100) over pico_ntsc, with PS/2
//         keyboard input. This demo echoes typed keys onto the TV.

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"

#include "consoleio.h"

#include <BLAKE2s.h>
#include <GCM.h>
#include <AES.h>

// ---------------- crypto known-answer self-test ----------------

static void crypto_selftest(void) {
    int pass = 0, fail = 0;
    uint8_t out[32];
    static const uint8_t blake_empty[32] = {
        0x69,0x21,0x7a,0x30,0x79,0x90,0x80,0x94,0xe1,0x11,0x21,0xd0,0x42,0x35,0x4a,0x7c,
        0x1f,0x55,0xb6,0x48,0x2c,0xa1,0xa5,0x1e,0x1b,0x25,0x0d,0xfd,0x1e,0xd0,0xee,0xf9 };
    { BLAKE2s b; b.reset(); b.finalize(out, sizeof out);
      (memcmp(out, blake_empty, 32) == 0 ? pass : fail)++; }
    static const uint8_t key[32] = {0}, iv[12] = {0};
    static const uint8_t gcm_tag[16] = {
        0xd0,0xd1,0xc8,0xa7,0x99,0x99,0x6b,0xf0,0x26,0x5b,0x98,0xb5,0xd4,0x8a,0xb9,0x19 };
    { static const uint8_t pt[16] = {0}; uint8_t ct[16], tag[16];
      GCM<AES256> gcm; gcm.setKey(key, 32); gcm.setIV(iv, 12);
      gcm.encrypt(ct, pt, 16); gcm.computeTag(tag, 16);
      (memcmp(tag, gcm_tag, 16) == 0 ? pass : fail)++; }
    printf("crypto self-test: %d passed, %d failed\n", pass, fail);
}

int main(void) {
    // console_init() brings up video (sets sysclk 126 MHz) and the keyboard;
    // do it before stdio_init_all().
    console_init();

    stdio_init_all();
    printf("\n=== PicoParanoia console demo ===\n");
    crypto_selftest();

    console_clrscr();
    console_highvideo();
    console_puts("PicoParanoia");
    console_lowvideo();
    console_puts(" console\r\n");
    console_puts("consoleio + TNTSCAnsi (VT100) over pico_ntsc\r\n\r\n");
    console_puts("Type on the PS/2 keyboard or USB serial - it echoes here.\r\n");
    console_puts("Arrow keys move the cursor; Enter = new line.\r\n\r\n");

    for (;;) {
        int ch = console_getch();
        if (ch == '\r') console_puts("\r\n");   // Enter -> CR+LF
        else            console_putch((char)ch);
        printf("key: %d\n", ch);                // mirror to USB for debugging
    }
}
