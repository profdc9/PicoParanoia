// PicoParanoia — bring-up step 1 self-test.
//
// Verifies the vendored Arduino Cryptography Library subset compiles and runs
// correctly on the RP2040 (copy-to-RAM build) by checking AES-256-GCM and
// BLAKE2s against known-answer test vectors. Output goes to the debug console
// (uart0 by default; see PORTING.md §1.7). No custom hardware required.

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"

#include <BLAKE2s.h>
#include <GCM.h>
#include <AES.h>

static int g_pass;
static int g_fail;

static void print_hex(const char *label, const uint8_t *d, size_t n) {
    printf("%s", label);
    for (size_t i = 0; i < n; i++) printf("%02x", d[i]);
    printf("\n");
}

static void check(const char *name, const uint8_t *got, const uint8_t *want, size_t n) {
    if (memcmp(got, want, n) == 0) {
        printf("[PASS] %s\n", name);
        g_pass++;
    } else {
        printf("[FAIL] %s\n", name);
        print_hex("   got:  ", got, n);
        print_hex("   want: ", want, n);
        g_fail++;
    }
}

// BLAKE2s-256 known-answer vectors.
static void test_blake2s(void) {
    uint8_t out[32];

    static const uint8_t want_empty[32] = {
        0x69,0x21,0x7a,0x30,0x79,0x90,0x80,0x94,0xe1,0x11,0x21,0xd0,0x42,0x35,0x4a,0x7c,
        0x1f,0x55,0xb6,0x48,0x2c,0xa1,0xa5,0x1e,0x1b,0x25,0x0d,0xfd,0x1e,0xd0,0xee,0xf9 };
    BLAKE2s b;
    b.reset();
    b.finalize(out, sizeof out);
    check("BLAKE2s(\"\")", out, want_empty, sizeof out);

    static const uint8_t want_abc[32] = {
        0x50,0x8c,0x5e,0x8c,0x32,0x7c,0x14,0xe2,0xe1,0xa7,0x2b,0xa3,0x4e,0xeb,0x45,0x2f,
        0x37,0x45,0x8b,0x20,0x9e,0xd6,0x3a,0x29,0x4d,0x99,0x9b,0x4c,0x86,0x67,0x59,0x82 };
    b.reset();
    b.update("abc", 3);
    b.finalize(out, sizeof out);
    check("BLAKE2s(\"abc\")", out, want_abc, sizeof out);
}

// AES-256-GCM known-answer vectors (zero key, zero 96-bit IV, no AAD).
static void test_aes256_gcm(void) {
    static const uint8_t key[32] = { 0 };
    static const uint8_t iv[12]  = { 0 };
    uint8_t dummy = 0;

    // Empty plaintext -> tag only.
    {
        static const uint8_t want_tag[16] = {
            0x53,0x0f,0x8a,0xfb,0xc7,0x45,0x36,0xb9,0xa9,0x63,0xb4,0xf1,0xc4,0xcb,0x73,0x8b };
        uint8_t tag[16];
        GCM<AES256> gcm;
        gcm.setKey(key, sizeof key);
        gcm.setIV(iv, sizeof iv);
        gcm.encrypt(&dummy, &dummy, 0);
        gcm.computeTag(tag, sizeof tag);
        check("AES-256-GCM tag (empty)", tag, want_tag, sizeof tag);
    }

    // 16 zero plaintext bytes -> ciphertext + tag.
    {
        static const uint8_t pt[16] = { 0 };
        static const uint8_t want_ct[16] = {
            0xce,0xa7,0x40,0x3d,0x4d,0x60,0x6b,0x6e,0x07,0x4e,0xc5,0xd3,0xba,0xf3,0x9d,0x18 };
        static const uint8_t want_tag[16] = {
            0xd0,0xd1,0xc8,0xa7,0x99,0x99,0x6b,0xf0,0x26,0x5b,0x98,0xb5,0xd4,0x8a,0xb9,0x19 };
        uint8_t ct[16], tag[16];
        GCM<AES256> gcm;
        gcm.setKey(key, sizeof key);
        gcm.setIV(iv, sizeof iv);
        gcm.encrypt(ct, pt, sizeof pt);
        gcm.computeTag(tag, sizeof tag);
        check("AES-256-GCM ct  (16B)", ct, want_ct, sizeof ct);
        check("AES-256-GCM tag (16B)", tag, want_tag, sizeof tag);

        // Round-trip: decrypt and verify the tag.
        uint8_t back[16], rtag[16];
        GCM<AES256> dec;
        dec.setKey(key, sizeof key);
        dec.setIV(iv, sizeof iv);
        dec.decrypt(back, ct, sizeof ct);
        dec.computeTag(rtag, sizeof rtag);
        check("AES-256-GCM decrypt", back, pt, sizeof back);
        check("AES-256-GCM tag verify", rtag, want_tag, sizeof rtag);
    }
}

int main(void) {
    stdio_init_all();

    for (;;) {
        g_pass = 0;
        g_fail = 0;
        printf("\n=== PicoParanoia crypto self-test (bring-up step 1) ===\n");
        test_blake2s();
        test_aes256_gcm();
        printf("RESULT: %d passed, %d failed\n", g_pass, g_fail);
        sleep_ms(3000);
    }
}
