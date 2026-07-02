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
#include "random.h"
#include "flashstruct.h"
#include "keymanager.h"

#include <BLAKE2s.h>
#include <GCM.h>
#include <AES.h>
#include <Curve25519.h>

// ---------------- crypto known-answer self-test ----------------

static void crypto_selftest(int *passed, int *failed) {
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
    // Curve25519 RFC 7748 §5.2 KAT. eval() does not clamp (dh1 normally does),
    // so we clamp the input scalar first. This validates the vendored curve on
    // hardware; keymanager exercises dh1()/dh2() (and thus our RNGClass::rand
    // glue) once it lands.
    static const uint8_t c25519_scalar[32] = {
        0xa5,0x46,0xe3,0x6b,0xf0,0x52,0x7c,0x9d,0x3b,0x16,0x15,0x4b,0x82,0x46,0x5e,0xdd,
        0x62,0x14,0x4c,0x0a,0xc1,0xfc,0x5a,0x18,0x50,0x6a,0x22,0x44,0xba,0x44,0x9a,0xc4 };
    static const uint8_t c25519_u[32] = {
        0xe6,0xdb,0x68,0x67,0x58,0x30,0x30,0xdb,0x35,0x94,0xc1,0xa4,0x24,0xb1,0x5f,0x7c,
        0x72,0x66,0x24,0xec,0x26,0xb3,0x35,0x3b,0x10,0xa9,0x03,0xa6,0xd0,0xab,0x1c,0x4c };
    static const uint8_t c25519_out[32] = {
        0xc3,0xda,0x55,0x37,0x9d,0xe9,0xc6,0x90,0x8e,0x94,0xea,0x4d,0xf2,0x8d,0x08,0x4f,
        0x32,0xec,0xcf,0x03,0x49,0x1c,0x71,0xf7,0x54,0xb4,0x07,0x55,0x77,0xa2,0x85,0x52 };
    { uint8_t s[32], r[32];
      memcpy(s, c25519_scalar, 32);
      s[0] &= 0xF8; s[31] = (s[31] & 0x7F) | 0x40;   // X25519 clamp
      Curve25519::eval(r, s, c25519_u);
      (memcmp(r, c25519_out, 32) == 0 ? pass : fail)++; }
    printf("crypto self-test: %d passed, %d failed\n", pass, fail);
    if (passed) *passed = pass;
    if (failed) *failed = fail;
}

// Low-level SD probe: does the card initialize, does a raw sector read work,
// and is the boot signature (0x55 0xAA at offset 510/511) intact?
static void sd_diag(BYTE drv, const char *label) {
    console_puts(label);
    console_puts(":\r\n  ");
    DSTATUS st = disk_initialize(drv);
    console_puts("init="); console_printint(st);
    if (st & STA_NOINIT) { console_puts(" NOINIT\r\n"); return; }
    static BYTE buf[512];
    DRESULT r = disk_read(drv, buf, 0, 1);
    console_puts(" read="); console_printint(r);
    if (r == RES_OK)
        console_puts(buf[510] == 0x55 && buf[511] == 0xAA ? " sig=OK" : " sig=BAD");
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
R - Randomness Test\r\n\
Z - Show Raw Noise\r\n\
C - Capture Entropy to File\r\n\
F - Flash Store Self-Test\r\n\
K - Key Manager\r\n\
\r\n\r\nOption: ";
static const char mainmenuoptions[] = "MTNVXRZCFK";

int main(void) {
    // console_init() brings up video (sets sysclk 126 MHz) and the keyboard;
    // do it before stdio_init_all().
    console_init();

    stdio_init_all();
    printf("\n=== PicoParanoia console demo ===\n");
    int cpass = 0, cfail = 0;
    crypto_selftest(&cpass, &cfail);

    // One-shot boot diagnostics on the TV.
    console_clrscr();
    console_highvideo();
    console_puts("PicoParanoia");
    console_lowvideo();
    console_puts(" bring-up\r\n\r\n");
    console_puts("Crypto self-test: ");
    console_printint(cpass);
    console_puts(" pass ");
    console_printint(cfail);
    console_puts(cfail ? " FAIL\r\n" : " fail\r\n");
    console_puts("(BLAKE2s / AES-GCM / Curve25519)\r\n\r\n");
    console_puts("SD probe:\r\n");
    sd_diag(0, "ciphertext (spi1)");
    sd_diag(1, "plaintext (spi0)");
    random_initialize();
    console_puts("Entropy circuit: ");
    console_puts(random_circuit_check() ? "OK" : "SUSPECT");
    console_printcrlf();
    console_press_space();

    keymanager_initialize();
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
        console_puts("\r\nPriv key: ");
        console_highvideo();
        keymanager_display_key(-1, &current_key_private);
        console_lowvideo();
        console_puts("\r\nPub key:  ");
        console_highvideo();
        keymanager_display_key(-1, &current_key_public);
        console_lowvideo();

        int option = console_selectmenu(mainmenu, mainmenuoptions);
        switch (option) {
            case 'M': file_mount_volume(0); break;
            case 'T': file_edit();          break;
            case 'N': file_new();           break;
            case 'V': file_view();          break;
            case 'X': file_delete();        break;
            case 'R': randomness_test();    break;
            case 'Z': randomness_show();    break;
            case 'C': randomness_capture_to_file(); break;
            case 'F':
                console_clrscr();
                console_gotoxy(1, 4);
                console_puts("Flash store self-test: ");
                console_puts(flashstruct_selftest() ? "PASS" : "FAIL");
                console_press_space();
                break;
            case 'K': keymanager();         break;
        }
    }
}
