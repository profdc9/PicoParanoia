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
#include "ff.h"
#include "diskio.h"
#include "fileop.h"
#include "editor.h"

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

// Low-level SD probe: does the card initialize, does a raw sector read work,
// and is the boot signature (0x55 0xAA at offset 510/511) intact?
static void sd_diag(BYTE drv, const char *label) {
    console_puts(label);
    DSTATUS st = disk_initialize(drv);
    console_puts(" init="); console_printint(st);
    if (st & STA_NOINIT) { console_puts(" NOINIT\r\n"); return; }
    static BYTE buf[512];
    DRESULT r = disk_read(drv, buf, 0, 1);
    console_puts(" read="); console_printint(r);
    if (r == RES_OK) {
        console_puts(" sig=");
        console_printuint(buf[510]); console_putch(','); console_printuint(buf[511]);
        console_puts(buf[510] == 0x55 && buf[511] == 0xAA ? " OK" : " BAD");
    }
    console_printcrlf();
}

// Main menu — only the operations ported so far (fileop + editor). Key
// manager, randomness test, and encrypt/decrypt (fileenc) land in later steps.
static const char mainmenu[] =
    "\r\n\r\nM - Mount Drives\r\n\
T - Text Editor\r\n\
N - New File\r\n\
V - View File\r\n\
X - Delete File\r\n\
\r\n\r\nOption: ";
static const char mainmenuoptions[] = "MTNVX";

int main(void) {
    // console_init() brings up video (sets sysclk 126 MHz) and the keyboard;
    // do it before stdio_init_all().
    console_init();

    stdio_init_all();
    printf("\n=== PicoParanoia console demo ===\n");
    crypto_selftest();

    // One-shot low-level SD probe at boot (raw init/read/signature).
    console_clrscr();
    console_highvideo();
    console_puts("PicoParanoia");
    console_lowvideo();
    console_puts(" bring-up\r\n\r\n");
    console_puts("SD low-level probe:\r\n");
    sd_diag(0, "  ciphertext (spi1)");
    sd_diag(1, "  plaintext  (spi0)");
    console_press_space();

    file_mount_volume(0);   // mount both cards (fileop's fs0/fs1)

    for (;;) {
        console_clrscr();
        console_highvideo();
        console_puts("PicoParanoia");
        console_lowvideo();
        console_puts(" by D. Marks\r\n");
        console_puts("Ciphertext card ");
        console_highvideo();
        console_puts(fs0_mounted ? "present" : "absent");
        console_lowvideo();
        console_puts("\r\nPlaintext card ");
        console_highvideo();
        console_puts(fs1_mounted ? "present" : "absent");
        console_lowvideo();

        int option = console_selectmenu(mainmenu, mainmenuoptions);
        switch (option) {
            case 'M': file_mount_volume(0); break;
            case 'T': file_edit();          break;
            case 'N': file_new();           break;
            case 'V': file_view();          break;
            case 'X': file_delete();        break;
        }
    }
}
