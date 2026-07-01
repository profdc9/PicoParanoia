// PicoParanoia — bring-up demo.
//
// Step 1: verifies the vendored crypto subset (AES-256-GCM + BLAKE2s KATs).
// Step 2: brings up pico_ntsc composite video and cycles a text demo through
// both fonts (unscii-8 regular / thin) and both column modes (80 / 40) so the
// readability tradeoff can be judged on a real TV. Debug over USB-CDC.

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"

#include "pico_ntsc.h"

#include <BLAKE2s.h>
#include <GCM.h>
#include <AES.h>

// ---------------- crypto known-answer self-test (step 1) ----------------

static int g_pass, g_fail;
static void check(const char *name, const uint8_t *got, const uint8_t *want, size_t n) {
    if (memcmp(got, want, n) == 0) { printf("[PASS] %s\n", name); g_pass++; }
    else                          { printf("[FAIL] %s\n", name); g_fail++; }
}
static void crypto_selftest(void) {
    g_pass = g_fail = 0;
    uint8_t out[32];
    static const uint8_t blake_empty[32] = {
        0x69,0x21,0x7a,0x30,0x79,0x90,0x80,0x94,0xe1,0x11,0x21,0xd0,0x42,0x35,0x4a,0x7c,
        0x1f,0x55,0xb6,0x48,0x2c,0xa1,0xa5,0x1e,0x1b,0x25,0x0d,0xfd,0x1e,0xd0,0xee,0xf9 };
    { BLAKE2s b; b.reset(); b.finalize(out, sizeof out);
      check("BLAKE2s(\"\")", out, blake_empty, sizeof out); }
    static const uint8_t key[32] = {0}, iv[12] = {0};
    static const uint8_t gcm_tag[16] = {
        0xd0,0xd1,0xc8,0xa7,0x99,0x99,0x6b,0xf0,0x26,0x5b,0x98,0xb5,0xd4,0x8a,0xb9,0x19 };
    { static const uint8_t pt[16] = {0}; uint8_t ct[16], tag[16];
      GCM<AES256> gcm; gcm.setKey(key, 32); gcm.setIV(iv, 12);
      gcm.encrypt(ct, pt, 16); gcm.computeTag(tag, 16);
      check("AES-256-GCM tag", tag, gcm_tag, 16); }
    printf("crypto self-test: %d passed, %d failed\n", g_pass, g_fail);
}

// ---------------- video demo (step 2) ----------------

static void draw_demo(const char *font_name) {
    pico_ntsc_clear();
    pico_ntsc_put_text(0, 0, "PicoParanoia  pico_ntsc", false);

    char label[64];
    snprintf(label, sizeof label, "Font: %s   Mode: %dcol", font_name, pico_ntsc_cols());
    pico_ntsc_put_text(2, 0, label, false);

    // Full 128-glyph table: 16 per row, rows 4..11.
    for (int g = 0; g < 128; g++)
        pico_ntsc_put_cell(4 + g / 16, g % 16, (uint8_t)g);

    const char *s = "The quick brown fox jumps over the lazy dog 0123456789";
    pico_ntsc_put_text(13, 0, s, false);
    pico_ntsc_put_text(14, 0, s, true);   // reverse video

    pico_ntsc_set_cursor(16, 0, true);
}

int main(void) {
    // Video init sets the system clock to 126 MHz, which the NTSC line timing
    // depends on (must precede stdio init).
    bool video_ok = pico_ntsc_init(PICO_NTSC_MODE_80);

    stdio_init_all();
    printf("\n=== PicoParanoia bring-up ===\n");
    crypto_selftest();
    printf("video: %s\n", video_ok ? "running (GPIO16 sync / GPIO17 luma)" : "FAILED");

    // Alternate the two modes, each using its readability default font
    // (80-col = unscii-8 regular, 40-col = unscii-8 thin).
    for (bool wide = true; ; wide = !wide) {
        pico_ntsc_set_mode(wide ? PICO_NTSC_MODE_80 : PICO_NTSC_MODE_40);
        const char *name = wide ? "unscii-8 (default)" : "unscii-8 thin (default)";
        draw_demo(name);
        printf("showing: %s  %dcol\n", name, pico_ntsc_cols());
        sleep_ms(5000);
    }
}
